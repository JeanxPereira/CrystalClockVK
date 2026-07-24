import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.address.Address;
import java.io.*;
import java.util.*;

public class DumpVuMicro extends GhidraScript {
    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        String outDir = "C:/CodingProjects/Personal/CrystalClockVK/tools/vu/dumps/";
        new File(outDir).mkdirs();
        StringBuilder idx = new StringBuilder("[\n");
        int found = 0;
        // Scan the whole image for VIFcode MPG tags: cmd byte 0x4A in the high byte
        // of a 32-bit VIFcode word: [IMMEDIATE:16][NUM:8][CMD:8], CMD==0x4A -> MPG.
        Address start = mem.getMinAddress();
        Address end = mem.getMaxAddress();
        long lo = start.getOffset(), hi = end.getOffset();
        for (long a = lo; a < hi - 8; a += 4) {
            int word;
            try { word = mem.getInt(toAddr(a)); } catch (Exception e) { continue; }
            int cmd = (word >> 24) & 0x7F;   // top bit is IRQ flag, mask it
            if (cmd != 0x4A) continue;        // MPG
            int num = (word >> 16) & 0xFF;    // count of 64-bit micro-instructions (0 => 256)
            int count = (num == 0) ? 256 : num;
            int loadAddr = word & 0xFFFF;     // dest in VU micro mem (in dwords)
            long dataAddr = a + 4;            // micro data usually follows the tag
            int bytes = count * 8;
            // sanity: require the following bytes to be readable
            byte[] buf = new byte[bytes];
            boolean ok = true;
            try { mem.getBytes(toAddr(dataAddr), buf); } catch (Exception e) { ok = false; }
            if (!ok) continue;
            String name = String.format("mpg_%08x_vu%04x", dataAddr, loadAddr);
            try (FileOutputStream fo = new FileOutputStream(outDir + name + ".bin")) {
                fo.write(buf);
            }
            idx.append(String.format(
                "  {\"elf_addr\": \"0x%08x\", \"vu_target\": \"0x%04x\", \"length\": %d, \"name\": \"%s\"},\n",
                dataAddr, loadAddr, bytes, name));
            found++;
        }
        idx.append("]\n");
        try (FileWriter fw = new FileWriter(outDir + "index.json")) { fw.write(idx.toString()); }
        println("DumpVuMicro: found " + found + " candidate MPG uploads, wrote " + outDir);
    }
}
