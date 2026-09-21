"""Build and run direct mormot.core.zip qualification with explicit source/toolchain paths."""
import argparse
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
for option in ('compiler', 'mormot', 'results'):
    parser.add_argument('--' + option, type=Path, required=True)
parser.add_argument('--rtl', type=Path, help='MoonCompiler RTL unit directory (required with ppcx64)')
parser.add_argument('--optimization', choices=('O-', 'O2', 'O3'), default='O-')
args = parser.parse_args()
build = args.results.resolve() / 'build'
build.mkdir(parents=True, exist_ok=True)
root = args.mormot.resolve()
source = Path(__file__).with_name('ZipRegression.dpr').resolve()
units = [root / name for name in ('core', 'net', 'lib', 'crypt')]
compiler = args.compiler.resolve()
if compiler.stem.lower() == 'dcc64':
    # Compiled Delphi RTL units only; never add the proprietary RTL source tree.
    rtl = args.rtl or compiler.parent.parent / 'lib' / 'win64' / 'release'
    command = [str(compiler), str(source), '-B', '-Q', '-$O-' if args.optimization == 'O-' else '-$O+',
               '-NSSystem;Winapi', '-U' + ';'.join(map(str, [rtl, *units])),
               '-I' + str(root), '-E' + str(build), '-N0' + str(build)]
    executable = build / 'ZipRegression.exe'
else:
    if args.rtl is None:
        parser.error('--rtl is required with MoonCompiler')
    rtl = args.rtl.resolve()
    command = [str(compiler), '-n', '-B', '-Mdelphi', '-gl', '-gw3', '-' + args.optimization,
               '-dMOONCOMPILER_UNICODE_DEFAULT', '-dMOONCOMPILER_VANILLA_RUNTIME',
               *['-Fu' + str(path) for path in [rtl, *units]],
               *['-Fu' + str(path) for path in rtl.parent.iterdir() if path.is_dir() and path != rtl],
               '-Fi' + str(root), '-Fo' + str(root), '-Fl' + str(root / 'static' / rtl.parent.name),
               '-FU' + str(build), '-FE' + str(build), str(source)]
    if sys.platform.startswith('linux'):
        gcc_lib = Path(subprocess.check_output(['gcc', '-print-file-name=libgcc_s.so'], text=True).strip())
        if gcc_lib.is_file():
            command.insert(-1, '-Fl' + str(gcc_lib.parent))
    executable = build / ('ZipRegression.exe' if sys.platform == 'win32' else 'ZipRegression')
process = subprocess.run(command, capture_output=True, text=True)
(build / 'build.log').write_text(process.stdout + process.stderr, encoding='utf-8')
(build / 'build-command.txt').write_text('\n'.join(command), encoding='utf-8')
if process.returncode:
    print(process.stdout[-4000:] + process.stderr)
    raise SystemExit(process.returncode)
raise SystemExit(subprocess.call([sys.executable, str(source.with_name('verify.py')),
                                 '--exe', str(executable), '--results', str(args.results.resolve() / 'cases')]))
