program FallbackSmoke;

{$APPTYPE CONSOLE}
{$I ../../mormot.defines.inc}

uses
  SysUtils, Math, Variants,
  mormot.core.base, mormot.core.variants;

var
  Checks: integer;

const
  Price: double = 228518839.2;

procedure Check(Condition: boolean);
begin
  inc(Checks);
  if not Condition then
    raise Exception.CreateFmt('Numeric fallback regression at check %d', [Checks]);
end;

procedure Number(const Text: RawUtf8; Expected: double);
var
  Error: integer;
  Value: double;
begin
  Value := GetExtended(pointer(Text), Error);
  Check((Error = 0) and (Value = Expected));
end;

var
  Error: integer;
  V: TVarData;
  P: PUtf8Char;
begin
  SetRoundMode(rmNearest);
  SetExceptionMask([exInvalidOp, exDenormalized, exZeroDivide, exOverflow, exUnderflow, exPrecision]);
  Number('10.1', 10.1);
  Number('0.0000000000000000077', 7.7e-18);
  Number('228518839.20000000', 228518839.2);
  Number('0e3', 0); // the legacy Win32 x87 entry has its own exponent limits
  Check(GetInteger('123456789', Error) = 123456789);
  Check(Error = 0);
  Check(GetInteger('-123456789', Error) = -123456789);
  Check(Error = 0);
  GetInteger('123x', Error);
  Check(Error <> 0);
  P := '12345';
  Check(GetInteger(P, P + 3) = 123);
  Check(GetInteger(' - 123') = -123);
  FillChar(V, SizeOf(V), 0);
  P := '228518839.20000000,';
  Check(GetNumericVariantFromJson(P, V, true) = P + 18);
  Check((V.VType = varDouble) and (V.VDouble = Price));
  Check(GetNumericVariantFromJson('9223372036854775807', V, true) <> nil);
  Check((V.VType = varInt64) and (V.VInt64 = High(Int64)));
  Check(GetNumericVariantFromJson('-9223372036854775808', V, true) <> nil);
  Check((V.VType = varInt64) and (V.VInt64 = Low(Int64)));
  Check(GetNumericVariantFromJson('01', V, true) = nil);
  Check(GetNumericVariantFromJson('1.5', V, false) = nil);
  Check(GetNumericVariantFromJson('0e9999', V, true) <> nil);
  Check((V.VType = varDouble) and (V.VDouble = 0));
  Writeln('NUMERIC_FALLBACK_PASS ', SizeOf(pointer) * 8);
end.
