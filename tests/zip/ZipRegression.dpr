program ZipRegression;

{$APPTYPE CONSOLE}
{$I mormot.defines.inc}

uses
  SysUtils, Classes,
  mormot.core.base, mormot.core.zip;

type
  TFailFilter = class
    Seen: integer;
    Fail: boolean;
    function Keep(const Entry: TZipReadEntry): boolean;
  end;

function TFailFilter.Keep(const Entry: TZipReadEntry): boolean;
begin
  inc(Seen);
  if Fail and (Seen = 2) then
    raise Exception.Create('expected filter failure');
  result := false;
end;

procedure Check(Condition: boolean; const Message: string);
begin
  if not Condition then
    raise Exception.Create(Message);
end;

function OpenZip(const FileName: string; WorkingMem: QWord; Source: TMemoryStream): TZipRead;
begin
  if WorkingMem = 0 then
  begin
    Source.LoadFromFile(FileName);
    result := TZipRead.Create(PByteArray(Source.Memory), Source.Size);
  end
  else
    result := TZipRead.Create(FileName, 0, 0, WorkingMem);
end;

procedure ReadZip(const FileName, OutputDir: string; WorkingMem: QWord);
var
  Source, Dest: TMemoryStream;
  Zip: TZipRead;
  Info: TFileInfoFull;
  Data: RawByteString;
  Names: TStringList;
  i, DiskEntries: integer;
begin
  Source := TMemoryStream.Create;
  Names := TStringList.Create;
  try
    Zip := OpenZip(FileName, WorkingMem, Source);
    try
      DiskEntries := 0;
      for i := 0 to Zip.Count - 1 do
      begin
        Names.Add(Zip.Entry[i].zipName);
        if Zip.Entry[i].local = nil then
          inc(DiskEntries);
        Check(Zip.RetrieveFileInfo(i, Info), 'RetrieveFileInfo: ' + IntToStr(i));
        Data := Zip.UnZip(i);
        Check(QWord(length(Data)) = Info.f64.zfullSize, 'UnZip size: ' + IntToStr(i));
        Dest := TMemoryStream.Create;
        try
          if Data <> '' then
            Dest.WriteBuffer(pointer(Data)^, length(Data));
          Dest.SaveToFile(OutputDir + IntToStr(i) + '.bytes');
          Dest.Clear;
          Check(Zip.UnZip(i, Dest), 'UnZip stream: ' + IntToStr(i));
          Check(QWord(Dest.Size) = Info.f64.zfullSize, 'UnZip stream size: ' + IntToStr(i));
          Dest.SaveToFile(OutputDir + IntToStr(i) + '.stream');
        finally
          Dest.Free;
        end;
      end;
      Check(Zip.TestAll, 'TestAll');
      Names.SaveToFile(OutputDir + 'names.txt');
      Writeln('READ_PASS count=', Zip.Count, ' disk_entries=', DiskEntries);
    finally
      Zip.Free;
    end;
  finally
    Names.Free;
    Source.Free;
  end;
end;

procedure CopyZip(const FileName, OutputFile: string; WorkingMem: QWord);
var
  Source: TMemoryStream;
  Reader: TZipRead;
  Writer: TZipWrite;
  i: integer;
begin
  Source := TMemoryStream.Create;
  try
    Reader := OpenZip(FileName, WorkingMem, Source);
    try
      Writer := TZipWrite.Create(OutputFile);
      try
        for i := 0 to Reader.Count - 1 do
          Writer.AddFromZip(Reader, i);
      finally
        Writer.Free;
      end;
    finally
      Reader.Free;
    end;
  finally
    Source.Free;
  end;
  Writeln('COPY_PASS');
end;

procedure FailFilter(const FileName: string; WorkingMem: QWord);
var
  Filter: TFailFilter;
  Writer: TZipWrite;
  Failed: boolean;
begin
  Filter := TFailFilter.Create;
  try
    Filter.Fail := true;
    Failed := false;
    try
      Writer := TZipWrite.CreateFrom(FileName, WorkingMem, Filter.Keep);
      Writer.Free;
    except
      on E: Exception do
      begin
        Check(E.Message = 'expected filter failure', E.ClassName + ': ' + E.Message);
        Failed := true;
      end;
    end;
    Check(Failed, 'filter exception not propagated');
  finally
    Filter.Free;
  end;
  Writeln('FILTER_FAILURE_PASS');
end;

procedure UpdateZip(const Action, FileName, IgnoreName: string; WorkingMem: QWord);
var
  Zip: TZipWrite;
  Filter: TFailFilter;
  Data: RawByteString;
begin
  Filter := TFailFilter.Create;
  try
    if (Action = 'append') or (Action = 'noop') then
      Zip := TZipWrite.CreateFrom(FileName, WorkingMem)
    else if Action = 'delete-all' then
      Zip := TZipWrite.CreateFrom(FileName, WorkingMem, Filter.Keep)
    else
      Zip := TZipWrite.CreateFromIgnore(FileName, [IgnoreName], WorkingMem);
    try
      if Action = 'append' then
      begin
        Data := 'added by TZipWrite';
        Zip.AddDeflated('appended.txt', pointer(Data), length(Data));
      end;
    finally
      Zip.Free;
    end;
  finally
    Filter.Free;
  end;
  Writeln('UPDATE_PASS');
end;

begin
  try
    Check(ParamCount = 4, 'Usage: ZipRegression ACTION ZIP WorkingMem OutputDir|File|IgnoreName');
    if ParamStr(1) = 'read' then
      ReadZip(ParamStr(2), IncludeTrailingPathDelimiter(ParamStr(4)), StrToInt64(ParamStr(3)))
    else if ParamStr(1) = 'copy' then
      CopyZip(ParamStr(2), ParamStr(4), StrToInt64(ParamStr(3)))
    else if ParamStr(1) = 'fail-filter' then
      FailFilter(ParamStr(2), StrToInt64(ParamStr(3)))
    else
    begin
      Check((ParamStr(1) = 'append') or (ParamStr(1) = 'noop') or
        (ParamStr(1) = 'delete') or (ParamStr(1) = 'delete-all'), 'Unknown action');
      UpdateZip(ParamStr(1), ParamStr(2), ParamStr(4), StrToInt64(ParamStr(3)));
    end;
  except
    on E: Exception do
    begin
      Writeln('FAIL ', E.ClassName, ': ', E.Message);
      Halt(1);
    end;
  end;
end.
