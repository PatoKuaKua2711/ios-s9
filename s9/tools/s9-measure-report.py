#!/usr/bin/env python3
"""Summary of one tools/s9-measure.sh trip, as Markdown, for the decision table
of the study (ESTUDO section 5).

  s9-measure-report.py logs/measure-DATE [> report.md]

Reads bench-*.txt (s9-bench.sh), top-*.txt (s9top -a), freq-*.txt
(kern.sweet_cpu_freq samples), membw.txt (s9membw), batt-start/end.txt
(s9batt), fps-lines.txt (kernel "present avg" lines), ca-*.log
(CoreAnimation's own log). Missing files are skipped.
"""
import glob
import os
import re
import statistics
import sys


def read(path):
    try:
        with open(path, errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def bench(text):
    """median fps, max fps, active seconds, present us/frame, per-second swaps."""
    m = re.search(r'swaps/s: median (\d+), max (\d+), over (\d+) active seconds', text)
    p = re.search(r'present ([\d.]+) us/frame', text)
    per = [int(x) for x in re.findall(r'^\s+(\d+)\s+\d+\s+\d+\s*$', text, re.M)]
    if not m:
        return None
    return {'median': int(m.group(1)), 'max': int(m.group(2)), 'secs': int(m.group(3)),
            'present_us': float(p.group(1)) if p else None, 'per': per}


def top(text):
    """Averages over the samples: core busy, interrupts, top processes and threads."""
    samples = text.split('--- ')[1:]
    a55, a75, ipi, irq = [], [], [], []
    procs, threads = {}, {}
    for s in samples:
        m = re.search(r'\(A55 (\d+)%, A75 (\d+)% of 400\)', s)
        if m:
            a55.append(int(m.group(1)))
            a75.append(int(m.group(2)))
        for key, acc in (('ipi', ipi), ('irq', irq)):
            m = re.search(r'^\s+%s/s:(.*)$' % key, s, re.M)
            if m:
                acc.append(sum(float(x) for x in m.group(1).replace('|', ' ').split()))
        for m in re.finditer(r'^\s+([\d.]+)%\s+(\S+)\s+\[(\d+)\]\s+csw/s (\d+)\s+syscalls/s (\d+)', s, re.M):
            procs.setdefault(m.group(2), []).append((float(m.group(1)), int(m.group(4)), int(m.group(5))))
        current = None
        for line in s.split('\n'):
            m = re.match(r'^\s\s(\S+) \[(\d+)\]: ([\d.]+)% of one core', line)
            if m:
                current = m.group(1)
                continue
            m = re.match(r'^\s+([\d.]+)%\s+pri\s+\d+/\s*\d+\s+0x[0-9a-f]+ (.*)$', line)
            if m and current:
                threads.setdefault((current, m.group(2).strip()), []).append(float(m.group(1)))
    n = max(1, len(samples))
    avg = lambda v: sum(v) / len(v) if v else 0.0
    top_procs = sorted(((sum(x[0] for x in v) / n, k, avg([x[1] for x in v]), avg([x[2] for x in v]))
                        for k, v in procs.items()), reverse=True)[:6]
    top_threads = sorted(((sum(v) / n, p, t) for (p, t), v in threads.items()), reverse=True)[:8]
    return {'a55': avg(a55), 'a75': avg(a75), 'ipi': avg(ipi), 'irq': avg(irq), 'procs': top_procs,
            'threads': top_threads, 'samples': len(samples)}


def freq(text):
    gold = [int(x) for x in re.findall(r'gold (\d+) MHz', text)]
    silver = [int(x) for x in re.findall(r'silver (\d+) MHz', text)]
    l3 = [int(x) for x in re.findall(r'l3 (\d+) MHz', text)]
    if not gold:
        return None
    return {'gold_min': min(gold), 'gold_avg': sum(gold) / len(gold), 'silver_min': min(silver) if silver else 0,
            'l3_min': min(l3) if l3 else 0}


def battery(text):
    m = re.search(r'kern\.sweet_battery: (.*)', text)
    return m.group(1).strip() if m else ''


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    out = []
    say = out.append
    say('# Medição %s\n' % os.path.basename(os.path.abspath(d)))

    names = sorted(os.path.basename(p)[6:-4] for p in glob.glob(os.path.join(d, 'bench-*.txt')))
    order = ['base', 'flush1', 'flush0', 'par1', 'par2', 'par4', 'caperf', 'vbl']
    names = [n for n in order if n in names] + [n for n in names if n not in order]
    rows = {}
    if names:
        say('## Rolagem (Ajustes, dedo virtual)\n')
        say('| teste | fps mediana | fps máx | apresentação µs | A75 % (de 400) | A55 % | IPI/s | gold MHz mín/média |')
        say('|---|---|---|---|---|---|---|---|')
        for n in names:
            b = bench(read(os.path.join(d, 'bench-%s.txt' % n)))
            t = top(read(os.path.join(d, 'top-%s.txt' % n)))
            f = freq(read(os.path.join(d, 'freq-%s.txt' % n)))
            rows[n] = (b, t, f)
            if not b:
                say('| %s | (sem dados) | | | | | | |' % n)
                continue
            say('| %s | %d | %d | %s | %.0f | %.0f | %.0f | %s |' % (
                n, b['median'], b['max'], '%.0f' % b['present_us'] if b['present_us'] is not None else '-',
                t['a75'], t['a55'], t['ipi'], '%d/%.0f' % (f['gold_min'], f['gold_avg']) if f else '-'))
        say('')
        b, t, f = rows.get('base', (None, None, None))
        if t and t['threads']:
            say('### Threads mais ocupadas na rolagem (teste base, % de um núcleo)\n')
            for pct, proc, th in t['threads']:
                say('- %.0f%%  %s / %s' % (pct, proc, th))
            say('')
            say('### Processos mais ocupados (teste base)\n')
            for pct, proc, csw, sysc in t['procs']:
                say('- %.0f%%  %s  (trocas de contexto %.0f/s, syscalls %.0f/s)' % (pct, proc, csw, sysc))
            say('')

    t = top(read(os.path.join(d, 'top-idle.txt')))
    if t['samples']:
        say('## Parado nos Ajustes\n')
        say('A75 %.0f %%, A55 %.0f %% (de 400), IPI %.0f/s, interrupções %.0f/s' % (t['a75'], t['a55'], t['ipi'], t['irq']))
        for pct, proc, csw, sysc in t['procs']:
            say('- %.0f%%  %s  (trocas de contexto %.0f/s, syscalls %.0f/s)' % (pct, proc, csw, sysc))
        say('')
    fps = read(os.path.join(d, 'fps-lines.txt'))
    tops = re.findall(r'top (\d+,\d+ \d+x\d+) x(\d+)', fps)
    if tops:
        say('Retângulo sujo mais frequente nas linhas de fps do kernel: ' +
            ', '.join('%s (x%s)' % t for t in tops[-5:]) + '\n')

    mem = read(os.path.join(d, 'membw.txt'))
    if mem:
        say('## Memória (s9membw)\n')
        say('```')
        say(mem.strip())
        say('```\n')
        m = re.search(r'^\s+1\s+[\d.]+\s+([\d.]+)', mem, re.M)
        if m and float(m.group(1)) < 5:
            say('**Cópia com 1 thread abaixo de 5 GB/s: memória lenta (ver voto de banda, só com o OK do dono).**\n')

    bs, be = battery(read(os.path.join(d, 'batt-start.txt'))), battery(read(os.path.join(d, 'batt-end.txt')))
    if bs or be:
        say('## Bateria\n')
        say('- início: `%s`' % bs)
        say('- fim: `%s`' % be)
        s1 = re.search(r'soc=([\d.]+)%', bs)
        s2 = re.search(r'soc=([\d.]+)%', be)
        if s1 and s2:
            say('- variação durante a medição: %+.1f pontos' % (float(s2.group(1)) - float(s1.group(1))))
        say('')

    for name in ('ca-perf.log', 'ca-vbl.log', 'ca-anim.log'):
        text = read(os.path.join(d, name))
        if text:
            lines = [l for l in text.split('\n') if l.strip()]
            say('## %s (%d linhas; as últimas 8)\n' % (name, len(lines)))
            say('```')
            say('\n'.join(lines[-8:]))
            say('```\n')

    # Hints for the decision table (ESTUDO section 5).
    say('## Pistas para a tabela de decisão\n')
    base = rows.get('base', (None, None, None))
    if base[0]:
        for n, label in (('flush1', 'esvaziar o L1 de novo'), ('par1', 'dispatch_apply sem ajudantes'),
                         ('par2', 'dispatch_apply com 2'), ('par4', 'dispatch_apply com 4'),
                         ('vbl', 'ritmo pelo vsync do kernel (CA_FORCE_TIMER=1)')):
            b = rows.get(n, (None,))[0]
            if b:
                diff = b['median'] - base[0]['median']
                say('- %s: %+d fps na mediana (%d → %d)' % (label, diff, base[0]['median'], b['median']))
    if base[2] and base[2]['gold_min'] < 2600:
        say('- o gold caiu a %d MHz durante a rolagem: **estrangulamento (LMh)**' % base[2]['gold_min'])
    if base[1] and base[1]['threads']:
        pct, proc, th = base[1]['threads'][0]
        if pct >= 85:
            say('- a thread mais ocupada (%s / %s) está em %.0f%% de um núcleo: **ela é o gargalo**' % (proc, th, pct))
    print('\n'.join(out))


if __name__ == '__main__':
    main()
