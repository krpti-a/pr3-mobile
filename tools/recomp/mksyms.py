#!/usr/bin/env python3
"""Writes a sorted '<hex addr> <demangled name>' list of the code symbols of a thin armv7 Mach-O. usage: mksyms.py <bin> <out>"""
import subprocess, sys, re
binary, out = sys.argv[1], sys.argv[2]
nm = subprocess.run(['nm', '-arch', 'armv7', binary], capture_output=True, text=True).stdout
rows = []
for l in nm.splitlines():
    m = re.match(r'^([0-9a-f]{8}) ([TtSsDd]) (.+)$', l)
    if m and m.group(2) in 'Tt': rows.append((int(m.group(1), 16), m.group(3)))
names = [n[1:] if n.startswith('_') else n for _, n in rows]
dem = subprocess.run(['c++filt'], input='\n'.join(names), capture_output=True, text=True).stdout.split('\n')
res = sorted((a, d if d else n) for (a, _), n, d in zip(rows, names, dem))
with open(out, 'w') as f:
    for a, n in res: f.write('%08x %s\n' % (a, n))
print(len(res), 'symbols')
