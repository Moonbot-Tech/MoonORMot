"""Check TTextWriter growth using the installed MoonCompiler product fpc."""
import argparse
import itertools
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', type=Path, required=True)
parser.add_argument('--results', type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).with_name('TextWriterGrowth.dpr').resolve()
root = source.parents[2]
results = args.results.resolve()
results.mkdir(parents=True, exist_ok=False)
rows = []
for profile, flags in [('debug', []), ('release', ['-dRELEASE'])]:
    build = results / profile
    build.mkdir()
    command = [str(args.compiler.resolve()), *flags, '-Fu' + str(root / 'core'),
               '-FU' + str(build), '-FE' + str(build), str(source)]
    process = subprocess.run(command, capture_output=True, text=True, timeout=120)
    (build / 'build.log').write_text(process.stdout + process.stderr, encoding='utf-8')
    (build / 'command.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    if process.returncode:
        raise SystemExit(f'BUILD_FAIL {profile}: {build / "build.log"}')
    exe = build / ('TextWriterGrowth.exe' if os.name == 'nt' else 'TextWriterGrowth')
    for ownership, growth, mode in itertools.product(
            ('owned', 'external'), ('small', 'large'), ('nil', 'raise', 'success')):
        process = subprocess.run([str(exe), ownership, growth, mode],
                                 capture_output=True, text=True, timeout=30)
        passed = process.returncode == 0 and 'continued_correctly=TRUE' in process.stdout
        rows.append(dict(profile=profile, ownership=ownership, growth=growth, mode=mode,
                         exit=process.returncode, stdout=process.stdout, stderr=process.stderr,
                         passed=passed))
        (results / 'results.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
        print('PASS' if passed else 'FAIL', profile, ownership, growth, mode, flush=True)
raise SystemExit(0 if all(row['passed'] for row in rows) else 1)
