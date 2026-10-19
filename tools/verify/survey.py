#!/usr/bin/env python3
"""Plays every single-player level headlessly (menu taps -> race -> hold right) and collects frames + warnings.
usage: survey.py <gl_boot> <PR3.armv7> <resource_dir> <outdir> [first] [last] [jobs]"""
import sys, os, subprocess, json, shutil, concurrent.futures, re
gl_boot, binary, res, out = sys.argv[1:5]
first = int(sys.argv[5]) if len(sys.argv) > 5 else 0
last = int(sys.argv[6]) if len(sys.argv) > 6 else 49
jobs = int(sys.argv[7]) if len(sys.argv) > 7 else 4
levels = json.load(open(os.path.join(res, 'LevelList.dat')))
ROWS = [120, 200, 280]
SAMPLE = [150, 300, 450, 600, 750]

def run(i):
    d = os.path.join(out, 'L%02d' % i); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    page, row = divmod(i, 3)
    unlocked = bool(os.environ.get('UNLOCKED'))       # main branch: pick the 'All Unlocked' tile (second in the row) so every level can be opened
    taps = ['400:240:160', '620:240:195' if unlocked else '480:240:195']
    t = 700 if unlocked else 560
    for _ in range(page): taps.append('%d:459:66' % t); t += 20
    taps.append('%d:330:%d' % (t + 20, ROWS[row]))
    start = t + 20
    frames = start + SAMPLE[-1] + 1
    keep = [start + x for x in SAMPLE]
    env = dict(os.environ, SAVE_FRAMES=','.join(map(str, keep)), PR3_RES=res, PR3_OUT=d, PR3_DOCS=os.path.join(d, 'docs'), TAPS=','.join(taps), **({'DRAGS': '560:580:360:200:230:200'} if unlocked else {}), PR3_COVERAGE=os.path.join(d, 'coverage.txt'),
               HOLDS='%d:%d:100:236' % (start + 260, frames - 1))
    os.makedirs(env['PR3_DOCS'])
    p = subprocess.run([gl_boot, binary, str(frames), '100000'], env=env, capture_output=True, text=True)
    log = p.stdout + p.stderr
    open(os.path.join(d, 'log.txt'), 'w').write(log)
    keep = {start + s for s in SAMPLE}
    for f in os.listdir(d):
        m = re.match(r'frame_(\d+)\.ppm', f)
        if m and int(m.group(1)) not in keep: os.remove(os.path.join(d, f))
    return i, p.returncode, start

with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
    for i, rc, start in ex.map(run, range(first, last + 1)):
        log = open(os.path.join(out, 'L%02d' % i, 'log.txt')).read()
        bad = [l for l in log.splitlines() if re.search(r'unknown|bad guest|UNIMPL|nhandled|FAILED|segmentation|abort|Assertion', l) and 'double free' not in l and 'UNCOMPRESSION FAILED' not in l]   # the game's own fallback when block data is not compressed
        print('%02d %-28s rc=%d start=%d warnings=%d %s' % (i, levels[i]['title'][:28], rc, start, len(bad), '; '.join(sorted(set(bad))[:3])[:200]), flush=True)
