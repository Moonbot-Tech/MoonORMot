# Third-Party Notices

This file records the origin and redistribution terms of code and prebuilt static-link inputs contained in this repository. It does not replace or narrow the original licenses.

## Synopse mORMot 2

The repository is derived from Synopse mORMot 2, copyright Arnaud Bouchez and Synopse Informatique. The framework source retains its original disjunctive MPL 1.1/GPL 2.0/LGPL 2.1 license with the FPC modified-LGPL linking exception. See [`LICENSE.md`](LICENSE.md) and the per-file headers.

## Binary inventory

Exact hashes for every binary below are recorded in [`static/MANIFEST.sha256`](static/MANIFEST.sha256). The corresponding reference sources and build recipes are preserved in the pinned upstream [`res/static`](https://github.com/synopse/mORMot2/tree/38874e16c03373a5275b959fdb1cc38d5597f67f/res/static) tree.

| Files | Component | Origin and license |
|---|---|---|
| `static/delphi/zlib*.obj`, `static/delphi/deflate.obj`, `static/x86_64-win64/{adler32,crc32,deflate,inffast,inflate,inftrees,trees,zutil}.o` | zlib | Jean-loup Gailly and Mark Adler; zlib License |
| `static/*/libdeflatepas.a` | libdeflate | Eric Biggers; MIT License |
| `static/*/liblizard.a`, `static/delphi/lizard1-*.dll` | Lizard | Yann Collet and Przemyslaw Skibinski; BSD 2-Clause License |
| `static/*/quickjs.o`, `static/delphi/quickjs.obj` | QuickJS with the mORMot static-build adaptations | Fabrice Bellard and Charlie Gordon; MIT License; adapted from the `c-smile/quickjspp` line by Synopse |
| `static/*/sqlite3.o`, `static/delphi/sqlite3.obj`, `static/delphi/sqlite3-64.dll`, `static/x86_64-win64/libsqlite3-64.a` | SQLite 3.46.1 with mORMot/SQLite3MultipleCiphers integration | SQLite public domain dedication; SQLite3MultipleCiphers integration by Ulrich Telle under the MIT License; mORMot wrappers under the framework license |
| `static/*/crc32c64.*`, `static/*/sha512-x64sse4.*` | Intel optimized CRC32C and SHA-512 routines | Intel Corporation; permissive BSD-style open-source notices retained in `mormot.crypt.core` |
| `static/delphi/sha512-x86.obj` | SHA-512 x86 implementation | Project Nayuki; MIT License |
| `static/x86_64-win64/libkernel32.a` | Win64 import library | Generated import metadata for Windows system APIs; no Windows implementation code is embedded |

## License notices

### zlib

Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler.

This software is provided "as-is", without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to these restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

### MIT-licensed components

The MIT terms apply separately to libdeflate, QuickJS, Project Nayuki's SHA-512 implementation, and the SQLite3MultipleCiphers integration, with their respective copyright notices:

- Copyright 2016 Eric Biggers;
- Copyright 2017-2021 Fabrice Bellard and Charlie Gordon;
- Copyright Project Nayuki;
- Copyright 2006-2020 Ulrich Telle.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### Lizard BSD 2-Clause notice

Copyright (C) 2011-2015 Yann Collet.

Copyright (C) 2016-2017 Przemyslaw Skibinski.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Intel optimized routines

Copyright (C) 2011-2015 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the original copyright notice, conditions and disclaimer are retained; binary redistributions must reproduce them in accompanying documentation; and neither Intel's name nor contributor names may be used to endorse derived products without prior written permission.

THE SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT, INCLUDING NEGLIGENCE, ARISING FROM THE USE OF THIS SOFTWARE.

### SQLite

SQLite is dedicated to the public domain by its authors. See https://www.sqlite.org/copyright.html. The optional encryption/VFS integration is not part of SQLite itself and retains the notices described above.
