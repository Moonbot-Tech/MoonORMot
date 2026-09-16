"""Derive the SysV plan from the same instruction sheets using measured MoonCompiler encodings.

python layout.py measure dump-sysv.json encodings-sysv.json
python layout.py emit

The unpadded first SysV build supplies instruction sizes. Subsequent builds are checked against
the solved plan, including every instruction, prefix, dead gap, jump target and loop.
Requires capstone only on the machine running this checker, not on the Linux build host.
"""
import contextlib, io, json, re, sys
from types import SimpleNamespace
from pathlib import Path
import capstone
import emit, solve, plan_hot

HERE = Path(__file__).resolve().parent
NAMES = ('string', 'json', 'intb', 'intt', 'intc')
ALIASES = {'jz':'je', 'jnz':'jne', 'jc':'jb', 'jnc':'jae', 'sal':'shl',
    'cmovnz':'cmovne', 'cmovz':'cmove', 'setz':'sete', 'setnz':'setne', 'movabs':'mov'}

def native(text):
    # Keep RDX: the cold conversion also uses its implicit MUL/DIV result.
    return re.sub(r'\b(rcx|ecx)\b', lambda m: {'rcx':'rdi', 'ecx':'edi'}[m[0]], text)

def win_plan(name):
    r = solve.load(name)
    with contextlib.redirect_stdout(io.StringIO()):
        laid = solve.main(r)
    return r, laid

def sysv_sheet(name, encodings=None):
    r, laid = win_plan(name)
    r = SimpleNamespace(**vars(r))
    seq = [(native(t), s) for t, s in laid if t != 'PAD' and not t.startswith('NOP')]
    if name == 'json':
        assert seq[0][0] == 'movzx r11d, r8b'
        seq[0] = ('movzx r11d, dl', 4)
        assert seq[2][0] == 'jz @nilHot'
        seq.insert(3, ('mov rdx, rsi', 3))
    elif name != 'intt':
        seq.insert(0, ('mov rdx, rsi', 3))
    if encodings is not None:
        seq = [(t, s if t.endswith(':') or t.split()[0] in plan_hot.J else encodings[t]) for t, s in seq]
    r.hot, r.cold = seq, []
    r.SHORTEN = True
    # A higher dead-gap cost avoids oscillating short/near choices at the JSON head.
    if name == 'json':
        r.GAP_COST = 0.25
    r.MEM = {native(k): native(v) for k, v in r.MEM.items()}
    r.INS = {native(k): native(v) for k, v in r.INS.items()}
    r.COMMENT = {k: (native(h) if h else h, native(c)) for k, (h, c) in r.COMMENT.items()}
    return r

def measure(dump):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    result = {}
    for name in NAMES:
        r = sysv_sheet(name)
        expected = [t for t, s in r.hot if not t.endswith(':')]
        code = bytes.fromhex(dump['routines'][name]['code'])
        ins = list(md.disasm(code, 0))[:len(expected)]
        assert len(ins) == len(expected)
        sizes = {}
        for text, got in zip(expected, ins):
            mnemonic = text.split()[0]
            assert ALIASES.get(mnemonic, mnemonic) == ALIASES.get(got.mnemonic, got.mnemonic), (name, text, got.mnemonic, got.op_str)
            if mnemonic not in plan_hot.J:
                if text in sizes:
                    assert sizes[text] == got.size, text
                sizes[text] = got.size
        result[name] = sizes
        print(name, len(ins), 'instructions;', ins[-1].address + ins[-1].size, 'unpadded bytes')
    return result

def plans(name):
    enc = json.loads((HERE / 'encodings-sysv.json').read_text())
    r, win = win_plan(name)
    sr = sysv_sheet(name, enc[name])
    with contextlib.redirect_stdout(io.StringIO()):
        linux = solve.main(sr)
    return r, sr, win, linux

def generated(name):
    r, sr, win, linux = plans(name)
    # One source sheet, separately solved encodings/entry shims. No compiler-owned bytes in ASM.
    src = r.FILE.read_text(encoding='utf-8')
    a = src.index(r.FUNC)
    b = src.index('end;', a)
    asm = '{$ifdef FPC} nostackframe; assembler; asm {$else} asm .noframe {$endif}\n'
    sysv = emit.emit(sr, linux).replace('{$ifndef FPC}', '').replace('{$endif}', '')
    body = r.FUNC + '\n' + asm + '{$ifdef SYSVABI}\n' + sysv + '{$else}\n' + emit.emit(r, win) + '{$endif SYSVABI}\n'
    return r, src[:a] + body + src[b:]

def check(dump, abi):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    shared_table = None
    for name in NAMES:
        wr, sr, win, linux = plans(name)
        r, seq = (sr, linux) if abi == 'sysv' else (wr, win)
        entry = dump['routines'][name]['address']
        assert entry % 16 == 0, (name, 'entry is not 16-aligned', hex(entry))
        code = bytes.fromhex(dump['routines'][name]['code'])
        sizes, labels = plan_hot.sizes_with_targets(seq, shorten=getattr(r, 'SHORTEN', False))
        off = pads = 0
        prev = None
        loop_ends = {}
        branches = []
        tables = set()
        count = 0
        for idx, (text, size) in enumerate(seq):
            if text == 'PAD':
                pads += 1
                continue
            if text.endswith(':'):
                assert off == labels[text[:-1]]
                continue
            if text.startswith('NOP'):
                assert not pads and prev and prev[0] in ('jmp', 'ret'), (name, 'reachable gap', off)
                n = int(text[3:])
                gap = b''.join(i.bytes for i in md.disasm(code[off:off+n], entry+off) if i.mnemonic == 'nop')
                assert len(gap) == n, (name, 'bad dead gap', off)
                off += n
                prev = None
                continue
            mn = text.split()[0]
            size = sizes.get(idx, size) + pads
            assert code[off:off+pads] == b'\x3e' * pads, (name, 'missing DS', off)
            assert pads <= 3 and (not pads or mn not in plan_hot.J | {'tzcnt'}), (name, 'invalid DS', text, pads)
            got = next(md.disasm(code[off:off+size+15], entry+off), None)
            assert got and got.size == size, (name, hex(off), text, size,
                None if not got else (got.mnemonic, got.op_str, got.size, got.bytes.hex()))
            assert ALIASES.get(mn, mn) == ALIASES.get(got.mnemonic, got.mnemonic), (name, hex(off), text, got.mnemonic)
            if 'NumberSimdData' in text:
                table = got.address + got.size + got.operands[1].mem.disp
                assert table % 16 == 0, (name, 'unaligned internal SIMD table', hex(table))
                tables.add(table)
            if mn in plan_hot.J:
                start = prev[1] if prev and prev[0] in plan_hot.FUSE and mn not in ('jmp', 'ret') else off
                end = off + size
                assert start // 16 == (end-1) // 16 and end % 16, (name, 'JCC16', text, hex(off))
                if mn != 'ret':
                    target = text.split()[1]
                    branches.append((got.operands[0].imm - entry, labels[target], text, off))
                    if target in r.LOOPS and labels[target] < off:
                        loop_ends.setdefault(target, end)
            prev = (mn, off)
            off += size
            pads = 0
            count += 1
        assert not pads
        assert len(tables) == 1, (name, 'internal SIMD table targets', [hex(p) for p in tables])
        table = tables.pop()
        if shared_table is None:
            shared_table = table
        else:
            assert table == shared_table, (name, 'numeric entries do not share one table', hex(table), hex(shared_table))
        for actual, expected, text, offset in branches:
            assert actual == expected, (name, 'branch target', text, hex(offset), hex(actual), hex(expected))
        soft = []
        for target, end in loop_ends.items():
            start = labels[target]
            n = end - start
            fits = start // 32 == (end-1) // 32 if n <= 32 else start % 64 == r.LOOP64 and n <= 64
            assert fits or target not in r.HARD_LOOPS, (name, 'required loop placement', target, start, end)
            if not fits:
                soft.append(target)
        print(name, count, 'instructions;', off, 'bytes; entry mod64', entry % 64,
            '; JCC16 OK; soft loop splits:', ', '.join(soft) or 'none')
    print('NUMERIC_LAYOUT_PASS', abi)

def main():
    if sys.argv[1] == 'measure':
        data = measure(json.loads(Path(sys.argv[2]).read_text(encoding='utf-8-sig')))
        Path(sys.argv[3]).write_text(json.dumps(data, indent=2) + '\n')
    elif sys.argv[1] == 'emit':
        for name in NAMES:
            r, text = generated(name)
            r.FILE.write_text(text, encoding='utf-8', newline='\n')
            print(name, 'generated for Win64 and SysV')
    elif sys.argv[1] == 'check':
        check(json.loads(Path(sys.argv[2]).read_text(encoding='utf-8-sig')), sys.argv[3])
    elif sys.argv[1] == 'source-check':
        for name in NAMES:
            r, text = generated(name)
            assert r.FILE.read_text(encoding='utf-8') == text, (name, 'source differs from generated sheet')
        print('NUMERIC_SHEETS_PASS')
    else:
        raise SystemExit('expected measure, emit, check DUMP win64|sysv, or source-check')

if __name__ == '__main__':
    main()
