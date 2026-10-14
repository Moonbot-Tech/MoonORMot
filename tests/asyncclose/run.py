"""Compile and run the async-close regression with the installed MoonCompiler."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', type=Path, required=True)
parser.add_argument('--results', type=Path, required=True)
parser.add_argument('--library', type=Path)
args = parser.parse_args()
source = Path(__file__).with_name('AsyncCloseRace.dpr').resolve()
root = (args.library or source.parents[2]).resolve()
results = args.results.resolve()
results.mkdir(parents=True, exist_ok=False)
rows = []
target = 'x86_64-win64' if sys.platform == 'win32' else 'x86_64-linux'
for profile, flags in [('debug', []), ('release', ['-dRELEASE'])]:
    output = results / profile
    output.mkdir()
    command = [str(args.compiler.resolve()), *flags,
               *['-Fu' + str(p) for p in root.iterdir() if p.is_dir()],
               '-Fl' + str(root / 'static' / target),
               '-FU' + str(output), '-FE' + str(output), str(source)]
    (output / 'command.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    process = subprocess.run(command, capture_output=True, text=True, timeout=180)
    (output / 'build.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    if process.returncode:
        print(f'BUILD_FAIL {profile}: {output / "build.log"}', flush=True)
        sys.exit(2)
    exe = output / ('AsyncCloseRace.exe' if sys.platform == 'win32' else 'AsyncCloseRace')
    process = subprocess.run([str(exe)], capture_output=True, text=True, timeout=90)
    (output / 'run.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    passed = process.returncode == 0 and 'FAILURES=0' in process.stdout
    rows.append(dict(profile=profile, exit=process.returncode, passed=passed))
    (results / 'results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print('PASS' if passed else 'FAIL', profile, flush=True)
sys.exit(0 if all(row['passed'] for row in rows) else 1)
