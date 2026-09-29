# Explicit ZLIBRTL with stock FPC

`ZLIBRTL` is an existing mORMot selection. With stock FPC it uses FPC's
`zlib` unit. `MOONCOMPILER_SYSTEM_ZLIB` is defined only by MoonCompiler and
selects its `System.ZLib` instead.

Build `StockFPC.dpr` with stock FPC 3.2.2 and `-dZLIBRTL`, adding this
repository's `core`, `lib`, and `static` directories to the unit path. Run the
result: it must print `ZLIBRTL PASS`. Use a fresh output directory and `-B`
so a PPU made by another compiler cannot hide a wrong selection. Before the
MoonCompiler selection was restricted to its own macro, this build failed at
`mormot.lib.z.pas` with `Can't find unit System.ZLib`.
