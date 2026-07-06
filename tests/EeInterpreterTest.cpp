#include "ee/EeInterpreter.hpp"
#include "ee/EeMemory.hpp"
#include <cassert>
#include <cstdio>

using namespace ps2ee;

static void poke(EeMemory& m, uint32_t addr, std::initializer_list<uint32_t> words) {
    for (uint32_t w : words) { m.write32(addr, w); addr += 4; }
}

int main() {
    EeMemory mem;
    mem.write32(0, 0);  // force RAM allocation
    EeInterpreter cpu(mem);

    // 1) addiu v0, a0, 5 ; jr ra ; nop      (delay slot exercised)
    //    001001 00100 00010 0000000000000101 = 0x24820005
    //    jr ra = 0x03E00008 ; nop = 0
    poke(mem, 0x1000, {0x24820005u, 0x03E00008u, 0u});
    assert(cpu.call(0x1000, 37) == 42);

    // 2) loop: addiu a0,a0,-1 ; bne a0,zero,loop ; addiu v0,v0,1  (branch+slot)
    //    addiu a0,a0,-1 = 0x2484FFFF ; bne a0,zero,-2 = 0x1480FFFE ;
    //    addiu v0,v0,1 = 0x24420001 ; jr ra ; nop
    poke(mem, 0x2000, {0x2484FFFFu, 0x1480FFFEu, 0x24420001u, 0x03E00008u, 0u});
    EeInterpreter cpu2(mem);
    assert(cpu2.call(0x2000, 5) == 5);   // slot executes 5 times

    // 3) lw/sw roundtrip: lw v0, 0(a0) ; sw v0, 4(a0) ; jr ra ; nop
    //    lw = 0x8C820000 ; sw = 0xAC820004
    poke(mem, 0x3000, {0x8C820000u, 0xAC820004u, 0x03E00008u, 0u});
    mem.write32(0x8000, 0xCAFEBABEu);
    EeInterpreter cpu3(mem);
    cpu3.call(0x3000, 0x8000);
    assert(mem.read32(0x8004) == 0xCAFEBABEu);

    // 4) unknown opcode fails fast with pc + word
    //    COP0 (op=0x10) is not in the required table -> must throw.
    //    (0xFC000000, op=0x3F, is actually sd $0,0($0) per the R5900 ISA and
    //     the required table -- a legitimate instruction, not a good negative
    //     case; COP0 is unambiguously unimplemented here.)
    poke(mem, 0x4000, {0x40000000u});
    EeInterpreter cpu4(mem);
    bool threw = false;
    try { cpu4.call(0x4000); } catch (const EeError& e) {
        threw = (e.pc == 0x4000 && e.word == 0x40000000u);
    }
    assert(threw);

    // 5) sync (SPECIAL fn=0x0F): memory barrier, no-op here.
    //    sync.p = 0x0000000F ; jr ra ; nop
    poke(mem, 0x5000, {0x0000000Fu, 0x03E00008u, 0u});
    EeInterpreter cpu5(mem);
    cpu5.call(0x5000);  // must not throw

    // 6) VADDx.xyzw vf10, vf8, vf6x  (COP2 macro broadcast group, fn=0 -> subop=0 VADD, bc=0)
    //    dest=0xF(xyzw), ft=6, fs=8, fd=10: 010010 01111 00110 01000 01010 000000
    //    = 0x49E64280 ; jr ra ; nop
    poke(mem, 0x6000, {0x49E64280u, 0x03E00008u, 0u});
    EeInterpreter cpu6(mem);
    cpu6.vf[8][0] = 1.0f; cpu6.vf[8][1] = 2.0f; cpu6.vf[8][2] = 3.0f; cpu6.vf[8][3] = 4.0f;
    cpu6.vf[6][0] = 10.0f; cpu6.vf[6][1] = 20.0f; cpu6.vf[6][2] = 30.0f; cpu6.vf[6][3] = 40.0f;
    cpu6.call(0x6000);
    assert(cpu6.vf[10][0] == 11.0f && cpu6.vf[10][1] == 12.0f &&
           cpu6.vf[10][2] == 13.0f && cpu6.vf[10][3] == 14.0f);

    // 7) divu v0, a1 ; mflo v0 ; jr ra ; nop
    //    divu $2,$5 = 000000 00010 00101 00000 00000 011011 = 0x0045001B
    //    mflo v0 = 0x00001012
    poke(mem, 0x7000, {0x0045001Bu, 0x00001012u, 0x03E00008u, 0u});
    EeInterpreter cpu7(mem);
    cpu7.gpr[2].lo = 17;  // v0; call() sets a1 (gpr5) via its args
    assert(cpu7.call(0x7000, 0, 5) == 3);  // 17u / 5u = 3

    std::printf("ee_interpreter: all assertions passed\n");
    return 0;
}
