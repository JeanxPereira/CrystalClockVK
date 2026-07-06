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

    // 6b) VSUBx.xyzw vf10, vf8, vf6x  (COP2 broadcast group, subop=1 VSUB, bc=0)
    //     Same operand layout as test 6 (dest=0xF, ft=6, fs=8, fd=10) but funct
    //     carries subop=1: funct = (1<<2)|0 = 4 -> word = 0x49E64280 | 4.
    //     fs=vf8={1,2,3,4}, ft=vf6={10,20,30,40} -> fd = fs - ft.x = {-9,-8,-7,-6}.
    //     A VADD-mistake would give {11,12,13,14}; this asserts the true VSUB result.
    poke(mem, 0x6100, {0x49E64284u, 0x03E00008u, 0u});
    EeInterpreter cpu6b(mem);
    cpu6b.vf[8][0] = 1.0f; cpu6b.vf[8][1] = 2.0f; cpu6b.vf[8][2] = 3.0f; cpu6b.vf[8][3] = 4.0f;
    cpu6b.vf[6][0] = 10.0f; cpu6b.vf[6][1] = 20.0f; cpu6b.vf[6][2] = 30.0f; cpu6b.vf[6][3] = 40.0f;
    cpu6b.call(0x6100);
    assert(cpu6b.vf[10][0] == -9.0f && cpu6b.vf[10][1] == -8.0f &&
           cpu6b.vf[10][2] == -7.0f && cpu6b.vf[10][3] == -6.0f);

    // 6c) VMAXx.xyzw vf10, vf8, vf6x  (subop=4 VMAX, bc=0)
    //     funct = (4<<2)|0 = 16 -> word = 0x49E64280 | 0x10 = 0x49E64290.
    //     fs=vf8={1,5,3,8}, ft=vf6={4,20,30,40} -> ft.x=4 broadcast.
    //     max(fs,4) = {4,5,4,8}; a VMINI-mistake would give {1,4,3,4}.
    poke(mem, 0x6200, {0x49E64290u, 0x03E00008u, 0u});
    EeInterpreter cpu6c(mem);
    cpu6c.vf[8][0] = 1.0f; cpu6c.vf[8][1] = 5.0f; cpu6c.vf[8][2] = 3.0f; cpu6c.vf[8][3] = 8.0f;
    cpu6c.vf[6][0] = 4.0f; cpu6c.vf[6][1] = 20.0f; cpu6c.vf[6][2] = 30.0f; cpu6c.vf[6][3] = 40.0f;
    cpu6c.call(0x6200);
    assert(cpu6c.vf[10][0] == 4.0f && cpu6c.vf[10][1] == 5.0f &&
           cpu6c.vf[10][2] == 4.0f && cpu6c.vf[10][3] == 8.0f);

    // 6d) VMINIx.xyzw vf10, vf8, vf6x  (subop=5 VMINI, bc=0)
    //     funct = (5<<2)|0 = 20 -> word = 0x49E64280 | 0x14 = 0x49E64294.
    //     Same operands as 6c: min(fs,4) = {1,4,3,4}; a VMAX-mistake would give
    //     {4,5,4,8}, so this discriminates against test 6c's expectation.
    poke(mem, 0x6300, {0x49E64294u, 0x03E00008u, 0u});
    EeInterpreter cpu6d(mem);
    cpu6d.vf[8][0] = 1.0f; cpu6d.vf[8][1] = 5.0f; cpu6d.vf[8][2] = 3.0f; cpu6d.vf[8][3] = 8.0f;
    cpu6d.vf[6][0] = 4.0f; cpu6d.vf[6][1] = 20.0f; cpu6d.vf[6][2] = 30.0f; cpu6d.vf[6][3] = 40.0f;
    cpu6d.call(0x6300);
    assert(cpu6d.vf[10][0] == 1.0f && cpu6d.vf[10][1] == 4.0f &&
           cpu6d.vf[10][2] == 3.0f && cpu6d.vf[10][3] == 4.0f);

    // 6e) VMULx.xyzw vf10, vf8, vf6x  (subop=6 VMUL, bc=0)
    //     funct = (6<<2)|0 = 24 -> word = 0x49E64280 | 0x18 = 0x49E64298.
    //     fs=vf8={1,2,3,4}, ft=vf6={10,20,30,40} -> fd = fs * ft.x(=10) = {10,20,30,40}
    //     (VMULx broadcasts ft.x across every lane). A per-lane fs*ft mistake would
    //     give {10,40,90,160}; a VADD one {11,12,13,14}; a VSUB one {-9,-8,-7,-6}.
    poke(mem, 0x6400, {0x49E64298u, 0x03E00008u, 0u});
    EeInterpreter cpu6e(mem);
    cpu6e.vf[8][0] = 1.0f; cpu6e.vf[8][1] = 2.0f; cpu6e.vf[8][2] = 3.0f; cpu6e.vf[8][3] = 4.0f;
    cpu6e.vf[6][0] = 10.0f; cpu6e.vf[6][1] = 20.0f; cpu6e.vf[6][2] = 30.0f; cpu6e.vf[6][3] = 40.0f;
    cpu6e.call(0x6400);
    assert(cpu6e.vf[10][0] == 10.0f && cpu6e.vf[10][1] == 20.0f &&
           cpu6e.vf[10][2] == 30.0f && cpu6e.vf[10][3] == 40.0f);

    // 6f) VMSUBx.xyzw vf10, vf8, vf6x  (subop=3 VMSUB, bc=0): fd = ACC - fs*ft.x
    //     funct = (3<<2)|0 = 12 -> word = 0x49E64280 | 0x0C = 0x49E6428C.
    //     ACC={100,100,100,100}, fs=vf8={1,2,3,4}, ft=vf6={10,20,30,40} -> ft.x=10.
    //     fs*ft.x = {10,20,30,40}; ACC - that = {90,80,70,60}. A VMADD-mistake
    //     (ACC + fs*ft.x) would give {110,120,130,140} -- operand order is pinned.
    poke(mem, 0x6500, {0x49E6428Cu, 0x03E00008u, 0u});
    EeInterpreter cpu6f(mem);
    cpu6f.vacc[0] = 100.0f; cpu6f.vacc[1] = 100.0f; cpu6f.vacc[2] = 100.0f; cpu6f.vacc[3] = 100.0f;
    cpu6f.vf[8][0] = 1.0f; cpu6f.vf[8][1] = 2.0f; cpu6f.vf[8][2] = 3.0f; cpu6f.vf[8][3] = 4.0f;
    cpu6f.vf[6][0] = 10.0f; cpu6f.vf[6][1] = 20.0f; cpu6f.vf[6][2] = 30.0f; cpu6f.vf[6][3] = 40.0f;
    cpu6f.call(0x6500);
    assert(cpu6f.vf[10][0] == 90.0f && cpu6f.vf[10][1] == 80.0f &&
           cpu6f.vf[10][2] == 70.0f && cpu6f.vf[10][3] == 60.0f);

    // 7) divu v0, a1 ; mflo v0 ; jr ra ; nop
    //    divu $2,$5 = 000000 00010 00101 00000 00000 011011 = 0x0045001B
    //    mflo v0 = 0x00001012
    //    rs=0xFFFFFFFF, rt=2: signed div truncates -1/2 == 0, but unsigned
    //    0xFFFFFFFF/2 == 0x7FFFFFFF -- these differ, so this catches divu
    //    being wired to the signed division path.
    poke(mem, 0x7000, {0x0045001Bu, 0x00001012u, 0x03E00008u, 0u});
    EeInterpreter cpu7(mem);
    cpu7.gpr[2].lo = 0xFFFFFFFFu;  // v0; call() sets a1 (gpr5) via its args
    assert(cpu7.call(0x7000, 0, 2) == 0x7FFFFFFFu);

    std::printf("ee_interpreter: all assertions passed\n");
    return 0;
}
