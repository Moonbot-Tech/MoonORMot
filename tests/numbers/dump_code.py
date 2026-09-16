"""Read the actual entry addresses and bytes for the layout checker (stdlib only, both ABIs)."""
import ctypes as C, json, sys
from numeric_api import Library

lib = Library(sys.argv[1])
names = ('string', 'json', 'intb', 'intt', 'intc')
print(json.dumps(dict(routines={name: dict(address=p, code=C.string_at(p, 8192).hex())
    for name, p in zip(names, lib.addresses)},
    table=dict(address=lib.lib.ParserAddress(5), bytes=C.string_at(lib.lib.ParserAddress(5), 920).hex()))))
