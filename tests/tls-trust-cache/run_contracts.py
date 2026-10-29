"""Exercise real OpenSSL trust-cache ownership, refresh and TLS verification."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import threading

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', type=Path, required=True, help='MoonCompiler fpc driver')
p.add_argument('--library', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--openssl', default='openssl', help='OpenSSL command for ephemeral fixtures')
p.add_argument('--openssl-dir', type=Path, help='Windows libcrypto-3.dll and libssl-3.dll directory')
p.add_argument('--ca-bundle', type=Path, help='Optional real CA bundle for representative timing')
p.add_argument('--count', type=int, default=5)
p.add_argument('--overlay', type=Path, help='Optional candidate source directory, before library units')
a = p.parse_args()
here = Path(__file__).resolve().parent
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
library = a.library.resolve(strict=True)
fixtures = out/'fixtures'
fixtures.mkdir()
(fixtures/'empty').mkdir()

def command(args, log, env=None, timeout=120):
    r = subprocess.run(list(map(str, args)), env=env, capture_output=True, timeout=timeout)
    log.write_bytes(r.stdout+r.stderr)
    if r.returncode:
        raise RuntimeError(f'exit {r.returncode}: see {log}')
    return r.stdout

for root in ('a','b'):
    command([a.openssl,'req','-x509','-newkey','rsa:2048','-nodes','-keyout',fixtures/f'{root}.key',
             '-out',fixtures/f'root-{root}.pem','-subj',f'/CN=TestRoot{root}','-days','2'],out/f'root-{root}.log')
command([a.openssl,'req','-newkey','rsa:2048','-nodes','-keyout',fixtures/'server.key',
         '-out',fixtures/'server.csr','-subj','/CN=localhost'],out/'server-request.log')
(fixtures/'server.ext').write_text('subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n')
command([a.openssl,'x509','-req','-in',fixtures/'server.csr','-CA',fixtures/'root-a.pem','-CAkey',fixtures/'a.key',
         '-CAcreateserial','-out',fixtures/'server.pem','-days','2','-extfile',fixtures/'server.ext'],out/'server-cert.log')
bundle = a.ca_bundle.read_bytes() if a.ca_bundle else b''
for root in ('a','b'):
    (fixtures/f'bundle-{root}.pem').write_bytes(bundle+b'\n'+(fixtures/f'root-{root}.pem').read_bytes())
shutil.copyfile(fixtures/'root-a.pem', fixtures/'current.pem')
env = os.environ.copy()
env['SSL_CERT_FILE'] = str(fixtures/'current.pem')
env['SSL_CERT_DIR'] = str(fixtures/'empty')
# Windows OpenSSL CRT reads the process-start environment, before DLL load.
tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
tls.load_cert_chain(fixtures/'server.pem',fixtures/'server.key')
listener = socket.socket()
listener.bind(('127.0.0.1',0))
listener.listen()
port = listener.getsockname()[1]
stop = threading.Event()
errors = []

def serve():
    while not stop.is_set():
        try:
            peer,_ = listener.accept()
            try:
                with tls.wrap_socket(peer,server_side=True) as secure:
                    data = b''
                    while len(data)<4:
                        part=secure.recv(4-len(data))
                        if not part:
                            break
                        data+=part
                    if data==b'ping':
                        secure.sendall(b'pong')
                    else:
                        errors.append('unexpected payload '+repr(data))
            except (ssl.SSLError,ConnectionError):
                peer.close()  # expected rejection controls; client requires every valid exchange
        except OSError as exc:
            if not stop.is_set():
                errors.append(str(exc))

threading.Thread(target=serve,daemon=True).start()
try:
    for mode,flags in [('debug',[]),('release',['-dRELEASE','-O2'])]:
        build=out/mode
        build.mkdir()
        paths = [a.overlay] if a.overlay else []
        paths += [library/n for n in ('core','lib','net','crypt')]
        command([a.compiler,*flags,'-dFORCE_OPENSSL',*['-Fu'+str(x) for x in paths],
                 '-Fi'+str(library/'lib'),'-Fi'+str(library),'-FU'+str(build),'-FE'+str(build),
                 here/'trust_probe.dpr'],build/'build.log')
        exe=build/('trust_probe.exe' if os.name=='nt' else 'trust_probe')
        base=[exe,fixtures,a.openssl_dir or '-']
        for name,extra,marker in [('contract',[],b'TRUST_CACHE_CONTRACT_PASS'),
                                 ('handshake',[port,a.count],b'TRUST_CACHE_HANDSHAKE_PASS')]:
            result=command([*base,*extra],build/(name+'.log'),env,60)
            if marker not in result:
                raise RuntimeError(f'missing {marker!r}: {build/name}')
            print(mode,name,'PASS',flush=True)
    if errors:
        raise RuntimeError(errors)
finally:
    stop.set()
    listener.close()
print('TLS_TRUST_CACHE_PASS')
