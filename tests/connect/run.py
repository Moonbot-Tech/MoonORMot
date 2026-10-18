"""Check initial connect results against native errors and deterministic callbacks."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', type=Path, required=True)
parser.add_argument('--library', type=Path, default=HERE.parents[1])
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
compiler = args.compiler.resolve(strict=True)
library = args.library.resolve(strict=True)
output = args.out.resolve()
output.mkdir(parents=True, exist_ok=False)
if os.name == 'nt':
    import ctypes
    ctypes.windll.kernel32.SetErrorMode(3)
subprocess.run([sys.executable, str(HERE/'prepare_connect_fixture.py'), str(library), str(output)], check=True)
target = 'x86_64-win64' if os.name == 'nt' else 'x86_64-linux'
for profile, flags in (('debug', []), ('release', ['-dRELEASE'])):
    directory = output/profile
    directory.mkdir()
    for name, marker in (('connect_fixture_contract', 'CONNECT_FIXTURE_CONTRACT checks=35 failures=0'),
                         ('socket_connect_native_immediate', 'SOCKET_CONNECT_NATIVE_IMMEDIATE_PASS')):
        command = [str(compiler), *flags, '-Fi'+str(output), '-Fu'+str(library/'*'),
                   '-Fl'+str(library/'static'/target), '-FE'+str(directory), '-FU'+str(directory), str(HERE/(name+'.dpr'))]
        build = subprocess.run(command, capture_output=True, timeout=180)
        (directory/(name+'-build.log')).write_bytes(build.stdout+build.stderr)
        if build.returncode:
            raise RuntimeError(f'{name} {profile}: compilation failed; see {directory}')
        exe = directory/(name+('.exe' if os.name == 'nt' else ''))
        run = subprocess.run([str(exe)], capture_output=True, timeout=30)
        (directory/(name+'-run.log')).write_bytes(run.stdout+run.stderr)
        if run.returncode or marker.encode() not in run.stdout:
            raise RuntimeError(f'{name} {profile}: execution failed; see {directory}')
        print('PASS', name, profile, flush=True)
print('CONNECT_CONTRACTS_PASS')
