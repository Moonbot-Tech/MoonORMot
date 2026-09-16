# Numeric ASM layout

The x86-64 entries are `GetExtended(P, err)`, `GetNumericVariantFromJson` and the
three native-width `GetInteger` overloads. Each entry performs scanning and
conversion itself, including its SSE2 byte continuation. `NumberSimdData` is one
920-byte table, aligned as a procedure and never executed. The existing `POW10`
table is shared with other library code. Pascal parsing and `DecimalToDouble`
remain under the complementary platform condition for non-ASM targets.

The value contract is nearest rounding, 15 significant digits of accuracy,
canonical decimal zeroes and equal bits across delimiters and page/SIMD paths.
Directed modes are not a value contract; JSON zero still has a positive sign.
These are the Whole parser contracts, not equivalence to every historical scanner.
The grammar, error results and bounded-work rules are beside each entry.

## Source and machine code

Edit the instruction sheets in `number-layout/`, then run `layout.py emit`.
`routine_string.py` uses `plan_hot.py` and `plan_cold.py`; the other four sheets
are self-contained. `solve.py` chooses harmless instruction alternatives, DS
prefixes and gaps after unconditional jumps/returns. `emit.py` only renders the
solved sequence. `layout.py` derives the SysV register plan and encodings from the
same sheets and emits the two ABI bodies into the includes.

Delphi entries are only guaranteed 16-byte alignment. Therefore branches, returns
and adjacent flag-operation/conditional-branch pairs use the stronger JCC16 rule:
they neither cross nor end on a 16-byte boundary. This also meets JCC32 at every
possible 16-byte entry phase. MoonCompiler Release places entries on 64 bytes and
inserts no bytes inside hand-written ASM. Its forward-branch relaxation differs
from Delphi; `sizes_with_targets` models both, and a nonconverging layout fails.
`encodings-sysv.json` holds measured instruction sizes, not estimated pad counts.

DS prefixes are forbidden on jumps/returns and TZCNT. The solver uses at most two
in hot regions and three in cold regions; none inside loops. Every generated NOP
is after a jump or return, outside executed fall-through. Required loop placement
is named by `HARD_LOOPS` in each sheet. Other loop placements are preferences and
the checker reports their remaining splits; it does not claim all cold loops fit.
Loop offsets are relative to the entry: their absolute 32/64-byte placement is
guaranteed only when the entry itself is 64-aligned, as on MoonCompiler Release.

The SysV plan consumes P in RDI. The second argument moves from RSI to RDX because
the converter explicitly preserves RDX around implicit MUL/DIV operations; a
blanket register substitution would corrupt those paths. JSON captures its DL
boolean before that move. All registers used are volatile for the selected ABI.

## Verification

Build `tests/numbers/NumericLibrary.dpr` with `build.cmd` (Delphi) or `build.sh`
(MoonCompiler and its matching RTL packages). The exports expose actual entry
addresses; they are not wrappers around the measured calls. Python commands from
the repository root (only the layout checker requires `capstone`):

```
python tests/numbers/verify.py tests/numbers/bin/NumericLibrary.dll --corpus LAB/data --write win64.json
python tests/numbers/dump_code.py tests/numbers/bin/NumericLibrary.dll > win64-code.json
python core/number-layout/layout.py check win64-code.json win64
python core/number-layout/layout.py source-check
```

On Linux, run `verify.py libNumericLibrary-release.so --corpus DATA --expect win64.json`
and `dump_code.py` with the Linux library. Check the dump with `layout.py check
sysv-code.json sysv`; this can run on Windows, so the host needs only Python's
standard library. Repeat runtime verification for Debug and the Unicode RTL build.

The checker compares every instruction's offset, length and mnemonic, every
branch destination, DS placement, dead gap, table alignment, required loop and
JCC16 boundary against the built code. Runtime digests contain all five APIs,
both JSON AllowVarDouble states, types, values, error flags and returned cursors.
They must agree with SIMD disabled and at a guard-page edge. The external NumberLab
adds accuracy/oracle, spelling, delimiter, consumer and balanced timing tests.

After changing a compiler, first check the actual binary. If nonbranch encodings
changed, use an unpadded build to refresh them with `layout.py measure DUMP
encodings-sysv.json`, regenerate, rebuild and check again. Do not trust the model
without that binary check. FPC Win64 and other FPC releases use the same semantics
but are not covered by the Delphi/MoonCompiler layout qualification.
