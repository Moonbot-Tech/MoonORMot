# Default OpenSSL trust cache

New default-trust client connections share one parsed CA store. The next
connection after one hour reloads the sources; `OpenSslClearDefaultTrust`
requests an earlier reload. Existing SSL contexts keep their own references.
This avoids parsing the system CA bundle on every reconnect. Certificate chain,
expiry and hostname checks still run for every new TLS connection.

The cache applies to ordinary client contexts using default trust sources.
Explicit CA files, servers and verification callbacks retain their original
setup. Windows SChannel is unchanged. Low-level callers of
`OpenSslLoadDefaultTrust` must use a fresh default-library-context SSL_CTX and
must not mutate its shared store. A zero age bypasses the cache.

Updates of the default CA file or environment take effect on expiry or clear.
Default CA directories/provider stores retain OpenSSL's lazy lookup behavior;
the cache is not an atomic snapshot of every trust file. An application that
updates trust sources can call clear after that update. There is no background
timer, directory watcher or stat/hash operation on each connection.

Run with an installed MoonCompiler driver, Python 3 and OpenSSL command:

```text
python tests/tls-trust-cache/run_contracts.py --compiler PATH/bin/fpc --library . --out NEW_DIRECTORY
```

On Windows use `PATH/bin/x86_64-win64/fpc.exe`, pass `--openssl` if the OpenSSL
command is not on PATH, and `--openssl-dir` for a directory containing
`libcrypto-3.dll` and `libssl-3.dll`. The test deliberately forces OpenSSL on
Windows to exercise this backend. No system trust settings are modified.

The runner generates short-lived local roots/server certificates and tests
Debug and Release: hit/expiry/clear/bypass, changed trust, context lifetime,
800 concurrent context creations with clears, valid encrypted exchanges,
wrong hostname, unknown issuer and custom CA isolation. An optional
`--ca-bundle FILE --count 100` measures complete loopback TLS connections plus
a checked ping/pong, comparing clear-before-each-connection with warm reuse.
These times do not predict Internet RTT or session-resumption performance.

The same probe also compiles on Delphi 12.2 Win64. Build it with
`RELEASE;FORCE_OPENSSL`, the usual core/lib/net/crypt search paths and System/Winapi
namespaces. Pass fixture directory and OpenSSL DLL directory to test ownership;
the runner-created fixtures can be reused. Full TLS exchange additionally needs
the local server port and optional connection count as arguments 3 and 4.
