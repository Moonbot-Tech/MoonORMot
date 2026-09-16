"""Generic layout solver: python solve.py <routine> [-q] [--emit]   (routine = string | json | ...)
The routine module routine_<name>.py gives hot, cold, LOOPS, HOT/WARM/DEEP, HARD_LOOPS, LOOP64,
NOGAP. Dynamic programming tracks offset mod 64, DS prefixes
on non-jump instructions outside loops, never-executed gaps after jmp/ret; every jump, fused pair
and ret inside one 16-byte block and not ending on a block boundary; loops <= 32 B in one 32-byte
window, longer loops on a 64-byte offset)."""
import sys, importlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import plan_hot
from plan_hot import FUSE, J

def load(name):
    return importlib.import_module('routine_' + name)

def loop_members(R, seq):
    inside = set(); cur = None
    for idx, (text, size) in enumerate(seq):
        if text.endswith(':'):
            if text[:-1] in R.LOOPS: cur = text[:-1]
            continue
        if cur:
            inside.add(idx)
            if text.split()[0] in J and text.split()[-1] == cur: cur = None
    return inside

def region_weights(R, cold):
    w = {}; cur = 10.0; deep = False
    for idx, (text, size) in enumerate(cold):
        if text.endswith(':'):
            name = text[:-1]
            if name in R.DEEP: deep = True
            cur = 10.0 if name in R.HOT else (3.0 if name in R.WARM else (0.5 if deep else 1.0))
        w[idx] = cur
    return w

def solve(R, cold, start, sizes, gap_max=15, pad_max=2):
    weights = region_weights(R, cold)
    inside = loop_members(R, cold)
    nogap = {}; flag = False
    for idx, (text, size) in enumerate(cold):
        if R.NOGAP and text == R.NOGAP[0] + ':': flag = True
        if R.NOGAP and text == R.NOGAP[1] + ':': flag = False
        nogap[idx] = flag
    states = {(start % 64, None, None): (0.0, [])}
    for idx, (text, size) in enumerate(cold):
        if text.endswith(':'):
            name = text[:-1]
            new = {}
            for (off, fuse, loop), (cost, ch) in states.items():
                key = (off, None, 'pending' if name in R.LOOPS else loop)
                if key not in new or new[key][0] > cost: new[key] = (cost, ch + [('L', name, off)])
            states = new; continue
        mn = text.split()[0]
        alts = [(t.strip(), sz) for t, sz in zip(text.split('|'), size)] if isinstance(size, list) else [(text, sizes.get(idx, size))]
        isjump = mn in J
        new = {}
        for (off, fuse, loop), (cost, ch) in states.items():
            if idx in inside or isjump or mn in {'tzcnt', 'lzcnt', 'popcnt', 'crc32'}:
                choices = [(0, 0)]
            else:
                choices = [(p, 0) for p in range((pad_max if weights[idx] >= 3.0 else pad_max + 1) + 1)]
            p = idx - 1
            while p >= 0 and cold[p][0].endswith(':'): p -= 1
            if p >= 0 and (cold[p][0].startswith('jmp') or cold[p][0] == 'ret') and not nogap[idx]:
                choices = choices + [(0, g) for g in range(1, (31 if weights[idx] < 1.0 else gap_max) + 1)]
            for pads, gap in choices:
              for ai, (atext, asize) in enumerate(alts):
                o = (off + gap) % 64
                lp = o if loop == 'pending' else loop
                k = asize + pads
                c = cost + weights[idx] * pads + getattr(R, 'GAP_COST', 0.2) * gap + 0.01 * ai
                ok = True
                if isjump:
                    s0 = fuse if fuse is not None and mn not in ('jmp', 'ret') else o
                    e = o + k
                    s0u = s0 if s0 <= o else s0 - 64
                    if (s0u // 16) != ((e - 1) // 16) or e % 16 == 0: ok = False
                if not ok: continue
                nloop = lp
                if lp is not None and idx in inside and isjump and text.split()[-1] in R.LOOPS and text.split()[-1] == [c[1] for c in ch if c[0] == 'L' and c[1] in R.LOOPS][-1]:
                    e = o + k
                    eu = e if e > lp else e + 64
                    n = eu - lp
                    fits = (eu - 1) // 32 == lp // 32 if n <= 32 else (lp % 64 == R.LOOP64 and n <= 64)
                    if not fits:
                        if text.split()[-1] in R.HARD_LOOPS: continue
                        c += 100
                    nloop = None
                nfuse = o if (mn in FUSE) else None
                key = ((o + k) % 64, nfuse, nloop)
                rec = (c, ch + [('I', idx, pads, gap, o, k, atext, asize)])
                if key not in new or new[key][0] > c: new[key] = rec
        states = new
        if not states: raise RuntimeError('no layout at %d %s after %s' % (idx, text, [c[0] for c in cold[max(0, idx - 8):idx]]))
    return min(states.values(), key=lambda value: value[0])[1]

def apply(cold, choice):
    out = []
    for c in choice:
        if c[0] == 'L':
            out.append((c[1] + ':', 0)); continue
        _, idx, pads, gap, o, k, atext, asize = c
        if gap:
            j = len(out)
            while j > 0 and out[j - 1][0].endswith(':'): j -= 1
            out.insert(j, ('NOP%d' % gap, gap))
        out += [('PAD', 1)] * pads
        out.append((atext, asize))
    return out

def loops(R, seq, start=0):
    size_of, labels = plan_hot.sizes_with_targets(seq, start, getattr(R, 'SHORTEN', False))
    off = start; pend = 0; ends = {}; cur = None
    for idx, (text, size) in enumerate(seq):
        if text == 'PAD': pend += 1; continue
        if text.endswith(':'):
            if text[:-1] in R.LOOPS: cur = text[:-1]
            continue
        if idx in size_of: size = size_of[idx]
        size += pend; pend = 0
        if cur and text.split()[0] in J and text.split()[-1] == cur:
            ends[cur] = off + size; cur = None
        off += size
    for name, e in ends.items():
        s = labels[name]
        n = e - s
        ok = (s // 32) == ((e - 1) // 32) if n <= 32 else (s % 64 == R.LOOP64 and n <= 64)
        where = 'one 32-byte window' if n <= 32 else 'a 64-byte offset'
        print(f'loop {name:18s} {s:04x}..{e - 1:04x} {n:2d} B  start mod 64 = {s % 64:2d}  {where if ok else "<<< SPLIT"}')

def main(R, quiet=True, emit=False):
    plan_hot.FAR = set()
    cold = [x for x in R.hot + R.cold if x[0] != 'PAD' and not x[0].startswith('NOP')]
    first = [(t.split('|')[0].strip(), sz[0]) if isinstance(sz, list) else (t, sz) for t, sz in cold]
    size_of, labels = plan_hot.sizes_with_targets(first, shorten=getattr(R, 'SHORTEN', False))
    sizes = dict(size_of)
    for it in range(12):
        choice = solve(R, cold, 0, sizes)
        laid = apply(cold, choice)
        size_of2, _ = plan_hot.sizes_with_targets(laid, shorten=getattr(R, 'SHORTEN', False))
        base_idx = {}; j = 0
        for i, (t, s) in enumerate(laid):
            if t == 'PAD' or t.startswith('NOP'): continue
            base_idx[i] = j; j += 1
        new_sizes = {base_idx[i]: s for i, s in size_of2.items() if i in base_idx}
        diff = {i: (sizes.get(i), new_sizes.get(i)) for i in set(sizes) | set(new_sizes) if sizes.get(i) != new_sizes.get(i)}
        print('iteration', it, 'size changes', {cold[i][0]: v for i, v in diff.items()})
        if not diff: break
        sizes = new_sizes
    else:
        raise RuntimeError('branch sizes and padding did not converge: ' + R.NAME)
    plan_hot.show(laid, quiet=quiet, shorten=getattr(R, 'SHORTEN', False))
    loops(R, laid)
    pads = sum(1 for t, s in laid if t == 'PAD'); gaps = sum(s for t, s in laid if t.startswith('NOP'))
    print('pads', pads, 'gap bytes', gaps)
    if emit:
        for t, s in laid: print(repr((t, s)) + ',')
    return laid

if __name__ == '__main__':
    R = load(sys.argv[1])
    main(R, quiet='-q' in sys.argv, emit='--emit' in sys.argv)
