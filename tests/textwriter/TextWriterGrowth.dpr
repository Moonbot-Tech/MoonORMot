program TextWriterGrowth;

uses SysUtils, Classes, mormot.core.text;

type
  TCountingStream = class(TStream)
  public
    BytesWritten: Int64;
    BadData: Boolean;
    function Write(const Buffer; Count: LongInt): LongInt; override;
    function Seek(const Offset: Int64; Origin: TSeekOrigin): Int64; override;
  end;

  TProbeWriter = class(TTextWriter)
  public
    function BufferPointer: Pointer;
    function BufferSize: Integer;
    function ExternalBuffer: Boolean;
  end;

var
  Original, Hooked: TMemoryManager;
  OldBuffer: Pointer;
  FailureSize: PtrUInt;
  Armed, FreedOld: Boolean;
  FailureHits: Integer;

function HookGetMem(Size: PtrUInt): Pointer;
begin
  If Armed and (Size = FailureSize) then begin
    Armed := False;
    Inc(FailureHits);
    // Exercise the installed allocator's real nil/exception policy without
    // exhausting physical memory. Its normal allocation function stays intact.
    Exit(Original.GetMem(PtrUInt(1) shl 60));
  end;
  Result := Original.GetMem(Size);
end;

function HookFreeMem(P: Pointer): PtrUInt;
begin
  If P = OldBuffer then
    FreedOld := True;
  Result := Original.FreeMem(P);
end;

function TCountingStream.Write(const Buffer; Count: LongInt): LongInt;
var
  I: Integer;
begin
  for I := 0 to Count - 1 do
    If PByte(@Buffer)[I] <> Ord('A') then
      BadData := True;
  Inc(BytesWritten, Count);
  Result := Count;
end;

function TCountingStream.Seek(const Offset: Int64; Origin: TSeekOrigin): Int64;
begin
  If (Offset <> 0) or (Origin <> soCurrent) then
    raise Exception.Create('Only querying the current position is supported');
  Result := BytesWritten;
end;

function TProbeWriter.BufferPointer: Pointer;
begin
  Result := fTempBuf;
end;

function TProbeWriter.BufferSize: Integer;
begin
  Result := fTempBufSize;
end;

function TProbeWriter.ExternalBuffer: Boolean;
begin
  Result := twoBufferIsExternal in fCustomOptions;
end;

var
  Stream: TCountingStream;
  Writer: TProbeWriter;
  ExternalStorage: array[0..65535] of Byte;
  Chunk: array[0..131071] of Byte;
  IsExternal, IsLarge, ExpectRaise, NoFailure, Raised, Preserved, Good: Boolean;
  OldEnd: Pointer;
  Size, I, ErrorCode: Integer;
  ExpectedBytes: Int64;
begin
  If (ParamCount <> 3) or
    ((ParamStr(1) <> 'owned') and (ParamStr(1) <> 'external')) or
    ((ParamStr(2) <> 'small') and (ParamStr(2) <> 'large')) or
    ((ParamStr(3) <> 'nil') and (ParamStr(3) <> 'raise') and (ParamStr(3) <> 'success')) then begin
    WriteLn('Usage: TextWriterGrowth owned|external small|large nil|raise|success');
    Halt(2);
  end;
  IsExternal := ParamStr(1) = 'external';
  IsLarge := ParamStr(2) = 'large';
  ExpectRaise := ParamStr(3) = 'raise';
  NoFailure := ParamStr(3) = 'success';
  ReturnNilIfGrowHeapFails := not ExpectRaise;
  FillChar(Chunk, SizeOf(Chunk), Ord('A'));
  If IsLarge then begin
    Size := 65536;
    FailureSize := 1 shl 20;
  end else begin
    Size := 256;
    FailureSize := 512;
  end;
  Stream := TCountingStream.Create;
  If IsExternal then
    Writer := TProbeWriter.Create(Stream, @ExternalStorage, Size)
  else
    Writer := TProbeWriter.Create(Stream, Size);
  try
    // Reach each growth threshold using public writer operations, not edits
    // to protected counters. The output stream allocates nothing.
    If IsLarge then begin
      for I := 1 to 320 do
        Writer.AddNoJsonEscapeBig(@Chunk, SizeOf(Chunk));
    end else begin
      for I := 1 to 8 do begin
        Writer.AddNoJsonEscape(@Chunk, 128);
        Writer.FlushFinal;
      end;
    end;
    ExpectedBytes := Stream.BytesWritten + 1;
    Writer.Add(AnsiChar('A'));
    OldBuffer := Writer.BufferPointer;
    OldEnd := Writer.BEnd;
    GetMemoryManager(Original);
    Hooked := Original;
    Hooked.GetMem := @HookGetMem;
    Hooked.FreeMem := @HookFreeMem;
    Armed := not NoFailure;
    SetMemoryManager(Hooked);
    try
      try
        Writer.FlushToStream;
      except
        on E: EOutOfMemory do
          Raised := True;
      end;
    finally
      SetMemoryManager(Original);
      Armed := False;
    end;
    Preserved := (Writer.BufferPointer = OldBuffer) and
      (Writer.BufferSize = Size) and (Writer.BEnd = OldEnd) and
      (Writer.ExternalBuffer = IsExternal) and not FreedOld;
    WriteLn('ownership=', ParamStr(1), ' growth=', ParamStr(2), ' mode=', ParamStr(3),
      ' hits=', FailureHits, ' raised=', Raised, ' freed_old=', FreedOld,
      ' old_ptr=', Writer.BufferPointer = OldBuffer, ' nil_ptr=', Writer.BufferPointer = nil,
      ' size=', Writer.BufferSize, ' external=', Writer.ExternalBuffer,
      ' flushed=', Stream.BytesWritten = ExpectedBytes, ' preserved=', Preserved);
    Good := not Stream.BadData and (Stream.BytesWritten = ExpectedBytes) and
      (Raised = ExpectRaise);
    If NoFailure then
      Good := Good and (FailureHits = 0) and (Writer.BufferPointer <> nil) and
        (Writer.BufferPointer <> OldBuffer) and (Writer.BufferSize = Integer(FailureSize)) and
        not Writer.ExternalBuffer and (FreedOld = not IsExternal)
    else
      Good := Good and (FailureHits = 1) and Preserved;
    If Good then begin
      Writer.Add(AnsiChar('A'));
      Writer.FlushFinal;
      Good := not Stream.BadData and (Stream.BytesWritten = ExpectedBytes + 1);
      WriteLn('continued_correctly=', Good);
    end;
    If not Good then
      ErrorCode := 3;
  finally
    ReturnNilIfGrowHeapFails := False;
    Writer.Free;
    Stream.Free;
  end;
  Halt(ErrorCode);
end.
