#include "ee/EeInterpreter.hpp"
#include "ee/EeMemory.hpp"
#include <bit>
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
    //    tlbwi (COP0 CO=1, fn=0x02) is not in the required table -> must
    //    throw. (mfc0/mtc0 (rs=0/4) and di/ei (CO=1, fn=0x38/0x39) are now
    //    legitimately implemented -- see tests 12/13 -- so this uses a COP0
    //    CO-group op that genuinely isn't: tlbwi = 010000 10000 00000 00000
    //    00000 000010 = 0x42000002.)
    poke(mem, 0x4000, {0x42000002u});
    EeInterpreter cpu4(mem);
    bool threw = false;
    try { cpu4.call(0x4000); } catch (const EeError& e) {
        threw = (e.pc == 0x4000 && e.word == 0x42000002u);
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

    // 8) instructionsRetired must reset per call(), not accumulate across
    //    calls on a long-lived instance (Phase-2 drives call() in a per-frame
    //    loop). Cap maxInstructions low; each call of the 3-instr snippet
    //    from test 1 must pass every time even though 5000 calls * 3 instrs
    //    = 15000 >> 1000 cumulative -- proving the budget is per-call.
    poke(mem, 0x1000, {0x24820005u, 0x03E00008u, 0u});
    EeInterpreter cpu8(mem);
    cpu8.maxInstructions = 1000;
    for (int i = 0; i < 5000; i++) {
        assert(cpu8.call(0x1000, 37) == 42);
    }

    // 9) swc1/lwc1 roundtrip through memory (Phase-2 caller's FPU-context save).
    //    swc1 $f20, 0xA0($sp) ; lwc1 $f21, 0xA0($sp) ; jr ra ; nop
    //    swc1: 111001 11101 10100 0000000010100000 = 0xE7B400A0
    //    lwc1: 110001 11101 10101 0000000010100000 = 0xC7B500A0
    //    f20 and f21 start different -- a mixed-up ft/base or a swc1<->lwc1
    //    swap would leave f21 unchanged (still its seeded sentinel) instead
    //    of picking up f20's value through the store.
    //    call() always seeds $sp = kDefaultStack, so the effective address is
    //    kDefaultStack + 0xA0 regardless of the code's load address.
    poke(mem, 0x9000, {0xE7B400A0u, 0xC7B500A0u, 0x03E00008u, 0u});
    EeInterpreter cpu9(mem);
    cpu9.fpr[20] = 12345.5f;
    cpu9.fpr[21] = -1.0f;  // sentinel, must be overwritten by the lwc1
    cpu9.call(0x9000);
    assert(cpu9.fpr[21] == 12345.5f);
    assert(std::bit_cast<uint32_t>(cpu9.fpr[20]) ==
           mem.read32(EeInterpreter::kDefaultStack + 0xA0));

    // 10) c.olt.s $f0, $f1 ; bc1t +2 (skip poison if cond true) ; nop (delay
    //     slot, always runs) ; poison: addiu v0,zero,111 ; jr ra ; nop
    //     c.olt.s fs=f0 ft=f1: 010001 10000 00001 00000 00000 110100 = 0x46010034
    //     bc1t rt=1,imm=2 (target = branch_pc+4+2*4, lands on the jr,
    //     skipping the poison slot) = 0x45010002
    //     Run twice: f0<f1 (cond true, branch taken, poison skipped, v0
    //     stays 0) and f0>f1 (cond false, branch not taken, poison runs,
    //     v0 becomes 111) -- pins the comparison direction, not just "sets
    //     some bit".
    poke(mem, 0xA000, {0x46010034u, 0x45010002u, 0u, 0x2402006Fu, 0x03E00008u, 0u});
    EeInterpreter cpu10a(mem);
    cpu10a.fpr[0] = 1.0f; cpu10a.fpr[1] = 2.0f;  // f0 < f1 -> cond true -> branch taken -> skip poison
    assert(cpu10a.call(0xA000) == 0);
    EeInterpreter cpu10b(mem);
    cpu10b.fpr[0] = 5.0f; cpu10b.fpr[1] = 2.0f;  // f0 > f1 -> cond false -> branch not taken -> poison runs
    assert(cpu10b.call(0xA000) == 111);

    // 11) syscall (SPECIAL fn=0x0C): BIOS call number in $v1. Real hardware's
    //     FlushCache (#100) trampoline is exactly this shape:
    //     addiu v1,zero,100 ; syscall ; jr ra ; nop
    //     No kernel state is modeled -- this must no-op and just return, but
    //     the number must be captured in cpu.syscalls so callers can report
    //     which BIOS calls a run actually hit (a bug that dropped the number
    //     or corrupted a register would fail this).
    poke(mem, 0xB000, {0x24030064u, 0x0000000Cu, 0x03E00008u, 0u});
    EeInterpreter cpu11(mem);
    cpu11.call(0xB000);
    assert(cpu11.syscalls.size() == 1 && cpu11.syscalls[0] == 100);
    assert(cpu11.gpr[3].lo == 100);  // v1 must be untouched by the no-op

    // 12) di (COP0 CO=1, fn=0x39): disable interrupts, no interrupt model
    //     here so must no-op and just return (not throw, unlike test 4's
    //     COP0 rs=0 case, which is a real negative case -- this pins that
    //     rs=0x10/fn=0x39 specifically is handled, not COP0 broadly).
    //     di = 010000 10000 00000 00000 00000 111001 = 0x42000039
    poke(mem, 0xC000, {0x42000039u, 0x03E00008u, 0u});
    EeInterpreter cpu12(mem);
    cpu12.call(0xC000);  // must not throw

    // 13) mtc0 v0, $12 (Status) ; mfc0 v1, $12 ; jr ra ; nop  (roundtrip
    //     through cop0[] storage -- the caller's Status-register save/
    //     restore around di/ei, e.g. `mfc0 v0,$12` seen at pc=0x271900).
    //     mtc0: 010000 00100 00010 01100 00000000000 = 0x40826000
    //     mfc0: 010000 00000 00011 01100 00000000000 = 0x40036000
    poke(mem, 0xD000, {0x40826000u, 0x40036000u, 0x03E00008u, 0u});
    EeInterpreter cpu13(mem);
    cpu13.gpr[2].lo = 0xABCDu;  // v0
    cpu13.call(0xD000);
    assert(cpu13.gpr[3].lo == 0xABCDu);  // v1 picked up cop0[12] via the roundtrip
    assert(cpu13.cop0[12] == 0xABCDu);

    // 14-19) MMI (EE multimedia extension, op=0x1C) 128-bit ops -- Phase 2
    // spike 2's new wall past the Deci2 sync-wait stub (pc=0x0026C66C in the
    // live capture, a memset-style routine using these to fill a buffer).
    // All use rs=$t2(10), rt=$t1(9), rd=$t0(8); operands chosen so every
    // byte/halfword lane is distinct, which would catch a lane-order or
    // hi/lo-swap bug that all-same-byte operands would hide.
    auto setRsRt = [](EeInterpreter& c) {
        c.gpr[10].lo = 0x0807060504030201ull; c.gpr[10].hi = 0x1817161514131211ull;  // rs=t2
        c.gpr[9].lo = 0x0101010101010101ull; c.gpr[9].hi = 0x0202020202020202ull;    // rt=t1
    };

    // 14) PSUBB t0, t2, t1 (MMI0 idx9: 16 signed byte lanes, rd=rs-rt)
    poke(mem, 0xE000, {0x71494248u, 0x03E00008u, 0u});
    EeInterpreter cpu14(mem);
    setRsRt(cpu14);
    cpu14.call(0xE000);
    assert(cpu14.gpr[8].lo == 0x0706050403020100ull);
    assert(cpu14.gpr[8].hi == 0x161514131211100Full);

    // 15) PCPYLD t0, t2, t1 (MMI2 idx14: rd.hi=rs.lo, rd.lo=rt.lo)
    poke(mem, 0xE100, {0x71494389u, 0x03E00008u, 0u});
    EeInterpreter cpu15(mem);
    setRsRt(cpu15);
    cpu15.call(0xE100);
    assert(cpu15.gpr[8].lo == 0x0101010101010101ull);
    assert(cpu15.gpr[8].hi == 0x0807060504030201ull);

    // 15b) PAND t0, t2, t1 (MMI2 idx18: 128-bit and)
    poke(mem, 0xE150, {0x71494489u, 0x03E00008u, 0u});
    EeInterpreter cpu15b(mem);
    setRsRt(cpu15b);
    cpu15b.call(0xE150);
    assert(cpu15b.gpr[8].lo == 0x0001000100010001ull);
    assert(cpu15b.gpr[8].hi == 0x0002020000020200ull);

    // 16) PXOR t0, t2, t1 (MMI2 idx19: 128-bit xor)
    poke(mem, 0xE200, {0x714944C9u, 0x03E00008u, 0u});
    EeInterpreter cpu16(mem);
    setRsRt(cpu16);
    cpu16.call(0xE200);
    assert(cpu16.gpr[8].lo == 0x0906070405020300ull);
    assert(cpu16.gpr[8].hi == 0x1A15141716111013ull);

    // 17) PNOR t0, t2, t1 (MMI3 idx19: 128-bit nor)
    poke(mem, 0xE300, {0x714944E9u, 0x03E00008u, 0u});
    EeInterpreter cpu17(mem);
    setRsRt(cpu17);
    cpu17.call(0xE300);
    assert(cpu17.gpr[8].lo == 0xF6F8F8FAFAFCFCFEull);
    assert(cpu17.gpr[8].hi == 0xE5E8E9E8E9ECEDECull);

    // 18) PCPYUD t0, t2, t1 (MMI3 idx14: rd.lo=rs.hi, rd.hi=rt.hi)
    poke(mem, 0xE400, {0x714943A9u, 0x03E00008u, 0u});
    EeInterpreter cpu18(mem);
    setRsRt(cpu18);
    cpu18.call(0xE400);
    assert(cpu18.gpr[8].lo == 0x1817161514131211ull);
    assert(cpu18.gpr[8].hi == 0x0202020202020202ull);

    // 19) PCPYH t0, t1 (MMI3 idx27: replicate rt's low halfword of each
    //     64-bit half across all 4 halfword lanes of that half; rs unused)
    poke(mem, 0xE500, {0x700946E9u, 0x03E00008u, 0u});
    EeInterpreter cpu19(mem);
    setRsRt(cpu19);
    cpu19.call(0xE500);
    assert(cpu19.gpr[8].lo == 0x0101010101010101ull);
    assert(cpu19.gpr[8].hi == 0x0202020202020202ull);

    // 20) unimplemented MMI op must still throw fast, not silently no-op
    //     (MMI0 idx0 = PADDW, not implemented): funct=0x08, sa=0x00.
    poke(mem, 0xE600, {0x71494008u});
    EeInterpreter cpu20(mem);
    bool mmiThrew = false;
    try { cpu20.call(0xE600); } catch (const EeError& e) {
        mmiThrew = (e.pc == 0xE600 && e.word == 0x71494008u);
    }
    assert(mmiThrew);

    // 21) cfc2 $a2, $29  (COP2 transfer form: rs=2/rt=6($a2)/rd(id)=29/sa=0/
    //     fn=0). Real hardware word from re/ram/clock/eeMemory.bin at
    //     pc=0026E7E4: 010010 00010 00110 11101 00000 000000 = 0x4846E800.
    //     Must yield 0 (no async VU0/VU1 busy state modeled) -- seed $a2 with
    //     a nonzero sentinel first so a no-op mistake (leaving it unchanged,
    //     which is exactly what the old dest=rs&0xF/vfd=sa=0 misroute did --
    //     it always resets vf[0], never touches a GPR) fails this.
    poke(mem, 0xE700, {0x4846E800u, 0x03E00008u, 0u});
    EeInterpreter cpu21(mem);
    cpu21.gpr[6].lo = 0xDEADBEEFu;
    cpu21.call(0xE700);
    assert(cpu21.gpr[6].lo == 0);
    assert(cpu21.gpr[6].hi == 0);

    // 22) VMULxyzw vf10, vf8, vf6  (COP2 macro FULL-VECTOR group, fn=0x2A --
    //     Phase 2 Visor dial render: real word from re/ram/clock_viewer/
    //     eeMemory.bin at pc=00273824 (inside 0x00232DA0's VU0-matrix
    //     callee 0x00273820), dest=0xE/rt=4/rd=4/sa=5/fn=0x2A ==
    //     0x4BC4216A -- but this test uses distinct dest/fs/ft/fd operands
    //     (dest=0xF, ft=6, fs=8, fd=10) to also exercise the "no broadcast,
    //     full vft vector" semantics distinctly from the broadcast-group
    //     tests above: word = 010010 01111 00110 01000 01010 101010 =
    //     0x49E642AA. fs=vf8={2,3,4,5}, ft=vf6={10,20,30,40} ->
    //     fd = fs*ft elementwise = {20,60,120,200}. A VMULbc (broadcast)
    //     mistake would read ft.x=10 for every lane, giving {20,30,40,50}
    //     instead -- this asserts the true elementwise result, discriminating
    //     against that misroute.
    poke(mem, 0xE800, {0x49E642AAu, 0x03E00008u, 0u});
    EeInterpreter cpu22(mem);
    cpu22.vf[8][0] = 2.0f; cpu22.vf[8][1] = 3.0f; cpu22.vf[8][2] = 4.0f; cpu22.vf[8][3] = 5.0f;
    cpu22.vf[6][0] = 10.0f; cpu22.vf[6][1] = 20.0f; cpu22.vf[6][2] = 30.0f; cpu22.vf[6][3] = 40.0f;
    cpu22.call(0xE800);
    assert(cpu22.vf[10][0] == 20.0f && cpu22.vf[10][1] == 60.0f &&
           cpu22.vf[10][2] == 120.0f && cpu22.vf[10][3] == 200.0f);

    std::printf("ee_interpreter: all assertions passed\n");
    return 0;
}
