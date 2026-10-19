"""Static recompiler: armv7 (ARM mode, VFP + NEON-scalar) -> C.
Each guest function f_<addr>(CPU *c) keeps registers in C locals; calls pass r0-r3/sp through the CPU struct."""
import sys, os, re, struct, bisect, subprocess
from capstone import *
from capstone.arm import *

BIN = __import__('os').environ['PR3_BIN']
SYMS = __import__('os').environ['PR3_SYMS']
TEXT_LO, TEXT_HI = 0x2f74, 0x1042a8      # __text section
data = open(BIN, 'rb').read()

def rd(vm, n=4): return data[vm - 0x1000: vm - 0x1000 + n]
def rd32(vm): return struct.unpack('<I', rd(vm))[0]

syms = []
for l in open(SYMS):
    a, _, s = l.rstrip('\n').partition(' ')
    syms.append((int(a, 16), s))
syms.sort()
saddrs = [a for a, _ in syms]
sym_name = {}
for a, s in syms: sym_name.setdefault(a, s)

stubs = {}
out = subprocess.run(['otool', '-arch', 'armv7', '-Iv', BIN], capture_output=True, text=True).stdout
sec = None
for l in out.splitlines():
    m = re.match(r'Indirect symbols for \((\S+),(\S+)\)', l)
    if m: sec = m.group(2); continue
    m = re.match(r'(0x[0-9a-f]+)\s+\d+\s+(\S+)', l)
    if m and sec == '__symbolstub1': stubs[int(m.group(1), 16)] = m.group(2)

def func_end(a):
    i = bisect.bisect_right(saddrs, a)
    return saddrs[i] if i < len(saddrs) else TEXT_HI

md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
md.detail = True

CC = {ARM_CC_EQ: 'z', ARM_CC_NE: '!z', ARM_CC_HS: 'cf', ARM_CC_LO: '!cf', ARM_CC_MI: 'n', ARM_CC_PL: '!n',
      ARM_CC_VS: 'v', ARM_CC_VC: '!v', ARM_CC_HI: '(cf && !z)', ARM_CC_LS: '(!cf || z)',
      ARM_CC_GE: '(n == v)', ARM_CC_LT: '(n != v)', ARM_CC_GT: '(!z && n == v)', ARM_CC_LE: '(z || n != v)'}
CCSUF = {ARM_CC_EQ: 'eq', ARM_CC_NE: 'ne', ARM_CC_HS: 'hs', ARM_CC_LO: 'lo', ARM_CC_MI: 'mi', ARM_CC_PL: 'pl',
         ARM_CC_VS: 'vs', ARM_CC_VC: 'vc', ARM_CC_HI: 'hi', ARM_CC_LS: 'ls', ARM_CC_GE: 'ge', ARM_CC_LT: 'lt',
         ARM_CC_GT: 'gt', ARM_CC_LE: 'le'}
SFT = {ARM_SFT_ASR: 'ASR', ARM_SFT_LSL: 'LSL', ARM_SFT_LSR: 'LSR', ARM_SFT_ROR: 'ROR'}


class Unsupported(Exception): pass


def regname(ins, r):
    n = ins.reg_name(r)
    return {'ip': 'r12', 'fp': 'r11', 'sl': 'r10', 'sb': 'r9', 'pc': 'pc'}.get(n, n)

def is_s(n): return re.match(r's\d+$', n) is not None
def is_d(n): return re.match(r'd\d+$', n) is not None
def is_q(n): return re.match(r'q\d+$', n) is not None


class FuncGen:
    def __init__(self, name, start, end):
        self.name = name; self.start = start; self.end = end
        self.callees = set()
        self.lines = []
        self.indirect_sites = 0

    # ---------------- helpers -----------------
    def R(self, ins, op):
        """read a core register operand as C expression"""
        n = regname(ins, op.reg)
        if n == 'pc': return '0x%xu' % ((ins.address + 8) & 0xffffffff)
        return n

    def shifted(self, ins, op, carry=False):
        """operand2 expression for a register operand with possible shift"""
        base = self.R(ins, op)
        t, v = op.shift.type, op.shift.value
        if t == 0: return base
        if t in SFT:
            f = SFT[t]
            if v == 0 and f == 'LSL': return base
            return '%s(%s, %d)' % (f, base, v)
        if t == ARM_SFT_RRX: return '(((uint32_t)cf << 31) | (%s >> 1))' % base
        if t in (ARM_SFT_ASR_REG, ARM_SFT_LSL_REG, ARM_SFT_LSR_REG, ARM_SFT_ROR_REG):
            f = {ARM_SFT_ASR_REG: 'ASR', ARM_SFT_LSL_REG: 'LSL', ARM_SFT_LSR_REG: 'LSR', ARM_SFT_ROR_REG: 'ROR'}[t]
            return '%s(%s, %s & 0xffu)' % (f, base, regname(ins, v))
        raise Unsupported('shift %s' % t)

    def op2(self, ins, op):
        if op.type == ARM_OP_IMM: return '0x%xu' % (op.imm & 0xffffffff)
        return self.shifted(ins, op)


    def sh_carry(self, ins, opnd):
        """Statements that set t1 to the shifter carry-out of flexible operand `opnd` (the C flag of S-form logical ops),
        or None when the operand leaves C unchanged."""
        word = int.from_bytes(bytes(ins.bytes), 'little')
        if opnd.type == ARM_OP_IMM:
            if (word >> 25) & 1 and ((word >> 8) & 0xf):       # rotated modified immediate: C = bit 31 of the value
                return ['t2 = %du;' % ((opnd.imm >> 31) & 1)]
            return None
        x = self.R(ins, opnd)
        t, v = opnd.shift.type, opnd.shift.value
        if t == 0: return None
        if t == ARM_SFT_LSL: return ['t2 = (%s >> %d) & 1u;' % (x, 32 - v)] if v else None
        if t in (ARM_SFT_LSR, ARM_SFT_ASR, ARM_SFT_ROR): return ['t2 = (%s >> %d) & 1u;' % (x, v - 1)]
        if t == ARM_SFT_RRX: return ['t2 = %s & 1u;' % x]
        if t in (ARM_SFT_ASR_REG, ARM_SFT_LSL_REG, ARM_SFT_LSR_REG, ARM_SFT_ROR_REG):
            return self.sh_carry_amt({ARM_SFT_ASR_REG: 'asr', ARM_SFT_LSL_REG: 'lsl', ARM_SFT_LSR_REG: 'lsr', ARM_SFT_ROR_REG: 'ror'}[t], x, '(%s & 0xffu)' % regname(ins, v))
        raise Unsupported('shifter carry %s' % t)

    def sh_carry_amt(self, kind, x, amt):
        """register-specified shift amount: C unchanged when the amount is 0"""
        if kind == 'lsl': e = '%s < 32 ? (%s >> (32 - %s)) & 1u : (%s == 32 ? %s & 1u : 0u)' % (amt, x, amt, amt, x)
        elif kind == 'lsr': e = '%s < 32 ? (%s >> (%s - 1)) & 1u : (%s == 32 ? %s >> 31 : 0u)' % (amt, x, amt, amt, x)
        elif kind == 'asr': e = '%s < 32 ? (%s >> (%s - 1)) & 1u : %s >> 31' % (amt, x, amt, x)
        else: e = '(%s & 31u) == 0 ? %s >> 31 : (%s >> ((%s & 31u) - 1)) & 1u' % (amt, x, x, amt)
        return ['t2 = cf; if (%s != 0) t2 = %s;' % (amt, e)]

    def sh_carry_pseudo(self, ins, mn, x, ops):
        """carry-out of the lsl/lsr/asr/ror/rrx pseudo-instruction forms"""
        if mn == 'rrx': return ['t2 = %s & 1u;' % x]
        if ops[2].type == ARM_OP_IMM:
            n = ops[2].imm
            if mn == 'lsl': return ['t2 = (%s >> %d) & 1u;' % (x, 32 - n)] if n else None
            return ['t2 = (%s >> %d) & 1u;' % (x, n - 1)]
        return self.sh_carry_amt(mn, x, '(%s & 0xffu)' % self.R(ins, ops[2]))

    def label(self, a): return 'L_%x' % a

    def mem_addr(self, ins, memop, post_imm_op=None):
        """returns (addr_expr, writeback_stmt or None, pre_stmt)"""
        m = memop.mem
        base = regname(ins, m.base)
        if base == 'pc':
            basev = '0x%xu' % ((ins.address + 8) & 0xffffffff)
        else:
            basev = base
        if post_imm_op is not None:
            # [rn], #imm  /  [rn], +/-rm {,shift}
            o = post_imm_op
            if o.type == ARM_OP_IMM: delta = '%d' % (-abs(o.imm) if o.subtracted else o.imm)   # capstone reports #-4 as imm=4, subtracted
            else:
                e = self.shifted(ins, o)
                delta = ('-' if o.subtracted else '') + e
            return basev, '%s = %s + (uint32_t)(%s);' % (base, base, delta)
        # offset / pre-index
        if m.index:
            idx = regname(ins, m.index)
            idxv = idx if idx != 'pc' else '0x%xu' % (ins.address + 8)
            sh = memop.shift
            if sh.type != 0:
                e = '%s(%s, %d)' % (SFT[sh.type], idxv, sh.value) if sh.type in SFT else idxv
            else: e = idxv
            expr = '%s %s %s' % (basev, '-' if memop.subtracted else '+', e)
        elif m.disp:
            expr = '%s + (uint32_t)(%d)' % (basev, m.disp)
        else:
            expr = basev
        if ins.writeback:
            return '(%s)' % base, '%s = %s;' % (base, expr) , True   # pre-index: write first then use
        return '(%s)' % expr, None

    def emit(self, s): self.lines.append(s)

    # ---------------- main -----------------
    def build(self):
        code = rd(self.start, self.end - self.start)
        insns = []
        for off in range(0, len(code) - 3, 4):
            for x in md.disasm(code[off:off + 4], self.start + off):
                insns.append(x)
        # literal pools
        lits = set()
        for ins in insns:
            if ins.mnemonic.startswith(('ldr', 'vldr')) and len(ins.operands) >= 2:
                for o in ins.operands:
                    if o.type == ARM_OP_MEM and regname(ins, o.mem.base) == 'pc' and not o.mem.index:
                        lits.add((ins.address + 8 + o.mem.disp) & ~3)
        data_words = set()
        ls = sorted(lits)
        i = 0
        while i < len(ls):
            j = i
            while j + 1 < len(ls) and ls[j + 1] - ls[j] <= 8: j += 1
            for a_ in range(ls[i], ls[j] + 4, 4): data_words.add(a_)
            i = j + 1
        # jump tables: "ldr pc,[pc,rX,lsl #2]" followed by words
        self.jump_tables = []
        insns2 = []
        skip = set()
        for k, ins in enumerate(insns):
            if ins.address in skip: continue
            insns2.append(ins)
            if ins.mnemonic.startswith('ldr') and ins.operands and ins.operands[0].type == ARM_OP_REG and regname(ins, ins.operands[0].reg) == 'pc':
                # table of code addresses follows (after one padding word for ldr pc,[pc,rX,lsl#2])
                a_ = ins.address + 8
                tbl = []
                while True:
                    try: v = rd32(a_)
                    except Exception: break
                    if self.start <= v < self.end and (v & 3) == 0: tbl.append(v); a_ += 4
                    else: break
                if tbl:
                    self.jump_tables.append((ins.address, ins.address + 8, tbl))
                    for x in range(ins.address + 4, a_, 4): skip.add(x)
        insns = [x for x in insns2 if x.address not in data_words]
        self.insns = insns
        # branch targets
        targets = set()
        for ins in insns:
            if ins.id in (ARM_INS_B, ARM_INS_BL) and ins.operands and ins.operands[0].type == ARM_OP_IMM:
                t = ins.operands[0].imm
                if ins.id == ARM_INS_B and self.start <= t < self.end: targets.add(t)
        for (_, _, tbl) in self.jump_tables:
            for t in tbl: targets.add(t)
        for k, ins in enumerate(insns):
            if ins.mnemonic.startswith('add') and ins.operands and ins.operands[0].type == ARM_OP_REG and regname(ins, ins.operands[0].reg) == 'pc':
                j = k + 1
                while j < len(insns) and insns[j].id == ARM_INS_B and insns[j].cc == ARM_CC_AL:
                    targets.add(insns[j].address); j += 1
        self.targets = targets
        # after a call? no. translate
        self.emit('void f_%x(CPU *c) {  /* %s */' % (self.start, self.name.replace('*/', '* /')))
        self.emit('  uint32_t r0 = c->r[0], r1 = c->r[1], r2 = c->r[2], r3 = c->r[3], r4 = 0, r5 = 0, r6 = 0, r7 = 0, r8 = 0, r9 = 0, r10 = 0, r11 = 0, r12 = 0, sp = c->sp, lr = c->lr, entry_lr = c->lr, bx_ = 0, t0, t1, t2;')
        self.emit('#define BX_(x) do { bx_ = (x); goto dispatch_; } while (0)')
        self.emit('  uint32_t n = 0, z = 0, cf = 0, v = 0, fn = 0, fz = 0, fc = 0, fv = 0;')
        self.emit('  float s[34] = {0}; (void)t0; (void)t1; (void)t2; (void)fn; (void)fz; (void)fc; (void)fv; (void)s;')
        self.emit('  (void)r4;(void)r5;(void)r6;(void)r7;(void)r8;(void)r9;(void)r10;(void)r11;(void)r12;(void)lr;(void)n;(void)z;(void)cf;(void)v;')
        self.emit('  RT_ENTER(0x%x);' % self.start)
        for ins in insns:
            if ins.address in targets: self.emit('L_%x:;' % ins.address)
            try:
                self.tr(ins)
            except Unsupported as e:
                self.emit('  UNIMPL("0x%x %s %s: %s");' % (ins.address, ins.mnemonic, ins.op_str.replace('"', "'"), e))
        # falling off the end: treat as return
        self.emit('  RETURN_();')
        self.emit('dispatch_:;')
        self.emit('  if (bx_ == entry_lr) RETURN_();')
        if self.targets:
            self.emit('  switch (bx_) {')
            for t_ in sorted(self.targets): self.emit('    case 0x%xu: goto L_%x;' % (t_, t_))
            self.emit('    default: break;')
            self.emit('  }')
        self.emit('  c->r[0] = r0; c->r[1] = r1; c->r[2] = r2; c->r[3] = r3; c->sp = sp; c->lr = entry_lr;')
        self.emit('  rt_call_indirect(c, bx_);')
        self.emit('  return;')
        self.emit('#undef BX_')
        self.emit('}')
        return '\n'.join(self.lines)

    def cond_wrap(self, ins, stmts):
        if ins.cc in (ARM_CC_AL, ARM_CC_INVALID) or ins.cc not in CC:
            for s in stmts: self.emit('  ' + s)
        else:
            self.emit('  if (%s) {' % CC[ins.cc])
            for s in stmts: self.emit('    ' + s)
            self.emit('  }')

    def base_mnemonic(self, ins):
        m = ins.mnemonic
        suf = CCSUF.get(ins.cc)
        if suf:
            # strip cc suffix, keeping any .type suffix
            head, dot, tail = m.partition('.')
            if head.endswith(suf): head = head[:-len(suf)]
            m = head + dot + tail
        return m

    def setflags_nz(self, res): return ['n = (%s) >> 31; z = ((%s) == 0);' % (res, res)]

    def tr(self, ins):
        ops = ins.operands
        m = self.base_mnemonic(ins)
        S = ins.update_flags and m.endswith('s') and not m.startswith(('v', 'cmp', 'cmn', 'tst', 'teq'))   # capstone also flags adc/sbc/rsc as flag-updating
        mn = m
        if S and mn.endswith('s'): mn = mn[:-1]
        st = []
        a = ins.address
        nxt = a + 4

        def dst(): return regname(ins, ops[0].reg)

        # ---- data processing ----
        if mn in ('mov', 'movw', 'mvn', 'movt'):
            d = dst()
            if mn == 'movt':
                st.append('%s = (%s & 0xffffu) | (0x%xu << 16);' % (d, d, ops[1].imm & 0xffff))
            else:
                v = self.op2(ins, ops[1])
                if mn == 'mvn': v = '(~%s)' % v
                if d == 'pc': return self.branch_reg(ins, v)
                sc = self.sh_carry(ins, ops[1]) if S else None
                if sc: st += sc
                st.append('%s = %s;' % (d, v))
                if S: st += self.setflags_nz(d)
                if sc: st.append('cf = t2;')
            return self.cond_wrap(ins, st)
        if mn in ('lsl', 'lsr', 'asr', 'ror', 'rrx') and ops[0].type == ARM_OP_REG:
            d = dst(); x = self.R(ins, ops[1])
            if mn == 'rrx': e = '(((uint32_t)cf << 31) | (%s >> 1))' % x
            elif len(ops) == 2:
                e = self.shifted(ins, ops[1]) if ops[1].shift.type != 0 else x
            else:
                f = mn.upper()
                amt = ('%d' % ops[2].imm) if ops[2].type == ARM_OP_IMM else '(%s & 0xffu)' % self.R(ins, ops[2])
                e = '%s(%s, %s)' % (f, x, amt)
            sc = None
            if S:
                sc = self.sh_carry_pseudo(ins, mn, x, ops) if (mn == 'rrx' or len(ops) > 2) else self.sh_carry(ins, ops[1])
            if sc: st += sc
            st.append('%s = %s;' % (d, e))
            if S: st += self.setflags_nz(d)
            if sc: st.append('cf = t2;')
            return self.cond_wrap(ins, st)
        if mn in ('add', 'sub', 'rsb', 'adc', 'sbc', 'rsc', 'and', 'eor', 'orr', 'bic'):
            d = dst()
            if len(ops) == 2:   # two-operand form: rd = rd op op2
                x = self.R(ins, ops[0]); y = self.op2(ins, ops[1])
            else:
                x = self.R(ins, ops[1]); y = self.op2(ins, ops[2])
            if mn == 'add':   e = '%s + %s' % (x, y)
            elif mn == 'sub': e = '%s - %s' % (x, y)
            elif mn == 'rsb': e = '%s - %s' % (y, x)
            elif mn == 'adc': e = '%s + %s + cf' % (x, y)
            elif mn == 'sbc': e = '%s - %s - (1 - cf)' % (x, y)
            elif mn == 'rsc': e = '%s - %s - (1 - cf)' % (y, x)
            elif mn == 'and': e = '%s & %s' % (x, y)
            elif mn == 'eor': e = '%s ^ %s' % (x, y)
            elif mn == 'orr': e = '%s | %s' % (x, y)
            elif mn == 'bic': e = '%s & ~(%s)' % (x, y)
            if d == 'pc':
                return self.branch_reg(ins, '(%s)' % e)
            if S:
                if mn not in ('and', 'orr', 'eor', 'bic'): st.append('t0 = %s; t1 = %s;' % ((x, y) if mn not in ('rsb', 'rsc') else (y, x)))
                if mn in ('add', 'adc'):
                    cin = 'cf' if mn == 'adc' else '0'
                    st.append('{ uint64_t w = (uint64_t)t0 + t1 + %s; %s = (uint32_t)w; cf = (uint32_t)(w >> 32); v = (~(t0 ^ t1) & (t0 ^ %s)) >> 31; }' % (cin, d, d))
                elif mn in ('sub', 'rsb', 'sbc', 'rsc'):
                    bin_ = '(1 - cf)' if mn in ('sbc', 'rsc') else '0'
                    st.append('{ uint64_t w = (uint64_t)t0 - t1 - %s; %s = (uint32_t)w; cf = !(w >> 32); v = ((t0 ^ t1) & (t0 ^ %s)) >> 31; }' % (bin_, d, d))
                else:
                    sc = self.sh_carry(ins, ops[-1])
                    if sc: st += sc
                    st.append('%s = %s;' % (d, e))
                    st += self.setflags_nz(d)
                    if sc: st.append('cf = t2;')
                    return self.cond_wrap(ins, st)
                st += self.setflags_nz(d)
            else:
                st.append('%s = %s;' % (d, e))
            return self.cond_wrap(ins, st)
        if mn in ('cmp', 'cmn', 'tst', 'teq'):
            x = self.R(ins, ops[0]); y = self.op2(ins, ops[1])
            st.append('t0 = %s; t1 = %s;' % (x, y))
            if mn == 'cmp':
                st.append('{ uint32_t r_ = t0 - t1; n = r_ >> 31; z = (r_ == 0); cf = (t0 >= t1); v = ((t0 ^ t1) & (t0 ^ r_)) >> 31; }')
            elif mn == 'cmn':
                st.append('{ uint32_t r_ = t0 + t1; n = r_ >> 31; z = (r_ == 0); cf = (r_ < t0); v = (~(t0 ^ t1) & (t0 ^ r_)) >> 31; }')
            elif mn == 'tst':
                sc = self.sh_carry(ins, ops[1])
                if sc: st += sc
                st.append('{ uint32_t r_ = t0 & t1; n = r_ >> 31; z = (r_ == 0); }')
                if sc: st.append('cf = t2;')
            else:
                sc = self.sh_carry(ins, ops[1])
                if sc: st += sc
                st.append('{ uint32_t r_ = t0 ^ t1; n = r_ >> 31; z = (r_ == 0); }')
                if sc: st.append('cf = t2;')
            return self.cond_wrap(ins, st)
        if mn in ('mul', 'mla', 'mls'):
            d = dst(); x = self.R(ins, ops[1]); y = self.R(ins, ops[2])
            e = '%s * %s' % (x, y)
            if mn == 'mla': e += ' + %s' % self.R(ins, ops[3])
            if mn == 'mls': e = '%s - (%s)' % (self.R(ins, ops[3]), e)
            st.append('%s = %s;' % (d, e))
            if S: st += self.setflags_nz(d)
            return self.cond_wrap(ins, st)
        if mn in ('smull', 'umull', 'smlal', 'umlal'):
            lo, hi = regname(ins, ops[0].reg), regname(ins, ops[1].reg)
            x, y = self.R(ins, ops[2]), self.R(ins, ops[3])
            if mn.startswith('s'): p = '(int64_t)(int32_t)%s * (int32_t)%s' % (x, y)
            else: p = '(uint64_t)%s * %s' % (x, y)
            if mn.endswith('lal'):
                st.append('{ uint64_t w = (uint64_t)(%s) + (((uint64_t)%s << 32) | %s); %s = (uint32_t)w; %s = (uint32_t)(w >> 32); }' % (p, hi, lo, lo, hi))
            else:
                st.append('{ uint64_t w = (uint64_t)(%s); %s = (uint32_t)w; %s = (uint32_t)(w >> 32); }' % (p, lo, hi))
            if S: st.append('n = %s >> 31; z = (%s == 0 && %s == 0);' % (hi, hi, lo))
            return self.cond_wrap(ins, st)
        if mn in ('uxtb', 'sxtb', 'uxth', 'sxth'):
            d = dst(); x = self.R(ins, ops[1])
            if len(ops) > 2 and ops[1].shift.type == 0 and ops[2].type == ARM_OP_IMM: x = 'ROR(%s, %d)' % (x, ops[2].imm)
            e = {'uxtb': '(uint8_t)', 'sxtb': '(uint32_t)(int32_t)(int8_t)', 'uxth': '(uint16_t)', 'sxth': '(uint32_t)(int32_t)(int16_t)'}[mn] + '(%s)' % x
            st.append('%s = %s;' % (d, e))
            return self.cond_wrap(ins, st)
        if mn == 'clz':
            st.append('%s = %s ? (uint32_t)__builtin_clz(%s) : 32u;' % (dst(), self.R(ins, ops[1]), self.R(ins, ops[1])))
            return self.cond_wrap(ins, st)
        if mn in ('rev',):
            st.append('%s = __builtin_bswap32(%s);' % (dst(), self.R(ins, ops[1])))
            return self.cond_wrap(ins, st)
        if mn == 'nop': return
        if mn in ('mcr', 'mcrr', 'mrc', 'mrrc', 'stc', 'stcl', 'ldc', 'ldcl', 'cdp', 'svc', 'mrs', 'msr', 'cdp2', 'ldc2', 'stc2', 'mcr2', 'mrc2'):
            return      # literal pool bytes mis-decoded as coprocessor / syscall instructions
        if mn == 'rev16':
            st.append('%s = ((%s & 0x00ff00ffu) << 8) | ((%s >> 8) & 0x00ff00ffu);' % (dst(), self.R(ins, ops[1]), self.R(ins, ops[1])))
            return self.cond_wrap(ins, st)
        if mn in ('sxtah', 'uxtah', 'uxtab'):
            x = self.R(ins, ops[2])
            if ops[2].shift.type: x = self.shifted(ins, ops[2])
            cast = {'sxtah': '(uint32_t)(int32_t)(int16_t)', 'uxtah': '(uint16_t)', 'uxtab': '(uint8_t)'}[mn]
            st.append('%s = %s + %s(%s);' % (dst(), self.R(ins, ops[1]), cast, x))
            return self.cond_wrap(ins, st)
        if mn == 'sxtab':
            x = self.R(ins, ops[2])
            if ops[2].shift.type: x = self.shifted(ins, ops[2])
            st.append('%s = %s + (uint32_t)(int32_t)(int8_t)(%s);' % (dst(), self.R(ins, ops[1]), x))
            return self.cond_wrap(ins, st)
        if mn == 'uxtb16':
            x = self.R(ins, ops[1])
            if ops[1].shift.type: x = self.shifted(ins, ops[1])
            st.append('%s = (%s) & 0x00ff00ffu;' % (dst(), x))
            return self.cond_wrap(ins, st)
        # ---- loads / stores ----
        if mn in ('ldr', 'ldrb', 'ldrh', 'ldrsb', 'ldrsh', 'str', 'strb', 'strh', 'ldrd', 'strd', 'ldrbt', 'strbt'):
            return self.ldst(ins, mn, ops, st)
        if mn in ('push', 'pop', 'ldm', 'stm', 'ldmia', 'stmia', 'ldmib', 'stmib', 'ldmda', 'stmda', 'ldmdb', 'stmdb', 'stmfd', 'ldmfd'):
            return self.block_xfer(ins, mn, ops, st)
        if mn in ('b', 'bl', 'bx', 'blx'):
            return self.branch(ins, mn, ops)
        if mn.startswith('v') or mn in ('vmrs', 'vmsr'):
            return self.vfp(ins, mn, ops, st)
        raise Unsupported('mnemonic ' + mn)

    # ---- memory ----
    def ldst(self, ins, mn, ops, st):
        rt = ops[0]
        # [rn], #imm / [rn], +-rm: capstone does not reliably set post_index (never for halfword/register forms),
        # but the post-index delta is always an extra operand after the memory operand
        mi = next(i for i, o in enumerate(ops) if o.type == ARM_OP_MEM)
        memop = ops[mi]
        post = ops[mi + 1] if len(ops) > mi + 1 else None
        res = self.mem_addr(ins, memop, post)
        a = res[0]; wb = res[1]
        pre_wb = len(res) > 2
        rtn = regname(ins, rt.reg)
        if wb and pre_wb: st.append(wb)       # pre-indexed writeback first
        if mn in ('ldr', 'ldrb', 'ldrh', 'ldrsb', 'ldrsh', 'ldrbt'):
            fn = {'ldr': 'LD32', 'ldrb': 'LD8', 'ldrbt': 'LD8', 'ldrh': 'LD16', 'ldrsb': 'LDS8', 'ldrsh': 'LDS16'}[mn]
            # constant pool load
            if rtn == 'pc':
                st.append('t0 = %s(%s);' % (fn, a))
                if wb and not pre_wb: st.append(wb)
                self.cond_wrap(ins, st + ['BX_(t0);'])
                return
            if memop.mem.base == ARM_REG_PC and not memop.mem.index and post is None:
                addr = (ins.address + 8 + memop.mem.disp) & 0xffffffff
                if fn == 'LD32' and 0x1000 <= addr < 0x11b100:
                    st.append('%s = 0x%xu;' % (rtn, rd32(addr)))
                    return self.cond_wrap(ins, st)
            st.append('%s = %s(%s);' % (rtn, fn, a))
            if wb and not pre_wb: st.append(wb)
        elif mn in ('str', 'strb', 'strh', 'strbt'):
            fn = {'str': 'ST32', 'strb': 'ST8', 'strbt': 'ST8', 'strh': 'ST16'}[mn]
            val = self.R(ins, rt)
            st.append('%s(%s, %s);' % (fn, a, val))
            if wb and not pre_wb: st.append(wb)
        elif mn == 'ldrd':
            rt2 = regname(ins, ops[1].reg)
            st.append('t0 = %s;' % a)
            st.append('%s = LD32(t0); %s = LD32(t0 + 4);' % (rtn, rt2))
            if wb and not pre_wb: st.append(wb)
        elif mn == 'strd':
            st.append('t0 = %s;' % a)
            st.append('ST32(t0, %s); ST32(t0 + 4, %s);' % (self.R(ins, rt), self.R(ins, ops[1])))
            if wb and not pre_wb: st.append(wb)
        self.cond_wrap(ins, st)

    def block_xfer(self, ins, mn, ops, st):
        regs = [regname(ins, o.reg) for o in ops if o.type == ARM_OP_REG]
        if mn in ('push', 'stmfd', 'stmdb') and (mn == 'push' or True):
            base = 'sp' if mn == 'push' else regname(ins, ops[0].reg)
            lst = regs if mn == 'push' else regs[1:]
            if mn != 'push' and not ins.writeback: raise Unsupported('stmdb without wb')
            n = len(lst)
            st.append('%s -= %d;' % (base, 4 * n))
            for i, r in enumerate(lst):
                v = r if r != 'pc' else '0x%xu' % (ins.address + 8)
                st.append('ST32(%s + %d, %s);' % (base, 4 * i, v))
            return self.cond_wrap(ins, st)
        if mn in ('pop',) or (mn in ('ldm', 'ldmia', 'ldmfd') and regname(ins, ops[0].reg) == 'sp' and ins.writeback):
            lst = regs if mn == 'pop' else regs[1:]
            for i, r in enumerate(lst):
                if r == 'pc': continue
                st.append('%s = LD32(sp + %d);' % (r, 4 * i))
            n = len(lst)
            if 'pc' in lst:
                st.append('t0 = LD32(sp + %d);' % (4 * lst.index('pc')))
                st.append('sp += %d;' % (4 * n))
                st.append('BX_(t0);')
            else:
                st.append('sp += %d;' % (4 * n))
            return self.cond_wrap(ins, st)
        # generic ldm/stm
        base = regname(ins, ops[0].reg); lst = regs[1:]
        mode = {'ldm': 'ia', 'stm': 'ia', 'ldmia': 'ia', 'stmia': 'ia', 'ldmib': 'ib', 'stmib': 'ib', 'ldmda': 'da', 'stmda': 'da', 'ldmdb': 'db', 'stmdb': 'db'}[mn]
        n = len(lst)
        if mode == 'ia': start = '0'
        elif mode == 'ib': start = '4'
        elif mode == 'da': start = '-%d' % (4 * (n - 1))
        else: start = '-%d' % (4 * n)
        newbase = {'ia': '+ %d' % (4 * n), 'ib': '+ %d' % (4 * n), 'da': '- %d' % (4 * n), 'db': '- %d' % (4 * n)}[mode]
        st.append('t0 = %s;' % base)
        is_load = mn.startswith('ldm')
        for i, r in enumerate(lst):
            if is_load:
                if r == 'pc': st.append('t1 = LD32(t0 + (%s) + %d);' % (start, 4 * i))
                else: st.append('%s = LD32(t0 + (%s) + %d);' % (r, start, 4 * i))
            else:
                v = r if r != 'pc' else '0x%xu' % (ins.address + 8)
                st.append('ST32(t0 + (%s) + %d, %s);' % (start, 4 * i, v))
        if ins.writeback: st.append('%s = t0 %s;' % (base, newbase))
        if is_load and 'pc' in lst: st.append('BX_(t1);')
        self.cond_wrap(ins, st)

    # ---- branches ----
    def branch(self, ins, mn, ops):
        a = ins.address
        if mn in ('b', 'bl') and ops[0].type == ARM_OP_IMM:
            t = ops[0].imm & 0xffffffff
            if mn == 'b':
                if self.start <= t < self.end:
                    return self.cond_wrap(ins, ['goto L_%x;' % t])
                # tail call to another function
                return self.cond_wrap(ins, [self.callstmt(t, tail=True)])
            return self.cond_wrap(ins, [self.callstmt(t, nxt=a + 4)])
        # register forms
        r = self.R(ins, ops[0])
        if mn == 'bx':
            if r == 'lr': return self.cond_wrap(ins, ['RETURN_();'])
            return self.cond_wrap(ins, ['BX_(%s);' % r])
        if mn == 'blx':
            self.indirect_sites += 1
            return self.cond_wrap(ins, ['CALL_IND(%s, 0x%xu);' % (r, a + 4)])
        raise Unsupported('branch')

    def branch_reg(self, ins, expr):
        # write to pc (mov pc,lr / add pc,... )
        self.cond_wrap(ins, ['BX_(%s);' % expr])

    def callstmt(self, t, tail=False, nxt=0):
        if t in stubs:
            f = 'stub_%x' % t
        elif TEXT_LO <= t < TEXT_HI:
            f = 'f_%x' % t
            self.callees.add(t)
        else:
            f = 'f_%x' % t
        if tail: return 'TAILCALL(%s);' % f
        return 'CALL(%s, 0x%xu);' % (f, nxt)

    # ---- VFP / NEON ----
    def sreg(self, ins, op):
        n = regname(ins, op.reg)
        if is_s(n): return 's[%s]' % n[1:]
        if is_d(n): return 's[%d]' % (int(n[1:]) * 2)      # lane 0
        raise Unsupported('sreg ' + n)

    def dlane(self, ins, op, lane):
        n = regname(ins, op.reg)
        return 's[%d]' % (int(n[1:]) * 2 + lane)

    def vfp(self, ins, mn, ops, st):
        base, _, ty = mn.partition('.')
        # vldr / vstr
        if base in ('vldr', 'vstr'):
            rn = regname(ins, ops[0].reg)
            memop = ops[1]
            res = self.mem_addr(ins, memop)
            a = res[0]
            if is_s(rn):
                if base == 'vldr':
                    if memop.mem.base == ARM_REG_PC:
                        addr = (ins.address + 8 + memop.mem.disp) & 0xffffffff
                        st.append('s[%s] = F32_BITS(0x%xu);' % (rn[1:], rd32(addr)))
                    else: st.append('s[%s] = LDF(%s);' % (rn[1:], a))
                else: st.append('STF(%s, s[%s]);' % (a, rn[1:]))
            elif is_d(rn):
                k = int(rn[1:]) * 2
                if base == 'vldr':
                    if memop.mem.base == ARM_REG_PC:
                        addr = (ins.address + 8 + memop.mem.disp) & 0xffffffff
                        st.append('s[%d] = F32_BITS(0x%xu); s[%d] = F32_BITS(0x%xu);' % (k, rd32(addr), k + 1, rd32(addr + 4)))
                    else: st.append('t0 = %s; s[%d] = LDF(t0); s[%d] = LDF(t0 + 4);' % (a, k, k + 1))
                else: st.append('t0 = %s; STF(t0, s[%d]); STF(t0 + 4, s[%d]);' % (a, k, k + 1))
            else: raise Unsupported('vldr reg ' + rn)
            return self.cond_wrap(ins, st)
        if base in ('vpush', 'vpop', 'vstmdb', 'vldmia', 'vstmia', 'vldm', 'vstm', 'vldmdb'):
            return self.vblock(ins, base, ops, st)
        if base == 'vmov':
            return self.vmov(ins, ty, ops, st)
        if base == 'vmrs':
            if regname(ins, ops[0].reg) in ('apsr_nzcv', 'apsr', 'pc') or ins.op_str.startswith('apsr_nzcv'):
                st.append('n = fn; z = fz; cf = fc; v = fv;')
            else:
                st.append('%s = 0;' % regname(ins, ops[0].reg))
            return self.cond_wrap(ins, st)
        if base == 'vmsr': return
        is64 = ty in ('f64',)
        if base in ('vadd', 'vsub', 'vmul', 'vdiv'):
            op = {'vadd': '+', 'vsub': '-', 'vmul': '*', 'vdiv': '/'}[base]
            if is64:
                d = self.dval(ins, ops[0], True); x = self.dget(ins, ops[1]); y = self.dget(ins, ops[2])
                st.append('{ double x_ = %s, y_ = %s; double r_ = x_ %s y_; %s }' % (x, y, op, self.dset(ins, ops[0], 'r_')))
            else:
                d = regname(ins, ops[0].reg)
                if is_d(d):    # NEON 2-lane
                    for lane in (0, 1):
                        st.append('%s = %s %s %s;' % (self.dlane(ins, ops[0], lane), self.dlane(ins, ops[1], lane), op, self.dlane(ins, ops[2], lane)))
                elif is_q(d): raise Unsupported('q reg')
                else:
                    st.append('%s = %s %s %s;' % (self.sreg(ins, ops[0]), self.sreg(ins, ops[1]), op, self.sreg(ins, ops[2])))
            return self.cond_wrap(ins, st)
        if base in ('vmla', 'vmls', 'vnmla', 'vnmls', 'vnmul', 'vfma', 'vfms', 'vfnma', 'vfnms'):
            if is64:
                x = self.dget(ins, ops[1]); y = self.dget(ins, ops[2]); dd = self.dget(ins, ops[0])
                e = {'vmla': 'd_ + x_ * y_', 'vmls': 'd_ - x_ * y_', 'vnmla': '-d_ - x_ * y_', 'vnmls': '-d_ + x_ * y_', 'vnmul': '-(x_ * y_)'}[base]
                st.append('{ double d_ = %s, x_ = %s, y_ = %s; double r_ = %s; %s }' % (dd, x, y, e, self.dset(ins, ops[0], 'r_')))
            else:
                regs = [self.sreg(ins, o) for o in ops[:3]]
                dstn = regname(ins, ops[0].reg)
                lanes = (0, 1) if is_d(dstn) else (None,)
                for lane in lanes:
                    if lane is None: d_, x_, y_ = regs
                    else: d_, x_, y_ = [self.dlane(ins, o, lane) for o in ops[:3]]
                    e = {'vmla': '%s + %s * %s', 'vmls': '%s - %s * %s', 'vnmla': '-%s - %s * %s', 'vnmls': '-%s + %s * %s', 'vnmul': '-(%s * %s)'}.get(base)
                    if base == 'vnmul': e = '-(%s * %s)' % (x_, y_)
                    elif base in ('vfma', 'vfnma', 'vfms', 'vfnms'): raise Unsupported('fma')
                    else: e = e % (d_, x_, y_)
                    st.append('%s = (float)(%s);' % (d_, e))
            return self.cond_wrap(ins, st)
        if base in ('vabs', 'vneg', 'vsqrt'):
            fn = {'vabs': 'fabsf', 'vneg': '-', 'vsqrt': 'sqrtf'}[base]
            if is64:
                st.append('{ double x_ = %s; double r_ = %s; %s }' % (self.dget(ins, ops[1]), {'vabs': 'fabs(x_)', 'vneg': '-x_', 'vsqrt': 'sqrt(x_)'}[base], self.dset(ins, ops[0], 'r_')))
            else:
                dn = regname(ins, ops[0].reg)
                lanes = (0, 1) if is_d(dn) else (None,)
                for lane in lanes:
                    if lane is None: d_, x_ = self.sreg(ins, ops[0]), self.sreg(ins, ops[1])
                    else: d_, x_ = self.dlane(ins, ops[0], lane), self.dlane(ins, ops[1], lane)
                    st.append('%s = %s;' % (d_, ('-%s' % x_) if base == 'vneg' else '%s(%s)' % (fn, x_)))
            return self.cond_wrap(ins, st)
        if base in ('vcmp', 'vcmpe'):
            if is64:
                x = self.dget(ins, ops[0])
                y = self.dget(ins, ops[1]) if ops[1].type == ARM_OP_REG else '0.0'
                st.append('{ double x_ = %s, y_ = %s; FCMP(x_, y_); }' % (x, y))
            else:
                x = self.sreg(ins, ops[0])
                y = self.sreg(ins, ops[1]) if ops[1].type == ARM_OP_REG else '0.0f'
                st.append('{ float x_ = %s, y_ = %s; FCMP(x_, y_); }' % (x, y))
            return self.cond_wrap(ins, st)
        if base == 'vcvt' or base == 'vcvtr':
            return self.vcvt(ins, ty, ops, st)
        if base in ('vmax', 'vmin'):
            raise Unsupported('vmax/vmin')
        raise Unsupported('vfp ' + mn)

    def dget(self, ins, op):
        n = regname(ins, op.reg)
        if is_d(n): k = int(n[1:]) * 2; return 'D2F(s[%d], s[%d])' % (k, k + 1)
        if is_s(n): return '(double)s[%s]' % n[1:]
        raise Unsupported('dget')

    def dset(self, ins, op, expr):
        n = regname(ins, op.reg)
        k = int(n[1:]) * 2
        return 'F2D(%s, &s[%d], &s[%d]);' % (expr, k, k + 1)

    def dval(self, ins, op, w): return None

    def vmov(self, ins, ty, ops, st):
        if len(ops) == 2:
            a, b = ops
            an = regname(ins, a.reg) if a.type == ARM_OP_REG else None
            if b.type == ARM_OP_FP:
                # immediate
                if is_s(an):
                    st.append('s[%s] = %sf;' % (an[1:], repr(float(b.fp))))
                else:
                    k = int(an[1:]) * 2
                    if ty == 'f64':
                        st.append('F2D(%s, &s[%d], &s[%d]);' % (repr(float(b.fp)), k, k + 1))
                    else:
                        st.append('s[%d] = %sf; s[%d] = %sf;' % (k, repr(float(b.fp)), k + 1, repr(float(b.fp))))
                return self.cond_wrap(ins, st)
            bn = regname(ins, b.reg)
            if is_s(an) and not is_s(bn) and not is_d(bn):          # core -> s
                st.append('s[%s] = F32_BITS(%s);' % (an[1:], self.R(ins, b)))
            elif not is_s(an) and not is_d(an) and is_s(bn):       # s -> core
                st.append('%s = BITS_F32(s[%s]);' % (an, bn[1:]))
            elif is_s(an) and is_s(bn):
                st.append('s[%s] = s[%s];' % (an[1:], bn[1:]))
            elif is_d(an) and is_d(bn):
                ka, kb = int(an[1:]) * 2, int(bn[1:]) * 2
                st.append('s[%d] = s[%d]; s[%d] = s[%d];' % (ka, kb, ka + 1, kb + 1))
            else: raise Unsupported('vmov 2 ' + an + ' ' + bn)
            return self.cond_wrap(ins, st)
        if len(ops) == 3:
            # vmov rt, rt2, dm  | vmov dm, rt, rt2 | vmov rt, rt2, sm, sm1
            a, b, c_ = ops
            an = regname(ins, a.reg) if a.type == ARM_OP_REG else None
            if is_d(an):
                k = int(an[1:]) * 2
                st.append('s[%d] = F32_BITS(%s); s[%d] = F32_BITS(%s);' % (k, self.R(ins, b), k + 1, self.R(ins, c_)))
            else:
                cn = regname(ins, c_.reg)
                if is_d(cn):
                    k = int(cn[1:]) * 2
                    st.append('%s = BITS_F32(s[%d]); %s = BITS_F32(s[%d]);' % (an, k, regname(ins, b.reg), k + 1))
                else: raise Unsupported('vmov 3')
            return self.cond_wrap(ins, st)
        if len(ops) == 4:
            raise Unsupported('vmov 4')
        raise Unsupported('vmov')

    def vblock(self, ins, base, ops, st):
        regs = [regname(ins, o.reg) for o in ops if o.type == ARM_OP_REG]
        if base in ('vpush', 'vpop'):
            bp = 'sp'; lst = regs
        else:
            bp = regs[0]; lst = regs[1:]
        words = []
        for r in lst:
            if is_d(r): k = int(r[1:]) * 2; words += [k, k + 1]
            elif is_s(r): words.append(int(r[1:]))
            else: raise Unsupported('vblock reg')
        n = len(words)
        if base in ('vpush', 'vstmdb'):
            st.append('%s -= %d;' % (bp, 4 * n))
            for i, w in enumerate(words): st.append('STF(%s + %d, s[%d]);' % (bp, 4 * i, w))
        elif base in ('vpop', 'vldmia', 'vldm'):
            for i, w in enumerate(words): st.append('s[%d] = LDF(%s + %d);' % (w, bp, 4 * i))
            if base == 'vpop' or ins.writeback: st.append('%s += %d;' % (bp, 4 * n))
        elif base in ('vstmia', 'vstm'):
            for i, w in enumerate(words): st.append('STF(%s + %d, s[%d]);' % (bp, 4 * i, w))
            if ins.writeback: st.append('%s += %d;' % (bp, 4 * n))
        else: raise Unsupported(base)
        self.cond_wrap(ins, st)

    def vcvt(self, ins, ty, ops, st):
        # ty like f32.s32, s32.f32, f64.s32, f32.f64, f64.f32, s32.f64, f32.u32, u32.f32
        parts = ty.split('.')
        dty, sty = parts[0], parts[1]
        d, s_ = ops[0], ops[1]
        def getsrc():
            if sty in ('s32', 'u32'):
                return 'BITS_F32(%s)' % self.sreg(ins, s_)           # raw int in an s register
            if sty == 'f32': return self.sreg(ins, s_)
            if sty == 'f64': return self.dget(ins, s_)
            raise Unsupported('vcvt src ' + sty)
        src = getsrc()
        if dty == 'f32':
            if sty == 's32': st.append('%s = (float)(int32_t)%s;' % (self.sreg(ins, d), src))
            elif sty == 'u32': st.append('%s = (float)(uint32_t)%s;' % (self.sreg(ins, d), src))
            elif sty == 'f64': st.append('%s = (float)%s;' % (self.sreg(ins, d), src))
            else: raise Unsupported('vcvt')
        elif dty == 'f64':
            if sty == 's32': st.append('{ double r_ = (double)(int32_t)%s; %s }' % (src, self.dset(ins, d, 'r_')))
            elif sty == 'u32': st.append('{ double r_ = (double)(uint32_t)%s; %s }' % (src, self.dset(ins, d, 'r_')))
            elif sty == 'f32': st.append('{ double r_ = (double)%s; %s }' % (src, self.dset(ins, d, 'r_')))
            else: raise Unsupported('vcvt')
        elif dty == 's32':
            st.append('%s = F32_BITS(CVT_S32((double)(%s)));' % (self.sreg(ins, d), src if sty != 'f32' else src))
        elif dty == 'u32':
            st.append('%s = F32_BITS(CVT_U32((double)(%s)));' % (self.sreg(ins, d), src))
        else: raise Unsupported('vcvt dst ' + dty)
        self.cond_wrap(ins, st)


def gen_function(name, start, end):
    g = FuncGen(name, start, end)
    body = g.build()
    return body, g


if __name__ == '__main__':
    n = sys.argv[1]
    a = [x for x in syms if x[1] == n][0][0]
    body, g = gen_function(n, a, func_end(a))
    print(body)
