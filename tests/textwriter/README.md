# TTextWriter growth failure regression

Run with an installed MoonCompiler product `fpc` (not bare `ppcx64`):

```text
python tests/textwriter/run.py --compiler PATH/toolchain/bin/fpc --results NEW_OUTPUT_DIRECTORY
```

On Windows use `PATH/toolchain/bin/x86_64-win64/fpc.exe`. The toolchain must have
its usual adjacent MoonORMot dependency. The runner puts this checkout's core
sources first on the unit path; all generated files go to the new output directory.

Twenty-four checks cover Debug and Release, owned and external buffers, growth
from 256 to 512 bytes and from 64 KiB to 1 MiB, and allocation success, nil and
exception results. The thresholds are reached through public writer operations.
The output stream checks every byte and performs no allocations while writing.

For failure cases, a temporary single-threaded memory-manager wrapper redirects
exactly one growth allocation to an impossible 2^60-byte request to the original
allocator. Its normal ReturnNilIfGrowHeapFails policy determines nil or an
out-of-memory exception; the test does not exhaust physical memory. It checks
pointer, capacity, end pointer, ownership, actual release of the previous buffer,
flushed data and continued writing after failure. The manager is restored in finally.

The pre-fix implementation fails all nil cases. Initial constructor allocation,
stream write failures and whole-framework out-of-memory recovery are outside
this regression's scope.
