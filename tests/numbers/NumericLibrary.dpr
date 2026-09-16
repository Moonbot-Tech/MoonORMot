library NumericLibrary;

{$I ../../mormot.defines.inc}

uses
  SysUtils, Math, Variants,
  mormot.core.base, mormot.core.data, mormot.core.variants;

type
  TStringParser = function(P: PUtf8Char; out Error: integer): TSynExtended;
  TJsonParser = function(P: PUtf8Char; var Value: TVarData; AllowDouble: boolean): PUtf8Char;
  TBoundedParser = function(P, PEnd: PUtf8Char): PtrInt;
  TTextParser = function(P: PUtf8Char): PtrInt;
  TCheckedParser = function(P: PUtf8Char; var Error: integer): PtrInt;

function ParserAddress(Mode: integer): pointer; cdecl;
var
  S: TStringParser;
  J: TJsonParser;
  B: TBoundedParser;
  T: TTextParser;
  K: TCheckedParser;
begin
  S := mormot.core.base.GetExtended;
  J := GetNumericVariantFromJson;
  B := GetInteger;
  T := GetInteger;
  K := GetInteger;
  case Mode of
    0: result := @S;
    1: result := @J;
    2: result := @B;
    3: result := @T;
    4: result := @K;
    {$if defined(ASMX64) and (defined(WIN64ABI) or not defined(ISDELPHI))}
    5: result := @NumberSimdData;
    {$ifend}
  else
    result := nil;
  end;
end;

function SetSimd(Value: integer): integer; cdecl;
begin
  {$if defined(ASMX64) and (defined(WIN64ABI) or not defined(ISDELPHI))}
  result := ord(NumberNoSimd = 0);
  NumberNoSimd := ord(Value = 0) shl 12;
  {$else}
  result := 0;
  {$ifend}
end;

function SetRounding(Mode: integer): integer; cdecl;
begin
  result := ord(SetRoundMode(TFpuRoundingMode(Mode)));
end;

function CheckDocument: integer; cdecl;
var
  V: variant;
  D: PDocVariantData;
  P: PVarData;
begin
  result := 1;
  if not _Json('{"price":228518839.20000000,"id":9223372036854775807,"qty":0.0000000000000000077,"next":7}',
    V, [dvoReturnNullForUnknownProperty, dvoNameCaseSensitive, dvoAllowDoubleValue]) then
    exit;
  if not _Safe(V, D) then
    exit;
  P := D.GetVarData('id');
  if (P = nil) or (P^.VType <> varInt64) or (P^.VInt64 <> High(Int64)) then
    exit;
  P := D.GetVarData('price');
  if (P = nil) or (P^.VType <> varDouble) or (P^.VDouble <> 228518839.2) then
    exit;
  if D^.I['next'] <> 7 then
    exit;
  if not boolean(V.Exists('price')) or (integer(V.NameIndex('next')) <> 3) then
    exit;
  V.Clear;
  if D^.Count <> 0 then
    exit;
  result := 0;
end;

exports
  ParserAddress, SetSimd, SetRounding, CheckDocument;

begin
  SetRoundMode(rmNearest);
  SetExceptionMask([exInvalidOp, exDenormalized, exZeroDivide, exOverflow, exUnderflow, exPrecision]);
end.
