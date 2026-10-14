# Async connection close ownership

Run with the installed MoonCompiler product driver:

```text
python tests/asyncclose/run.py --compiler PATH/toolchain/bin/fpc --results NEW_DIRECTORY
```

On Windows use `PATH/toolchain/bin/x86_64-win64/fpc.exe`. `--library PATH`
selects a different library tree, so the same tests can check the pre-fix pin.
All generated files go to the new results directory. Tests bind only loopback,
with OS-assigned ports, and need no service, credentials or Internet access.

The test uses the real close methods, socket shutdown, connection lists and GC
publication. Event barriers pause a virtual callback before/after inherited
`OnClose`, or pause a virtual `Stop` after its socket becomes nil. The latter
models a slow close without relying on a particular OS linger timeout. The
second closer must return without running another callback/stop/retirement.
Both direct close and read/write unlock-and-close entry points are covered.

Further cases cover a failed writer-lock acquisition, a losing closer releasing
its own lock, reentrant closure, recycling, callback exceptions, both orderings
of `ConnectionRemove` versus direct close, callbacks acquiring the owner list,
and the count/GC contract for `acoNoConnectionTrack`. Two live async loopback
cases each deliver 50 replies and retire 50 connections, closing from `OnRead`
and from nested `Write`/`AfterWrite`, through IOCP on Windows and epoll on Linux.

The pre-fix `90621ced` fails the ownership regressions. Qualification uses
MoonCompiler 2.4.6, Debug and Release, on Win64 and Linux x86-64. These are
focused lifecycle tests, not qualification of every mORMot protocol or TLS
backend; the blocked `Stop` is deterministic instrumentation, not an OS hang.

## Application integration

Use `Clients.CloseConnection(var Connection, Reason)` (a public entry point as
of MoonORMot 14), or `ConnectionRemove(Handle)`. The first clears the supplied
reference, including when another caller already owns the close. Keep a separate
application hold if code must continue using the instance after this call.
`ConnectionRemove` now calls `OnClose` before socket shutdown, outside the list
lock, and returns false if another caller already claimed the connection.

Do not manually compose `OnClose -> Stop -> EndConnection`: those low-level
operations bypass the close claim. Likewise an `OnClose` override is a
notification, not a place to call `Stop` or `EndConnection` itself.

The claim serializes closure; it does not keep arbitrary external pointers
alive. Retain application holds and join holders before freeing GC/server
state during shutdown. A timeout is not proof that a holder finished.
