#!/usr/bin/env python3
"""Collects the instructions the recompiler translated, picks representatives per instruction "shape",
and writes the generated per-instruction C test functions.
usage: collect.py <PR3.armv7> <syms.txt> <outdir> [per_class]"""
import os, sys, re, json, random, hashlib
bin_, syms_, outdir = sys.argv[1:4]
per_class = int(sys.argv[4]) if len(sys.argv) > 4 else 4
os.environ['PR3_BIN'] = bin_; os.environ['PR3_SYMS'] = syms_
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'recomp'))
import recomp
from recomp import *
os.makedirs(outdir, exist_ok=True)
random.seed(12345)

SKIP = ('FB', 'MGTwitter', 'OA', 'Tapjoy')
unique = {}                       # (word, addr-or-0) -> (addr, ins)
nfunc = 0
for a, name in recomp.syms:
    if not (recomp.TEXT_LO <= a < recomp.TEXT_HI) or name.startswith(SKIP): continue
    end = recomp.func_end(a)
    body, g = recomp.gen_function(name, a, end)
    nfunc += 1
    for ins in g.insns:
        word = int.from_bytes(bytes(ins.bytes), 'little')
        pcrel = any(o.type == ARM_OP_MEM and o.mem.base == ARM_REG_PC for o in ins.operands) or \
            any(o.type == ARM_OP_REG and o.reg == ARM_REG_PC for o in ins.operands)
        unique.setdefault((word, ins.address if pcrel else 0), (ins.address, ins))
print('functions', nfunc, 'unique instruction encodings', len(unique))

def writes_pc(ins):
    m = ins.mnemonic
    if re.match(r'(b|bl|bx|blx|bxj)(eq|ne|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?$', m): return True
    if ins.operands and ins.operands[0].type == ARM_OP_REG and ins.operands[0].reg == ARM_REG_PC and not m.startswith(('cmp', 'cmn', 'tst', 'teq', 'str', 'push', 'stm', 'vstr', 'vst')): return True
    if m.startswith(('pop', 'ldm')) and any(o.type == ARM_OP_REG and o.reg == ARM_REG_PC for o in ins.operands): return True
    return False

def bmn(ins):
    m = ins.mnemonic; suf = recomp.CCSUF.get(ins.cc)
    if suf:
        head, dot, tail = m.partition('.')
        if head.endswith(suf): head = head[:-len(suf)]
        m = head + dot + tail
    return m

def shape(ins):
    k = [bmn(ins), ins.cc, ins.update_flags, ins.writeback, ins.post_index]
    for o in ins.operands:
        if o.type == ARM_OP_REG:
            k.append(('r', o.reg in (ARM_REG_SP, ARM_REG_LR, ARM_REG_PC), o.shift.type, o.shift.value if o.shift.type else 0, o.subtracted))
        elif o.type == ARM_OP_IMM:
            k.append(('i', (o.imm > 0) - (o.imm < 0), o.imm.bit_length() if o.imm >= 0 else (-o.imm).bit_length() | 64, o.subtracted))
        elif o.type == ARM_OP_MEM:
            k.append(('m', o.mem.base == ARM_REG_PC, o.mem.base == ARM_REG_SP, o.mem.index != 0, (o.mem.disp > 0) - (o.mem.disp < 0), o.shift.type, o.shift.value, o.subtracted, o.mem.disp != 0 and abs(o.mem.disp) > 255))
        else:
            k.append(('o', o.type))
    regs = [o.reg for o in ins.operands if o.type == ARM_OP_REG]
    k.append(tuple((i, j) for i in range(len(regs)) for j in range(i + 1, len(regs)) if regs[i] == regs[j]))   # aliasing pattern
    if ins.mnemonic.startswith(('push', 'pop', 'ldm', 'stm', 'vpush', 'vpop', 'vldm', 'vstm')): k.append(len(ins.operands))
    return repr(k)

classes = {}
excluded = {'pc': 0}
for key, (addr, ins) in unique.items():
    if writes_pc(ins):
        excluded['pc'] += 1; continue
    classes.setdefault(shape(ins), []).append((addr, ins))
print('classes', len(classes))
reps = []
for k, lst in classes.items():
    random.shuffle(lst)
    for addr, ins in lst[:per_class]: reps.append((addr, ins))
reps.sort(key=lambda x: x[0])
print('representatives', len(reps))

# -------- generate C --------
chunk = 400
meta = []
files = []
unimpl = []
cur = None
HDR = '#include "arm_rt.h"\ntypedef struct { uint32_t r[13], sp, lr, n, z, cf, v, fn, fz, fc, fv; float s[34]; } TS;\n'
body_files = {}
for idx, (addr, ins) in enumerate(reps):
    fi = idx // chunk
    g = recomp.FuncGen('t', addr, addr + 4)
    txt = g.build()
    lines = g.lines
    try:
        i0 = next(i for i, l in enumerate(lines) if l.startswith('  RT_ENTER('))
        i1 = max(i for i, l in enumerate(lines) if l == '  RETURN_();')
    except StopIteration:
        unimpl.append((addr, 'extract')); continue
    inner = lines[i0 + 1:i1]
    if any('UNIMPL' in l for l in inner): unimpl.append((addr, ins.mnemonic + ' ' + ins.op_str)); continue
    f = ['void t_%d(TS *T) {' % idx,
         '  uint32_t r0=T->r[0],r1=T->r[1],r2=T->r[2],r3=T->r[3],r4=T->r[4],r5=T->r[5],r6=T->r[6],r7=T->r[7],r8=T->r[8],r9=T->r[9],r10=T->r[10],r11=T->r[11],r12=T->r[12],sp=T->sp,lr=T->lr,t0=0,t1=0,t2=0,bx_=0;',
         '  uint32_t n=T->n,z=T->z,cf=T->cf,v=T->v,fn=T->fn,fz=T->fz,fc=T->fc,fv=T->fv; float s[34]; memcpy(s,T->s,sizeof s); CPU cc_; CPU *c=&cc_; (void)c;(void)bx_;(void)t0;(void)t1;(void)t2;']
    f += inner
    f += ['  T->r[0]=r0;T->r[1]=r1;T->r[2]=r2;T->r[3]=r3;T->r[4]=r4;T->r[5]=r5;T->r[6]=r6;T->r[7]=r7;T->r[8]=r8;T->r[9]=r9;T->r[10]=r10;T->r[11]=r11;T->r[12]=r12;T->sp=sp;T->lr=lr;',
          '  T->n=n;T->z=z;T->cf=cf;T->v=v;T->fn=fn;T->fz=fz;T->fc=fc;T->fv=fv; memcpy(T->s,s,sizeof s);', '}']
    body_files.setdefault(fi, []).append('\n'.join(f))
    ops = ins.operands
    mem_base, mem_index, blk_base = [], [], None
    for o in ops:
        if o.type == ARM_OP_MEM:
            if o.mem.base: mem_base.append(ins.reg_name(o.mem.base))
            if o.mem.index: mem_index.append(ins.reg_name(o.mem.index))
    m = ins.mnemonic
    if m.startswith(('push', 'pop', 'vpush', 'vpop')): blk_base = 'sp'
    elif m.startswith(('ldm', 'stm', 'vldm', 'vstm')) and ops and ops[0].type == ARM_OP_REG: blk_base = ins.reg_name(ops[0].reg)
    meta.append({'idx': idx, 'addr': addr, 'word': int.from_bytes(bytes(ins.bytes), 'little'), 'text': '%s %s' % (ins.mnemonic, ins.op_str),
                 'mem_base': mem_base, 'mem_index': mem_index, 'blk_base': blk_base, 'cc': int(ins.cc),
                 'fp': m.startswith('v'), 'bits': bool(re.match(r'v(cvt|mov|ldr|str|ldm|stm|push|pop|mrs|msr|abs|neg|dup)', m))})
nfiles = 0
for fi, fl in sorted(body_files.items()):
    with open(os.path.join(outdir, 'ti_%03d.c' % fi), 'w') as f:
        f.write(HDR + '\n'.join(fl) + '\n')
    nfiles += 1
# function table: only indexes that were generated
with open(os.path.join(outdir, 'ti_table.c'), 'w') as f:
    f.write('typedef struct TS TS;\n')
    for m_ in meta: f.write('void t_%d(void *);\n' % m_['idx'])
    f.write('void (*const ts_funcs[])(void *) = {\n')
    for m_ in meta: f.write('  t_%d,\n' % m_['idx'])
    f.write('};\nconst int ts_nfuncs = %d;\n' % len(meta))
json.dump(meta, open(os.path.join(outdir, 'meta.json'), 'w'))
print('generated', len(meta), 'test functions in', nfiles, 'files;', len(unimpl), 'unimplemented:')
for a, t in unimpl[:20]: print('  0x%x %s' % (a, t))
