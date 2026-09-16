#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
# Use a complete MoonCompiler installation: its matching RTL/package PPUs are part of the ABI.
: "${MOON_HOME:?set MOON_HOME to a MoonCompiler installation (bin/ and lib/fpc/)}"
compiler="$MOON_HOME/lib/fpc/3.3.1/ppcx64"
units="$MOON_HOME/lib/fpc/3.3.1/units/x86_64-linux"
options=(-n -Mdelphi -Tlinux -Px86_64 -B -dMOONCOMPILER_VANILLA_RUNTIME -dNOPATCHRTL)
for unit in "$units"/*; do options+=("-Fu$unit"); done
options+=("-Fl$(dirname "$(gcc -print-libgcc-file-name)")" -Fl/usr/lib/x86_64-linux-gnu -Fu../../core)
mode="${1:-release}"
if [[ "$mode" == debug ]]; then
  options+=(-O- -dDEBUG -gl -Crtoi)
else
  options+=(-O3 -gl)
fi
if [[ "${2:-}" == unicode ]]; then
  options+=(-dUNICODERTL -dENABLE_DELPHI_RTTI -dMOONCOMPILER_DELPHI_CALLBACK_TYPES)
  mode="$mode-unicode"
fi
mkdir -p "dcu-$mode" "dcu-$mode-sse2" bin
"$compiler" "${options[@]}" -FEbin "-FUdcu-$mode" "-olibNumericLibrary-$mode.so" NumericLibrary.dpr
"$compiler" "${options[@]}" -dMORMOT_NUMERIC_FORCE_SSE2 -FEbin "-FUdcu-$mode-sse2" "-olibNumericLibrary-$mode-sse2.so" NumericLibrary.dpr
sha256sum "$compiler" "bin/libNumericLibrary-$mode.so" "bin/libNumericLibrary-$mode-sse2.so"
