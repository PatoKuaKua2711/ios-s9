#!/bin/sh
# CPU per process on the phone's iOS over N seconds (no `top` there):
# difference of the accumulated CPU time from ps.  tools/s9-cpu.sh [SECONDS]
cd "$(dirname "$0")/.."
n=${1:-20}
tools/s9ssh.sh "ps -axo pid=,cputime=,comm= > /tmp/a; sleep $n; ps -axo pid=,cputime=,comm= > /tmp/b; cat /tmp/a; echo SEP; cat /tmp/b" |
python3 -c '
import sys
n=float(sys.argv[1])
def secs(t):
    p=t.replace("-",":").split(":"); s=0.0
    for x in p: s=s*60+float(x)
    return s
a={};b={};cur=a
for l in sys.stdin:
    if l.startswith("SEP"): cur=b; continue
    f=l.split(None,2)
    if len(f)==3: cur[f[0]]=(secs(f[1]),f[2].strip())
rows=[(b[p][0]-a.get(p,(0,))[0],p,b[p][1]) for p in b]
rows.sort(reverse=True); tot=sum(r[0] for r in rows)
print("total %.1f%% de 1 nucleo (8 nucleos = 800%%) em %.0fs"%(100*tot/n,n))
for d,p,c in rows[:15]:
    if d>0: print("%6.1f%%  %5s  %s"%(100*d/n,p,c.split("/")[-1]))
' "$n"
