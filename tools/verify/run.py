#!/usr/bin/env python3
"""Instruction-level differential test: every (representative) instruction the game executes is run on
randomised machine states by (a) Unicorn emulating the original armv7 code and (b) the recompiler's C translation.
usage: run.py <PR3.armv7> <gen_dir> [samples_per_instruction] [seed]"""
import sys, os, json, ctypes, random, struct, subprocess, collections
from unicorn import *
from unicorn.arm_const import *

bin_, gen = sys.argv[1], sys.argv[2]
SAMPLES = int(sys.argv[3]) if len(sys.argv) > 3 else 20
SEED = int(sys.argv[4]) if len(sys.argv) > 4 else 1
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
meta = json.load(open(os.path.join(gen, 'meta.json')))

# ---- build the C test library ----
lib_path = os.path.join(gen, 'libti.dylib')
srcs = sorted(f for f in os.listdir(gen) if f.endswith('.c'))
objs = []
procs = []
for f in srcs:
    o = os.path.join(gen, f + '.o'); objs.append(o)
    if not os.path.exists(o) or os.path.getmtime(o) < os.path.getmtime(os.path.join(gen, f)):
        procs.append(subprocess.Popen(['clang', '-c', '-O1', '-w', '-ffp-contract=off', '-I', os.path.join(ROOT, 'PR3Core/include'), os.path.join(gen, f), '-o', o]))
o = os.path.join(gen, 'insn_rt.o'); objs.append(o)
procs.append(subprocess.Popen(['clang', '-c', '-O1', '-w', '-ffp-contract=off', '-I', os.path.join(ROOT, 'PR3Core/include'), os.path.join(HERE, 'insn_rt.c'), '-o', o]))
for p in procs: assert p.wait() == 0
subprocess.check_call(['clang', '-shared', '-o', lib_path] + objs)
lib = ctypes.CDLL(lib_path)
lib.ts_init.restype = ctypes.c_int; lib.ts_init.argtypes = [ctypes.c_char_p]
lib.ts_window.restype = ctypes.c_void_p
lib.ts_faults.restype = ctypes.c_uint32
assert lib.ts_init(bin_.encode()) == len(meta), 'test table mismatch'
window = lib.ts_window()

class TS(ctypes.Structure):
    _fields_ = [('r', ctypes.c_uint32 * 13), ('sp', ctypes.c_uint32), ('lr', ctypes.c_uint32), ('n', ctypes.c_uint32), ('z', ctypes.c_uint32),
                ('cf', ctypes.c_uint32), ('v', ctypes.c_uint32), ('fn', ctypes.c_uint32), ('fz', ctypes.c_uint32), ('fc', ctypes.c_uint32),
                ('fv', ctypes.c_uint32), ('s', ctypes.c_uint32 * 34)]
lib.ts_run.argtypes = [ctypes.c_int, ctypes.POINTER(TS)]

# ---- Unicorn side ----
data = open(bin_, 'rb').read()
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0x1000, 0x11d000); mu.mem_write(0x1000, data[0:1167360]); mu.mem_protect(0x1000, 0x11d000, UC_PROT_READ | UC_PROT_EXEC)
mu.mem_map(0x11e000, 0x6d000); mu.mem_write(0x11e000, data[1167360:1167360 + 57344])
MEM = 0x20000000
mu.mem_map(MEM, 0x10000)
mu.reg_write(UC_ARM_REG_C1_C0_2, mu.reg_read(UC_ARM_REG_C1_C0_2) | (0xf << 20))
mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
CORE = [UC_ARM_REG_R0 + i for i in range(13)] if False else [getattr(__import__('unicorn.arm_const', fromlist=['x']), 'UC_ARM_REG_R%d' % i) for i in range(13)]
CORE += [UC_ARM_REG_SP, UC_ARM_REG_LR]
SREGS = [getattr(__import__('unicorn.arm_const', fromlist=['x']), 'UC_ARM_REG_S%d' % i) for i in range(32)]
D16 = UC_ARM_REG_D16     # the recompiler keeps d16 (a scratch register) in s[32], s[33]

WIN_LO, WIN_LEN = 0x2000, 0x4000
BASE = MEM + 0x4000
rng = random.Random(SEED)

def rv():
    k = rng.random()
    if k < 0.30: return rng.getrandbits(32)
    if k < 0.50: return rng.randint(-64, 64) & 0xffffffff
    if k < 0.65: return rng.choice([0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 0xff, 0xffff, 0x8000, 0x10000, 0x80, 0xffff0000, 0x100, 0xfffffffe])
    if k < 0.75: return rng.getrandbits(8)
    if k < 0.85: return rng.getrandbits(16)
    p = 1 << rng.randrange(32)
    return (p + rng.choice([-1, 0, 1])) & 0xffffffff

def fv(bits_profile):
    k = rng.random()
    if bits_profile and k < 0.5: return rv()
    if k < 0.06: return rng.choice([0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x40000000, 0x3f000000])
    if k < 0.08: return rng.choice([0x7f800000, 0xff800000])
    if k < 0.09: return 0x7fc00000
    e = rng.randint(-20, 20); mant = 1 + rng.random()
    f = mant * (2.0 ** e) * (1 if rng.random() < 0.5 else -1)
    return struct.unpack('<I', struct.pack('<f', f))[0]

def reg_index(name):
    n = {'sp': 13, 'lr': 14, 'ip': 12, 'fp': 11, 'sl': 10, 'sb': 9}.get(name)
    if n is not None: return n
    return int(name[1:]) if name.startswith('r') and name[1:].isdigit() else None

def make_state(m):
    st = [rv() for _ in range(15)]
    word_aligned_only = not any(m['text'].startswith(p) for p in ('ldr ', 'str ', 'ldrh', 'strh', 'ldrsh', 'ldrsb', 'ldrb', 'strb'))
    def ptr():
        off = rng.randrange(-0x400, 0x400, 8 if word_aligned_only else 1) if word_aligned_only else rng.randrange(-0x400, 0x400, 4) + (rng.randrange(4) if rng.random() < 0.2 else 0)
        return BASE + off
    for nme in m['mem_base'] + ([m['blk_base']] if m['blk_base'] else []):
        i = reg_index(nme)
        if i is not None and i < 15: st[i] = ptr()
    for nme in m['mem_index']:
        i = reg_index(nme)
        if i is not None and i < 15: st[i] = rng.choice([rng.randint(0, 40), rng.randint(-20, 20) & 0xffffffff, rng.randrange(0, 128, 4)])
    # sp usually points into the window as well
    if reg_index('sp') is not None and not (m['blk_base'] == 'sp' or 'sp' in m['mem_base']):
        if rng.random() < 0.7: st[13] = BASE + rng.randrange(-0x200, 0x200, 8)
    flags = [rng.randint(0, 1) for _ in range(4)]     # n z c v
    fflags = [rng.randint(0, 1) for _ in range(4)]
    s = [fv(m['bits']) for _ in range(34)]
    mem = bytes(rng.getrandbits(8) for _ in range(WIN_LEN)) if rng.random() < 0.25 else os.urandom(WIN_LEN)
    return st, flags, fflags, s, mem

def nan_bits(x): return (x & 0x7f800000) == 0x7f800000 and (x & 0x7fffff) != 0

results = collections.defaultdict(list)
total = bad = skipped = 0
for m in meta:
    idx = m['idx']; addr = m['addr']
    for k in range(SAMPLES):
        st, flags, fflags, s, mem = make_state(m)
        # ---- unicorn ----
        mu.reg_write(UC_ARM_REG_CPSR, 0x10 | (flags[0] << 31) | (flags[1] << 30) | (flags[2] << 29) | (flags[3] << 28))
        mu.reg_write(UC_ARM_REG_FPSCR, (fflags[0] << 31) | (fflags[1] << 30) | (fflags[2] << 29) | (fflags[3] << 28))
        for r_, v_ in zip(CORE, st): mu.reg_write(r_, v_)
        for r_, v_ in zip(SREGS, s): mu.reg_write(r_, v_)
        mu.reg_write(D16, (s[33] << 32) | s[32])
        mu.mem_write(MEM + WIN_LO, mem)
        try:
            mu.emu_start(addr, addr + 4, count=1)
        except UcError:
            skipped += 1; continue
        if mu.reg_read(UC_ARM_REG_PC) != addr + 4: skipped += 1; continue
        exp_regs = [mu.reg_read(r_) for r_ in CORE]
        cp = mu.reg_read(UC_ARM_REG_CPSR); fp = mu.reg_read(UC_ARM_REG_FPSCR)
        exp_flags = [(cp >> 31) & 1, (cp >> 30) & 1, (cp >> 29) & 1, (cp >> 28) & 1]
        exp_f = [(fp >> 31) & 1, (fp >> 30) & 1, (fp >> 29) & 1, (fp >> 28) & 1]
        exp_s = [mu.reg_read(r_) for r_ in SREGS]
        d16 = mu.reg_read(D16); exp_s += [d16 & 0xffffffff, d16 >> 32]
        exp_mem = bytes(mu.mem_read(MEM + WIN_LO, WIN_LEN))
        # ---- recompiled C ----
        T = TS()
        for i in range(13): T.r[i] = st[i]
        T.sp = st[13]; T.lr = st[14]
        T.n, T.z, T.cf, T.v = flags
        T.fn, T.fz, T.fc, T.fv = fflags
        for i in range(34): T.s[i] = s[i]
        ctypes.memmove(window + WIN_LO, mem, WIN_LEN)
        lib.ts_run(idx, ctypes.byref(T))
        got_mem = ctypes.string_at(window + WIN_LO, WIN_LEN)
        faults = lib.ts_faults()
        total += 1
        diffs = []
        got_regs = list(T.r) + [T.sp, T.lr]
        names = ['r%d' % i for i in range(13)] + ['sp', 'lr']
        for i in range(15):
            if got_regs[i] != exp_regs[i]: diffs.append('%s: want %08x got %08x (in %08x)' % (names[i], exp_regs[i], got_regs[i], st[i]))
        if [T.n, T.z, T.cf, T.v] != exp_flags: diffs.append('NZCV: want %s got %s (in %s)' % (exp_flags, [T.n, T.z, T.cf, T.v], flags))
        if [T.fn, T.fz, T.fc, T.fv] != exp_f: diffs.append('FPSCR nzcv: want %s got %s (in %s)' % (exp_f, [T.fn, T.fz, T.fc, T.fv], fflags))
        for i in range(34):
            a, b = exp_s[i], T.s[i]
            if a != b and not (nan_bits(a) and nan_bits(b)): diffs.append('s%d: want %08x got %08x (in %08x)' % (i, a, b, s[i]))
        if got_mem != exp_mem:
            d = [j for j in range(WIN_LEN) if got_mem[j] != exp_mem[j]]
            diffs.append('mem: %d bytes differ, first at +%#x want %02x got %02x' % (len(d), WIN_LO + d[0], exp_mem[d[0]], got_mem[d[0]]))
        if faults: diffs.append('C faulted %d accesses' % faults)
        if diffs:
            bad += 1
            results[idx].append(diffs)

print('compared %d runs (%d skipped); %d mismatching runs in %d instructions' % (total, skipped, bad, len(results)))
byclass = collections.defaultdict(list)
for idx, lst in results.items():
    m = meta[idx]
    byclass[m['text'].split()[0]].append((m, lst))
for mn, items in sorted(byclass.items(), key=lambda x: -len(x[1])):
    print('\n== %s: %d instruction(s) with mismatches' % (mn, len(items)))
    for m, lst in items[:3]:
        print('  0x%x %-34s %d/%d runs; e.g. %s' % (m['addr'], m['text'], len(lst), SAMPLES, '; '.join(lst[0][:4])))
