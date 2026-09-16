/// Internal shared data for the complete x86-64 numeric parsers
// - this unit is a part of the Open Source Synopse mORMot framework 2,
// licensed under a MPL/GPL/LGPL three license - see LICENSE.md
unit mormot.core.base.asmx64.number;

interface

{$I ..\mormot.defines.inc}

{$if defined(ASMX64) and (defined(WIN64ABI) or not defined(ISDELPHI))}

// Implementation-only symbols shared by mormot.core.base and mormot.core.variants.
// Application and qualification code should call the public parsers as black boxes.
procedure NumberSimdData;
var
  // OR-ed into the page offset: 4096 selects the SSE2 byte path without an extra branch.
  NumberNoSimd: cardinal = 4096;
const
  NUMBER_BIAS = 0;
  NUMBER_THRESHOLD = 16;
  NUMBER_ASCII0 = 32;
  NUMBER_TEN = 48;
  NUMBER_HUNDRED = 64;
  NUMBER_TENTHOUSAND = 80;
  NUMBER_SIGN = 96;
  NUMBER_E8 = 104;
  NUMBER_CTRLINT = 96;               // rows 1..15 at 112..351 (row 0 overlaps the sign row, never read)
  NUMBER_CTRLDOT = 336;              // rows 1..15 at 352..591 (row 0 overlaps ctrlInt row 15, never read)
  NUMBER_CTRLSHORT = 576;            // rows 1..8 at 592..719 (row 0 overlaps ctrlDot row 15, never read)
  NUMBER_POWERS = 720;               // 10^0 .. 10^15 at 720..847
  NUMBER_SCALE = 848;                // 2^512
  NUMBER_INVSCALE = 856;             // 2^-512
  NUMBER_MAXSCALED = 864;            // MaxDouble * 2^-512
  NUMBER_MINBITS = 872;              // the smallest subnormal
  NUMBER_OVERFLOW = 880;             // floor of the overflow midpoint / 10^289
  NUMBER_UNDERFLOW = 888;             // floor of the underflow midpoint / 10^-342
  NUMBER_EXACTINTEGER = 896;         // 2^53 as a double
  NUMBER_INV5 = 904;                 // inverse of 5 modulo 2^64 (the JSON routine's strip test)
  NUMBER_STRIPLIMIT = 912;           // floor((2^64 - 1) / 10)

{$ifend}

implementation

{$if defined(ASMX64) and (defined(WIN64ABI) or not defined(ISDELPHI))}
{$I mormot.core.base.asmx64.number.data.inc}
{$ifend}

end.
