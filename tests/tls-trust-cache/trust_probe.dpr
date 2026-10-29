program trust_probe;
{$APPTYPE CONSOLE}
{$ifdef FPC}{$mode delphiunicode}{$endif}{$H+}
{$ifdef MSWINDOWS}{$define WINDOWS}{$endif}
uses
  {$ifdef UNIX}cthreads, cwstring,{$endif}
  {$ifdef WINDOWS}Windows,{$endif}
  SysUtils, Classes, mormot.core.base, mormot.core.os, mormot.net.sock, mormot.lib.openssl11;

{$ifdef UNIX}
function SetEnv(Name, Value: PAnsiChar; Replace: LongInt): LongInt; cdecl; external 'c' name 'setenv';
{$endif}

procedure Check(Value: Boolean; const Context: string);
begin
  If not Value then begin
    Writeln('FAIL ',Context);
    Halt(1);
  end;
end;

procedure Env(const Name, Value: string);
{$ifdef UNIX}var N,V: UTF8String;{$endif}
begin
  {$ifdef WINDOWS}
  Check(Windows.SetEnvironmentVariable(PChar(Name),PChar(Value)),'set env');
  {$else}
  N:=UTF8Encode(Name);
  V:=UTF8Encode(Value);
  Check(SetEnv(PAnsiChar(N),PAnsiChar(V),1)=0,'set env');
  {$endif}
end;

procedure CopyFileContent(const Source, Dest: string);
var A,B: TFileStream;
begin
  A:=TFileStream.Create(Source,fmOpenRead or fmShareDenyNone);
  try
    B:=TFileStream.Create(Dest,fmCreate);
    try
      B.CopyFrom(A,0);
    finally
      B.Free;
    end;
  finally
    A.Free;
  end;
end;

function Certificate(const Path: string): PX509;
var Stream: TFileStream; Data: RawByteString; Bio: PBIO;
begin
  Stream:=TFileStream.Create(Path,fmOpenRead or fmShareDenyNone);
  try
    SetLength(Data,Stream.Size);
    Stream.ReadBuffer(Data[1],Length(Data));
  finally
    Stream.Free;
  end;
  Bio:=BIO_new_mem_buf(Pointer(Data),Length(Data));
  Check(Bio<>nil,'BIO');
  try
    Result:=PEM_read_bio_X509(Bio,nil,nil,nil);
    Check(Result<>nil,'read cert');
  finally
    BIO_free(Bio);
  end;
end;

function Context(Age: Cardinal = 3600000): PSSL_CTX;
begin
  Result:=SSL_CTX_new(TLS_client_method);
  Check(Result<>nil,'new context');
  SSL_CTX_set_verify(Result,SSL_VERIFY_PEER,nil);
  Check(OpenSslLoadDefaultTrust(Result,Age)=1,'load trust');
end;

function Accepts(Ctx: PSSL_CTX; Cert: PX509): Boolean;
var V: PX509_STORE_CTX;
begin
  V:=X509_STORE_CTX_new;
  Check(V<>nil,'verify context');
  try
    Check(X509_STORE_CTX_init(V,SSL_CTX_get_cert_store(Ctx),Cert,nil)=1,'verify init');
    Result:=X509_verify_cert(V)=1;
  finally
    X509_STORE_CTX_free(V);
    While ERR_get_error<>0 do begin end;
  end;
end;

type
  TClient = class(TThread)
    procedure Execute; override;
  end;
var RootA,RootB: PX509;

procedure TClient.Execute;
var I: Integer; C: PSSL_CTX;
begin
  For I:=1 to 100 do begin
    If I mod 11=0 then
      OpenSslClearDefaultTrust;
    C:=Context;
    try
      Check(Accepts(C,RootB) and not Accepts(C,RootA),'concurrent trust');
    finally
      SSL_CTX_free(C);
    end;
  end;
end;

procedure Contract(const Directory: string);
var Current: string; A,B,C,D: PSSL_CTX; Workers: array[0..7] of TClient; I: Integer;
begin
  Current:=Directory+'/current.pem';
  Env('SSL_CERT_FILE',Current);
  Env('SSL_CERT_DIR',Directory+'/empty');
  CopyFileContent(Directory+'/root-a.pem',Current);
  RootA:=Certificate(Directory+'/root-a.pem');
  RootB:=Certificate(Directory+'/root-b.pem');
  OpenSslClearDefaultTrust;
  A:=Context;
  B:=Context;
  Check(SSL_CTX_get_cert_store(A)=SSL_CTX_get_cert_store(B),'cache hit');
  Check(Accepts(A,RootA) and not Accepts(A,RootB),'initial trust');
  CopyFileContent(Directory+'/root-b.pem',Current);
  C:=Context;
  Check(Accepts(C,RootA) and not Accepts(C,RootB),'documented cache lifetime');
  SSL_CTX_free(C);
  C:=Context(0);
  Check(Accepts(C,RootB) and not Accepts(C,RootA),'bypass reads new file');
  SSL_CTX_free(C);
  SysUtils.Sleep(50);
  C:=Context(20);
  Check(Accepts(C,RootB) and not Accepts(C,RootA),'expiry replaces trust');
  Check(Accepts(A,RootA) and Accepts(B,RootA),'old contexts retain valid refs');
  OpenSslClearDefaultTrust;
  D:=Context;
  Check(SSL_CTX_get_cert_store(C)<>SSL_CTX_get_cert_store(D),'explicit clear');
  Check(Accepts(C,RootB) and Accepts(D,RootB),'clear preserves live contexts');
  SSL_CTX_free(A);
  SSL_CTX_free(B);
  SSL_CTX_free(C);
  SSL_CTX_free(D);
  For I:=0 to High(Workers) do
    Workers[I]:=TClient.Create(False);
  For I:=0 to High(Workers) do begin
    Workers[I].WaitFor;
    Check(Workers[I].FatalException=nil,'worker exception');
    Workers[I].Free;
  end;
  OpenSslClearDefaultTrust;
  X509_free(RootA);
  X509_free(RootB);
  Writeln('TRUST_CACHE_CONTRACT_PASS');
end;

procedure Connect(const Port, Host, CAFile: RawUtf8; Expected: Boolean);
var Socket: TCrtSocket; Options: TNetTlsContext; Accepted: Boolean;
begin
  Options := Default(TNetTlsContext);
  Options.HostNamesCsv := Host;
  Options.CACertificatesFile := CAFile;
  Accepted := False;
  try
    Socket := TCrtSocket.Open('127.0.0.1', Port, nlTcp, 2000, True, @Options);
    try
      Socket.SockSendFlush('ping');
      Check(Socket.SockRecv(4) = 'pong', 'TLS payload');
      Accepted := True;
    finally
      Socket.Free;
    end;
  except
    on E: ENetSock do
      Writeln('TLS_REFUSED ', E.Message);
  end;
  Check(Accepted = Expected, 'handshake ' + string(Host) + ' / ' + string(CAFile));
end;

procedure Handshake(const Directory, Port: string);
var I, Count: Integer; Start, Stop: Int64;
begin
  Count := StrToIntDef(ParamStr(4), 100);
  Check(Count > 0, 'connection count');
  CopyFileContent(Directory+'/bundle-a.pem', Directory+'/current.pem');
  OpenSslClearDefaultTrust;
  Connect(RawUtf8(Port), 'localhost', '', True);
  Connect(RawUtf8(Port), 'wrong.invalid', '', False);
  Connect(RawUtf8(Port), 'localhost', RawUtf8(Directory+'/root-b.pem'), False);
  CopyFileContent(Directory+'/bundle-b.pem', Directory+'/current.pem');
  Connect(RawUtf8(Port), 'localhost', '', True);
  OpenSslClearDefaultTrust;
  Connect(RawUtf8(Port), 'localhost', '', False);
  Connect(RawUtf8(Port), 'localhost', RawUtf8(Directory+'/root-a.pem'), True);
  CopyFileContent(Directory+'/bundle-a.pem', Directory+'/current.pem');
  OpenSslClearDefaultTrust;
  For I := 1 to 2 do begin
    QueryPerformanceMicroSeconds(Start);
    For var N := 1 to Count do begin
      If I = 1 then
        OpenSslClearDefaultTrust;
      Connect(RawUtf8(Port), 'localhost', '', True);
    end;
    QueryPerformanceMicroSeconds(Stop);
    Writeln('TLS_HANDSHAKES mode=', I, ' count=', Count, ' us=', Stop-Start);
  end;
  OpenSslClearDefaultTrust;
  Writeln('TRUST_CACHE_HANDSHAKE_PASS');
end;

begin
  Env('SSL_CERT_FILE',ParamStr(1)+'/current.pem');
  Env('SSL_CERT_DIR',ParamStr(1)+'/empty');
  {$ifdef WINDOWS}
  Check(OpenSslInitialize(ParamStr(2)+'/libcrypto-3.dll',ParamStr(2)+'/libssl-3.dll'),'OpenSSL load');
  {$else}
  Check(OpenSslInitialize,'OpenSSL load');
  {$endif}
  If ParamCount >= 3 then
    Handshake(ParamStr(1), ParamStr(3))
  else
    Contract(ParamStr(1));
end.
