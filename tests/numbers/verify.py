"""Deterministic, byte-exact five-entry qualification across libraries, paths and ABIs.
The cursor entries GetNextExtended/GetNextInt64 are checked against GetExtended/GetInteger(P, err).

python verify.py LIBRARY --sse2 SSE2_LIBRARY [--reference WHOLE.dll] [--corpus LAB/data] [--write result.json]
python verify.py LIBRARY --sse2 SSE2_LIBRARY --corpus DATA --expect win64.json

The digest contains value bits, errors, JSON types/cursors, and both AllowDouble states.
No library address or padding byte enters it. A mismatch fails the process.
"""
import argparse, ctypes as C, hashlib, json, random, re, struct, time
from pathlib import Path
from numeric_api import Library, Pages

def cases(count, corpus):
    yield from (b'', b'-', b'+', b'.', b'.5', b'1.', b'1..2', b'1e', b'1e+', b'-0', b'-0.0',
        b'0e9999', b'0.00e-9223372036854775807', b'0.00e-9223372036854775808',
        b'0e-9223372036854775809', b'0e-92233720368547758080', b'228518839.20000000', b'0.0000000000000000077',
        b'1.5e1', b'1.50e1', b'1.500000000000000000e18', b'9223372036854775807',
        b'-9223372036854775808', b'9223372036854775808', b'18446744073709551615',
        b'1.7976931348623157e308', b'1e-324', b'4.9406564584124654e-324')
    for n in range(131):
        for prefix, suffix in ((b' ', b'1'), (b'+', b'1'), (b'- ', b'1'), (b'0', b'12345'),
                (b'0.', b'123e12'), (b'1e', b'1'), (b'12345', b'.7'), (b'1.25', b'')):
            yield prefix + (b' ' if prefix in (b' ', b'+', b'- ') else b'0') * n + suffix
    for a in range(256):
        for b in range(256):
            # Embedded NUL is deliberate: bounded/text stop early, checked reports their status.
            yield bytes((a, b))
    rng = random.Random(20260916)
    for i in range(count):
        n = rng.randrange(1, 90)
        raw = bytes(rng.choices(b'0123456789', k=n))
        if i % 3:
            at = rng.randrange(n + 1)
            raw = raw[:at] + b'.' + raw[at:]
        if i % 4:
            raw += b'e' + str(rng.randrange(-400, 401)).encode()
        raw = rng.choice((b'', b'-', b'+', b'  -', b'0' * 17)) + raw
        if i % 11 == 0:
            at = rng.randrange(len(raw))
            raw = raw[:at] + bytes((rng.randrange(256),)) + raw[at + 1:]
        yield raw
    if corpus:
        for name in ('golden.bin', 'boundaries.bin'):
            with (corpus / name).open('rb') as f:
                total, = struct.unpack('<I', f.read(4))
                for _ in range(total):
                    n, = struct.unpack('<I', f.read(4))
                    yield f.read(n)
                    f.read(8)  # independent oracle values are checked by NumberLab

def record(lib, p, bound):
    e = C.c_int(-777)
    value = lib.string(p, C.byref(e))
    out = struct.pack('<di', value, e.value)
    for allow in (False, True):
        var = (C.c_ubyte * 32)(*([0xa5] * 32))
        end = lib.json(p, C.byref(var), allow)
        if not end:
            assert bytes(var) == b'\xa5' * 32, 'invalid JSON modified Value'
            out += struct.pack('<iHQ', -1, 0, 0)
        else:
            kind, = struct.unpack_from('<H', var)
            bits, = struct.unpack_from('<Q', var, 8)
            out += struct.pack('<iHQ', end - p, kind, bits)
    b = lib.bounded(p, bound)
    t = lib.text(p)
    k = lib.checked(p, C.byref(e))
    return out + struct.pack('<qqqi', b, t, k, e.value)

JSON_NUMBER = re.compile(rb'-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)?')
JSON_INTEGER = re.compile(rb'-?(0|[1-9][0-9]*)')
JSON_DELIMITERS = set(b'\0\t\n\r ,]}')

def cursor(lib, pages, raw, tail, offset):
    """GetNextExtended/GetNextInt64 = GetExtended/GetInteger(P, err) on the same text, P on its ending byte."""
    if not hasattr(lib.lib, 'NextExtended'):
        return b''
    text = raw.split(b'\0', 1)[0]
    p = pages.put(text, tail, offset)
    e = C.c_int(-1)
    string = (struct.pack('<d', lib.string(p, C.byref(e))), e.value)
    integer = (struct.pack('<q', lib.checked(p, C.byref(e))), e.value)
    inside = bool(JSON_DELIMITERS & set(text))
    out = b''
    for ending, after in ((0, b''), (34, b'"'), (256, b','), (256, b'}'), (256, b' '), (256, b'')):
        q = pages.put(text + after, tail, offset)
        for entry, pack, ref, json, quoted in (
                (lib.lib.NextExtended, '<d', string, JSON_NUMBER.fullmatch(text), b'"' not in text),
                (lib.lib.NextInt64, '<q', integer, JSON_INTEGER.fullmatch(text), b'"' not in text and min(text, default=32) >= 32)):
            e = C.c_int(-7)
            c = C.c_void_p(0)
            bits = struct.pack(pack, entry(q, ending, C.byref(e), C.byref(c)))
            if ending == 0 or (ending == 34 and quoted) or (ending == 256 and json):
                assert (bits, e.value) == ref, (entry.__name__, ending, raw, bits.hex(), e.value, ref)
                assert e.value or c.value == q + len(text), (entry.__name__, ending, raw, 'cursor')
            elif ending == 256 and not inside:
                assert e.value, (entry.__name__, raw, 'accepted outside the JSON grammar')
            out += bits + struct.pack('<ii', e.value, c.value - q if e.value == 0 else -1)
    return out

def regressions(lib, pages):
    # Representable exponent, but adding the fractional scale overflows Int64.
    for tail in (False, True):
        for raw in (b'0.00e-9223372036854775807', b'0e9999', b'-0.00e-9223372036854775807',
                b'0.00e-9223372036854775808', b'-0e-9223372036854775808'):
            p = pages.put(raw, tail)
            e = C.c_int(-1)
            value = lib.string(p, C.byref(e))
            assert value == 0 and e.value == 0, (raw, value, e.value)
            assert struct.pack('<d', value) == struct.pack('<Q', (1 << 63) if raw[0] == 45 else 0)
            var = (C.c_ubyte * 32)()
            assert lib.json(p, C.byref(var), True) == p + len(raw), raw
            assert struct.unpack_from('<Q', var, 8)[0] == 0, raw
        for raw in (b'0e9223372036854775808', b'0e-9223372036854775809', b'0e-92233720368547758080'):
            p = pages.put(raw, tail)
            e = C.c_int(-1)
            lib.string(p, C.byref(e))
            assert e.value != 0, ('overflowing exponent accepted', raw)
            var = (C.c_ubyte * 32)()
            assert not lib.json(p, C.byref(var), True), ('overflowing JSON exponent accepted', raw)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('library', type=Path)
    ap.add_argument('--sse2', type=Path, required=True)
    ap.add_argument('--reference', type=Path)
    ap.add_argument('--corpus', type=Path)
    ap.add_argument('--random', type=int, default=100000)
    ap.add_argument('--write', type=Path)
    ap.add_argument('--expect', type=Path)
    args = ap.parse_args()
    libraries = [('native', Library(args.library)), ('sse2', Library(args.sse2))]
    reference = Library(args.reference) if args.reference else None
    pages = Pages()
    texts = list(cases(args.random, args.corpus))
    rows = []
    start = time.monotonic()
    try:
        for path, lib in libraries:
            regressions(lib, pages)
            if hasattr(lib.lib, 'CheckDocument'):
                assert lib.lib.CheckDocument() == 0, 'complete JSON document integration'
            for tail in (False, True):
                digest = hashlib.sha256()
                for i, raw in enumerate(texts):
                    p = pages.put(raw, tail, i & 63)
                    bound = p + (len(raw) if i % 2 else len(raw) // 2)
                    got = record(lib, p, bound)
                    if reference:
                        expected = record(reference, p, bound)
                        assert got == expected, (path, tail, i, raw, got.hex(), expected.hex())
                    digest.update(got)
                    digest.update(cursor(lib, pages, raw, tail, i & 63))
                row = dict(path=path, guard=tail, cases=len(texts), sha256=digest.hexdigest())
                print(row, flush=True)
                rows.append(row)
        assert len({r['sha256'] for r in rows}) == 1, 'native/SSE2/page paths disagree'
        # Null input and all JSON signed-zero paths, including directed modes.
        for _, lib in libraries:
            record(lib, 0, 0)
            for mode in range(4):
                lib.lib.SetRounding(mode)
                for raw in (b'-0.0', b'-0e5', b'-0.00000000000000000000000'):
                    for tail in (False, True):
                        p = pages.put(raw, tail)
                        var = (C.c_ubyte * 32)()
                        assert lib.json(p, C.byref(var), True)
                        assert struct.unpack_from('<Q', var, 8)[0] == 0, 'JSON minus zero'
            lib.lib.SetRounding(0)
    finally:
        pages.close()
    if args.expect:
        assert rows == json.loads(args.expect.read_text())['rows'], 'platform result differs'
    result = dict(rows=rows, seconds=time.monotonic() - start)
    if args.write:
        args.write.parent.mkdir(parents=True, exist_ok=True)
        args.write.write_text(json.dumps(result, indent=2) + '\n')
    print('NUMERIC_MATRIX_PASS', len(texts) * len(rows), 'records;', round(result['seconds'], 2), 'seconds')

if __name__ == '__main__':
    main()
