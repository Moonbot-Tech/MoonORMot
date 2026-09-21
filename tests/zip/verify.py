"""Direct mormot.core.zip regression; independent fixtures and byte oracle.

python verify.py --exe BUILD/ZipRegression --results RESULTS
Only writes RESULTS. Python 3.9+ standard library; no System.Zip dependency.
"""
import argparse
import bisect
import io
import json
from pathlib import Path
import random
import shutil
import struct
import subprocess
import zipfile


class NonSeekable(io.BytesIO):
    def seek(self, *args):
        raise io.UnsupportedOperation('nonseekable fixture')


def fixture(entries, streaming=False, zip64='none', metadata=False):
    output = NonSeekable() if streaming else io.BytesIO()
    saved_limit = zipfile.ZIP64_LIMIT
    try:
        if zip64 == 'full':
            zipfile.ZIP64_LIMIT = 1  # exercise ZIP64 central sizes/offsets/EOCD with small data
        with zipfile.ZipFile(output, 'w') as archive:
            for name, data in entries.items():
                info = zipfile.ZipInfo(name, (2026, 9, 22, 0, 0, 0))
                info.compress_type = zipfile.ZIP_STORED if name.endswith('.bin') else zipfile.ZIP_DEFLATED
                if metadata:
                    info.comment = b'entry comment'
                    info.extra = struct.pack('<HH', 0xcafe, 3) + b'abc'
                with archive.open(info, 'w', force_zip64=zip64 != 'none') as stream:
                    stream.write(data)
    finally:
        zipfile.ZIP64_LIMIT = saved_limit
    return output.getvalue()


def without_descriptor_signatures(raw):
    """Delete only optional signatures, then rebase ZIP32/ZIP64 directory offsets."""
    data = bytearray(raw)
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        removed = []
        for entry in archive.infolist():
            pos = entry.header_offset
            assert struct.unpack_from('<H', data, pos + 6)[0] & 8
            name_size, extra_size = struct.unpack_from('<HH', data, pos + 26)
            pos += 30 + name_size + extra_size + entry.compress_size
            assert data[pos:pos + 4] == b'PK\x07\x08'
            removed.append(pos)
        removed.sort()
        rebase = lambda pos: pos - 4 * bisect.bisect_left(removed, pos)
        pos = archive.start_dir
        for entry in archive.infolist():
            assert data[pos:pos + 4] == b'PK\x01\x02'
            name_size, extra_size, comment_size = struct.unpack_from('<HHH', data, pos + 28)
            offset, = struct.unpack_from('<I', data, pos + 42)
            if offset != 0xffffffff:
                struct.pack_into('<I', data, pos + 42, rebase(offset))
            else:
                extra = pos + 46 + name_size
                while extra < pos + 46 + name_size + extra_size:
                    tag, size = struct.unpack_from('<HH', data, extra)
                    if tag == 1:
                        compressed, full = struct.unpack_from('<II', data, pos + 20)
                        field = extra + 4 + 8 * (full == 0xffffffff) + 8 * (compressed == 0xffffffff)
                        offset, = struct.unpack_from('<Q', data, field)
                        struct.pack_into('<Q', data, field, rebase(offset))
                        break
                    extra += 4 + size
                else:
                    raise AssertionError('ZIP64 offset field missing')
            pos += 46 + name_size + extra_size + comment_size
        eocd = len(data) - 22  # generated fixtures have no comment
        assert data[eocd:eocd + 4] == b'PK\x05\x06'
        offset, = struct.unpack_from('<I', data, eocd + 16)
        if offset != 0xffffffff:
            struct.pack_into('<I', data, eocd + 16, rebase(offset))
        locator = eocd - 20
        if data[locator:locator + 4] == b'PK\x06\x07':
            record, = struct.unpack_from('<Q', data, locator + 8)
            struct.pack_into('<Q', data, locator + 8, rebase(record))
            offset, = struct.unpack_from('<Q', data, record + 48)
            struct.pack_into('<Q', data, record + 48, rebase(offset))
    for pos in reversed(removed):
        del data[pos:pos + 4]
    return data


def check_archive(path, expected):
    with zipfile.ZipFile(path) as archive:
        assert archive.namelist() == list(expected), archive.namelist()
        assert archive.testzip() is None
        for name, data in expected.items():
            assert archive.read(name) == data, name


def offset_only_zip64(raw):
    """Force only the central local-header offsets into ZIP64 extra fields."""
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        data = bytearray(raw[:archive.start_dir])
        pos = archive.start_dir
        for entry in archive.infolist():
            record = bytearray(raw[pos:pos + 46])
            name_size, extra_size, comment_size = struct.unpack_from('<HHH', record, 28)
            assert extra_size == comment_size == 0
            struct.pack_into('<H', record, 6, 45)  # version needed
            struct.pack_into('<H', record, 30, 12)
            struct.pack_into('<I', record, 42, 0xffffffff)
            data += record + raw[pos + 46:pos + 46 + name_size]
            data += struct.pack('<HHQ', 1, 8, entry.header_offset)
            pos += 46 + name_size
        eocd = bytearray(raw[pos:])
        struct.pack_into('<I', eocd, 12, len(data) - archive.start_dir)
        return data + eocd


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20260922)
    entries = {'first.bin': rng.randbytes(8193), 'empty.txt': b'',
               'middle.txt': rng.randbytes(4097), 'last.bin': rng.randbytes(8191)}
    fixtures = {}
    for zip64 in ('none', 'local', 'full'):
        fixtures[f'seekable-{zip64}'] = fixture(entries, zip64=zip64)
        raw = fixture(entries, streaming=True, zip64=zip64)
        fixtures[f'descriptor-signed-{zip64}'] = raw
        fixtures[f'descriptor-unsigned-{zip64}'] = without_descriptor_signatures(raw)
    fixtures['seekable-metadata'] = fixture(entries, metadata=True)
    fixtures['seekable-offset-only-zip64'] = offset_only_zip64(fixtures['seekable-none'])
    checks = []

    def run(label, action, archive, mem, fourth, expected):
        result = {'case': label}
        try:
            process = subprocess.run([str(args.exe.resolve()), action, str(archive.resolve()),
                                      str(mem), str(fourth) or '-'], capture_output=True, text=True, timeout=30)
            result.update(returncode=process.returncode, stdout=process.stdout, stderr=process.stderr)
            assert process.returncode == 0, process.stdout.strip() or process.stderr.strip()
            if action == 'read':
                names = (fourth / 'names.txt').read_text(encoding='utf-8-sig').splitlines()
                assert names == list(expected), names
                for i, data in enumerate(expected.values()):
                    assert (fourth / f'{i}.bytes').read_bytes() == data, f'UnZip {i}'
                    assert (fourth / f'{i}.stream').read_bytes() == data, f'UnZip stream {i}'
                if mem == 512:
                    assert 'disk_entries=0' not in process.stdout, 'disk path was not exercised'
            elif action == 'copy':
                check_archive(fourth, expected)
            elif action == 'fail-filter':
                assert archive.read_bytes() == expected, 'failed constructor changed the input ZIP'
            else:
                check_archive(archive, expected)
            result['pass'] = True
        except (AssertionError, OSError, zipfile.BadZipFile, subprocess.TimeoutExpired) as error:
            result.update({'pass': False, 'error': str(error)})
        checks.append(result)

    for name, data in fixtures.items():
        directory = args.results / name
        directory.mkdir(exist_ok=True)
        original = directory / 'original.zip'
        original.write_bytes(data)
        check_archive(original, entries)  # fail immediately if our fixture is invalid
        prefixed = directory / 'prefixed.zip'
        prefixed.write_bytes(b'executable-prefix\0' * 17 + data)
        check_archive(prefixed, entries)
        run(f'{name}/copy-prefix-disk', 'copy', prefixed, 512,
            (directory / 'copy-prefix-disk.zip').resolve(), entries)
        for mode, mem in (('memory', 0), ('file', 1 << 20), ('disk', 512)):
            output = directory / mode
            output.mkdir(exist_ok=True)
            run(f'{name}/read-{mode}', 'read', original, mem, output.resolve(), entries)
            run(f'{name}/copy-{mode}', 'copy', original, mem,
                (directory / f'copy-{mode}.zip').resolve(), entries)
        for mode, mem in (('file', 1 << 20), ('disk', 512)):
            for action, removed in (('append', ''), ('noop', ''), ('delete-first', 'first.bin'),
                                    ('delete-middle', 'middle.txt'), ('delete-last', 'last.bin'), ('delete-all', '*')):
                output = directory / f'{action}-{mode}.zip'
                shutil.copyfile(original, output)
                expected = {key: value for key, value in entries.items() if key != removed and removed != '*'}
                if action == 'append':
                    expected['appended.txt'] = b'added by TZipWrite'
                operation = 'delete' if action in ('delete-first', 'delete-middle', 'delete-last') else action
                run(f'{name}/{action}-{mode}', operation,
                    output, mem, removed, expected)
            output = directory / f'fail-filter-{mode}.zip'
            shutil.copyfile(original, output)
            run(f'{name}/fail-filter-{mode}', 'fail-filter', output, mem, '', data)
    (args.results / 'results.json').write_text(json.dumps(checks, indent=2), encoding='utf-8')
    for name in fixtures:
        subset = [check for check in checks if check['case'].startswith(name + '/')]
        print(f'{name}: {sum(check["pass"] for check in subset)}/{len(subset)} PASS')
    failed = [check for check in checks if not check['pass']]
    print(f'ZIP_REGRESSION: {len(checks) - len(failed)}/{len(checks)} PASS')
    raise SystemExit(bool(failed))


if __name__ == '__main__':
    main()
