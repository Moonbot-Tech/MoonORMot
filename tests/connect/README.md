# Initial connection errors

Run against this checkout with an installed MoonCompiler toolchain:

```text
python tests/connect/run.py --compiler PATH/toolchain/bin/fpc --out NEW_DIRECTORY
```

On Windows use `PATH/toolchain/bin/x86_64-win64/fpc.exe`. The tests run Debug and
Release. The native test deliberately connects an IPv4 socket to an IPv6 address:
the OS rejects the request immediately, while a later `SO_ERROR` is zero. It
checks both asynchronous initiation and the positive-timeout entry point without
depending on Internet access, routing or IPv6 resolver configuration.

The deterministic fixture extracts the actual connection/error-classification
bodies from the selected library tree. Native calls are controlled to check
success, immediate errors, pending completion, EINTR and wait outcomes. This
allows the rare interrupted-connect path to be tested without timing a signal.
The pre-fix library fails the immediate-error contract; a version that treats
POSIX EINTR as fatal fails the interruption contract.

MoonCompiler additionally tests HTTP synchronous/asynchronous error propagation,
same-client recovery, refusal, timeout and cancellation. Its port-exhaustion
fixture runs only in a new Linux network namespace.
