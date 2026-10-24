#!/usr/bin/env python3
"""Symbolicate a spindump taken on the phone's iOS (spindump -file X).

  spin-sym.py SPINDUMP [PROCESS ...] > out.txt

Only the named processes (all if none) are kept. Frames inside the dyld
shared cache ("??? (UIKitCore + 123) [0x1a...]") get their symbol from the
iOS 16.7.16 cache of the IPSW through `ipsw dyld a2s`, with the slide the
spindump header reports. Needs DSC (env) = the cache's main file, which comes
from the IPSW's SystemOS dmg (see NOTES.md), and work/dsc/a2s.cache.
Frames with fewer than MINCOUNT samples (env, default 3) are dropped.
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
DSC = os.environ.get('DSC', '')
CACHE = os.path.join(HERE, '..', '..', 'work', 'dsc', 'a2s.cache')
MINCOUNT = int(os.environ.get('MINCOUNT', '3'))

FRAME = re.compile(r'^(\s*\*?(\d+)\s+)\?\?\? \((.+?) \+ (\d+)\) \[(0x[0-9a-f]+)\](.*)$')


def a2s(slide, addr):
    r = subprocess.run(['ipsw', 'dyld', 'a2s', '--cache', CACHE, '-s', hex(slide), DSC, addr],
                       capture_output=True, text=True)
    out = [l for l in r.stdout.splitlines() if l.startswith('0x')]
    return out[-1].split(': ', 1)[1] if out else None


def main():
    txt = open(sys.argv[1], errors='replace').read()
    want = set(sys.argv[2:])
    m = re.search(r'slide (0x[0-9a-f]+)', txt)
    slide = int(m.group(1), 16) if m else 0
    blocks = re.split(r'\n(?=Process:\s)', txt)
    keep = []
    for b in blocks:
        p = re.match(r'Process:\s+(.+?) \[', b)
        if p and (not want or p.group(1) in want):
            keep.append(b)
    lines = [l for b in keep for l in b.split('\n')]
    addrs = set()
    for l in lines:
        f = FRAME.match(l)
        if f and int(f.group(2)) >= MINCOUNT and int(f.group(5), 16) < 0xfffffe0000000000:
            addrs.add(f.group(5))
    with ThreadPoolExecutor(8) as ex:
        syms = dict(zip(addrs, ex.map(lambda a: a2s(slide, a), addrs)))
    for l in lines:
        f = FRAME.match(l)
        if f:
            if int(f.group(2)) < MINCOUNT:
                continue
            s = syms.get(f.group(5))
            if s:
                l = '%s%s  (%s)%s' % (f.group(1), s, f.group(3), f.group(6))
        print(l)


if __name__ == '__main__':
    main()
