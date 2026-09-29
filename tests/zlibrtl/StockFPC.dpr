program StockFPC;

{$mode delphi}

uses
  mormot.lib.z;

var
  Source, Restored: array[0..127] of Byte;
  Compressed: array[0..255] of Byte;
  Z: TZLib;
  CompressedSize, I: Integer;
begin
  for I := Low(Source) to High(Source) do
    Source[I] := I mod 7;

  Z.Init(@Source[0], @Compressed[0], SizeOf(Source), SizeOf(Compressed));
  If not Z.CompressInit(6, True) then Halt(1);
  try
    If Z.Compress(Z_FINISH) <> Z_STREAM_END then Halt(2);
    CompressedSize := Z.Stream.total_out;
  finally
    Z.CompressEnd;
  end;

  Z.Init(@Compressed[0], @Restored[0], CompressedSize, SizeOf(Restored));
  If not Z.UncompressInit(True) then Halt(3);
  try
    If Z.Uncompress(Z_FINISH) <> Z_STREAM_END then Halt(4);
  finally
    Z.UncompressEnd;
  end;
  for I := Low(Source) to High(Source) do
    If Source[I] <> Restored[I] then Halt(5);
  Writeln('ZLIBRTL PASS');
end.
