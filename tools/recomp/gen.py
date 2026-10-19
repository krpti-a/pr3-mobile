#!/usr/bin/env python3
"""Drives the recompiler: computes the closure of functions reachable from ROOTS and writes C files.
Usage: gen.py <config.json> <out_dir>"""
import sys, os, json, re
sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
import recomp
from recomp import *

def mangle(name):
    return re.sub(r'[^A-Za-z0-9_]', '_', name)

def main():
    cfg = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    by_name = {}
    for a, s in recomp.syms: by_name.setdefault(s, a)
    overrides = {}      # addr -> C symbol
    for name, csym in cfg.get('overrides', {}).items():
        overrides[by_name[name]] = csym
    wraps = {}          # addr -> C symbol: the original is kept as f_<addr>_orig and f_<addr> calls the wrapper
    for name, csym in cfg.get('wraps', {}).items():
        wraps[by_name[name]] = csym
    roots = []
    for r in cfg['roots']:
        if r == '*':
            roots += [a for a, s in recomp.syms if recomp.TEXT_LO <= a < recomp.TEXT_HI]
        elif r.endswith('*'):
            roots += [a for a, s in recomp.syms if s.startswith(r[:-1]) and recomp.TEXT_LO <= a < recomp.TEXT_HI]
        else:
            roots.append(by_name[r])
    todo = list(dict.fromkeys(roots)); done = {}
    skipnames = tuple(cfg.get('skip_prefixes', []))
    refs = set()
    unsupported = {}
    while todo:
        a = todo.pop()
        if a in done or a in overrides: continue
        if not (recomp.TEXT_LO <= a < recomp.TEXT_HI): continue
        name = recomp.sym_name.get(a, 'sub_%x' % a)
        if skipnames and name.startswith(skipnames):
            refs.add(a); continue
        end = recomp.func_end(a)
        body, g = recomp.gen_function(name, a, end)
        if a in wraps: body = body.replace('void f_%x(CPU *c)' % a, 'void f_%x_orig(CPU *c)' % a, 1)
        done[a] = body
        for m in re.findall(r'UNIMPL\("([^"]*)"\)', body): unsupported.setdefault(a, []).append(m)
        for t in g.callees:
            if t not in done: todo.append(t)
        refs |= set(g.callees)
    # references to functions we did not generate
    missing = sorted(x for x in re.findall(r'\bf_([0-9a-f]+)\b', ''.join(done.values())) if int(x, 16) not in done)
    missing = sorted({int(x, 16) for x in missing} - set(done))
    names = {a: recomp.sym_name.get(a, 'sub_%x' % a) for a in set(done) | set(missing) | set(overrides)}
    stub_addrs = sorted(recomp.stubs)
    # ---- write files ----
    items = sorted(done.items())
    chunk = cfg.get('chunk', 40)
    files = []
    for i in range(0, len(items), chunk):
        fn = 'gen_%03d.c' % (i // chunk)
        with open(os.path.join(out, fn), 'w') as f:
            f.write('#include "gen_decls.h"\n\n')
            for a, body in items[i:i + chunk]: f.write(body + '\n\n')
        files.append(fn)
    with open(os.path.join(out, 'gen_decls.h'), 'w') as f:
        f.write('#ifndef GEN_DECLS_H\n#define GEN_DECLS_H\n#include "arm_rt.h"\n')
        allf = sorted(set(done) | set(missing) | set(overrides))
        for a in allf: f.write('void f_%x(CPU *c);\n' % a)
        for a in wraps: f.write('void f_%x_orig(CPU *c);\n' % a)
        for a in stub_addrs: f.write('void stub_%x(CPU *c);\n' % a)
        f.write('#endif\n')
    with open(os.path.join(out, 'gen_stubs.c'), 'w') as f:
        f.write('#include "gen_decls.h"\nvoid rt_unhandled_import(const char *name, CPU *c);\n')
        seen = set()
        for a in stub_addrs:
            nm = recomp.stubs[a]
            m = mangle(nm)
            if m not in seen:
                seen.add(m)
                f.write('__attribute__((weak)) void hle_%s(CPU *c) { rt_unhandled_import("%s", c); }\n' % (m, nm))
            f.write('void stub_%x(CPU *c) { hle_%s(c); }\n' % (a, m))
        for a, cs in sorted(wraps.items()):
            f.write('__attribute__((weak)) void %s(CPU *c) { f_%x_orig(c); }\n' % (cs, a))
            f.write('void f_%x(CPU *c) { %s(c); }  /* wrapper */\n' % (a, cs))
        osyms = set()
        for a in sorted(set(missing) | set(overrides)):
            nm = names.get(a, 'sub_%x' % a)
            if a in overrides:
                cs = overrides[a]
                if cs not in osyms:
                    osyms.add(cs)
                    f.write('__attribute__((weak)) void %s(CPU *c) { rt_missing(0x%x); }\n' % (cs, a))
                f.write('void f_%x(CPU *c) { %s(c); }  /* override: %s */\n' % (a, cs, nm.replace('*/', '')))
            else:
                f.write('void f_%x(CPU *c) { rt_missing(0x%x); }  /* %s */\n' % (a, a, nm.replace('*/', '')))
    with open(os.path.join(out, 'gen_table.c'), 'w') as f:
        f.write('#include "gen_decls.h"\nconst rt_entry rt_table[] = {\n')
        ents = sorted(set(done) | set(missing) | set(overrides))
        for a in ents: f.write('  {0x%x, f_%x},\n' % (a, a))
        for a in stub_addrs: f.write('  {0x%x, stub_%x},\n' % (a, a))
        f.write('};\nconst uint32_t rt_table_count = %d;\n' % (len(ents) + len(stub_addrs)))
        f.write('const rt_stub_name rt_stub_names[] = {\n')
        for a in stub_addrs: f.write('  {0x%x, "%s"},\n' % (a, recomp.stubs[a]))
        f.write('};\nconst uint32_t rt_stub_name_count = %d;\n' % len(stub_addrs))
    # dyld binds (external data symbols the original expected dyld to fill in)
    bj = os.path.join(os.path.dirname(__file__), 'binds.json')
    if os.path.exists(bj):
        binds = json.load(open(bj))['bind']
        with open(os.path.join(out, 'gen_binds.c'), 'w') as f:
            f.write('#include "arm_rt.h"\nconst rt_bind rt_binds[] = {\n')
            for addr, sym, typ, addend in binds: f.write('  {0x%x, "%s", %d},\n' % (addr, sym, addend))
            f.write('};\nconst uint32_t rt_bind_count = %d;\n' % len(binds))
    with open(os.path.join(out, 'gen_names.txt'), 'w') as f:
        for a in sorted(names): f.write('%x %s\n' % (a, names[a]))
    print('generated', len(done), 'functions;', len(missing), 'missing refs;', len(unsupported), 'functions with UNIMPL')
    for a, l in list(unsupported.items())[:20]: print('  UNIMPL in', names[a], l[:3])
    print('files:', ' '.join(files))

main()
