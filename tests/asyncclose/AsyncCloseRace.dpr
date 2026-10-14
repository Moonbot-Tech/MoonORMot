program AsyncCloseRace;

uses
  {$ifdef UNIX} cthreads, {$endif}
  SysUtils, Classes, SyncObjs,
  mormot.core.base, mormot.core.os, mormot.net.sock, mormot.net.async;

type
  TPauseAt = (paNone, paBeforeOnClose, paAfterOnClose, paStop, paWriterInCallback, paReentrant, paRaiseOnClose);
  TProbeConnection = class(TAsyncConnection)
  protected
    function OnRead: TPollAsyncSocketOnReadWrite; override;
    procedure OnClose; override;
  public
    procedure Arm;
  end;
  TProbeSockets = class(TPollAsyncSockets)
  protected
    function OnError(connection: TPollAsyncConnection; events: TPollSocketEvents): boolean; override;
    procedure OnClosed(connection: TPollAsyncConnection); override;
    procedure RegisterConnection(connection: TPollAsyncConnection); override;
  public
    function Stop(connection: TPollAsyncConnection; const caller: shortstring): boolean; override;
    procedure CloseProbe(connection: TPollAsyncConnection; Unlock: boolean);
  end;
  TProbeOwner = class(TAsyncConnections)
  protected
    procedure Execute; override;
  public
    function AddProbe: TProbeConnection;
    function GarbageCount: integer;
  end;
  TCloser = class(TThread)
  public
    Poll: TProbeSockets;
    Conn: TProbeConnection;
    Owner: TProbeOwner;
    Unlock: boolean;
    Done: TEvent;
    ErrorText: string;
    constructor Create(APoll: TProbeSockets; AConn: TProbeConnection;
      AOwner: TProbeOwner; AUnlock: boolean);
    destructor Destroy; override;
    procedure Execute; override;
  end;
  TLoopConnection = class(TAsyncConnection)
  protected
    function OnRead: TPollAsyncSocketOnReadWrite; override;
    function AfterWrite: TPollAsyncSocketOnReadWrite; override;
    procedure OnClose; override;
  end;

var
  Entered, ContinueClose: TEvent;
  PauseAt: TPauseAt;
  CloseCalls, StopCalls, ClosedCalls, StopsRunning, EarlyClosed, Failures: integer;
  CallbackPoll: TProbeSockets;
  CallbackOwner: TProbeOwner;
  LoopCloseAfterWrite: boolean;
  LoopClosed: integer;

procedure Check(Value: boolean; const Name: string);
begin
  if Value then
    Writeln('PASS ', Name)
  else begin
    Writeln('FAIL ', Name);
    InterlockedIncrement(Failures);
  end;
end;

procedure PauseClose;
begin
  Entered.SetEvent;
  if ContinueClose.WaitFor(5000) <> wrSignaled then
    raise Exception.Create('barrier timed out');
end;

function NewProbeSocket: TNetSocket;
begin
  if NewSocket('127.0.0.1', '0', nlTcp, true, 1000, 1000, 1000, 0, Result) <> nrOK then
    raise Exception.Create('cannot create loopback socket');
end;

procedure TProbeConnection.Arm;
begin
  fHandle := 1;
  fSocket := NewProbeSocket;
end;

function TProbeConnection.OnRead: TPollAsyncSocketOnReadWrite;
begin
  Result := soContinue;
end;

procedure TProbeConnection.OnClose;
var
  First: boolean;
begin
  First := InterlockedIncrement(CloseCalls) = 1;
  if PauseAt = paRaiseOnClose then
    raise Exception.Create('expected OnClose failure');
  if First and (PauseAt = paBeforeOnClose) then
    PauseClose;
  if First and (PauseAt = paWriterInCallback) then begin
    if not TryLock(true) then
      raise Exception.Create('callback could not acquire writer lock');
    try
      PauseClose;
    finally
      UnLock(true);
    end;
  end;
  inherited OnClose;
  if First and (PauseAt = paAfterOnClose) then
    PauseClose;
  if First and (PauseAt = paReentrant) then
    CallbackPoll.CloseProbe(Self, false);
  if CallbackOwner <> nil then
    // This callback needs the list lock. ConnectionRemove must not hold it.
    Check(not CallbackOwner.ConnectionRemove(Handle), 'reentrant remove leaves the close owner in charge');
end;

function TProbeSockets.OnError(connection: TPollAsyncConnection; events: TPollSocketEvents): boolean;
begin
  Result := false;
end;

procedure TProbeSockets.RegisterConnection(connection: TPollAsyncConnection);
begin
end;

procedure TProbeSockets.OnClosed(connection: TPollAsyncConnection);
begin
  InterlockedIncrement(ClosedCalls);
  if StopsRunning <> 0 then
    InterlockedIncrement(EarlyClosed);
end;

function TProbeSockets.Stop(connection: TPollAsyncConnection; const caller: shortstring): boolean;
var
  First: boolean;
begin
  First := InterlockedIncrement(StopCalls) = 1;
  InterlockedIncrement(StopsRunning);
  try
    Result := inherited Stop(connection, caller);
    // fSocket is nil, but this virtual Stop has not returned to the close caller.
    // This models a slow socket/TLS shutdown without relying on network timing.
    if First and (PauseAt = paStop) then
      PauseClose;
  finally
    InterlockedDecrement(StopsRunning);
  end;
end;

procedure TProbeSockets.CloseProbe(connection: TPollAsyncConnection; Unlock: boolean);
begin
  if Unlock then
    UnlockAndCloseConnection(true, connection, 'regression unlock')
  else
    CloseConnection(connection, 'regression close');
end;

constructor TCloser.Create(APoll: TProbeSockets; AConn: TProbeConnection;
  AOwner: TProbeOwner; AUnlock: boolean);
begin
  inherited Create(true);
  Poll := APoll;
  Conn := AConn;
  Owner := AOwner;
  Unlock := AUnlock;
  Done := TEvent.Create(nil, true, false, '');
  Start;
end;

destructor TCloser.Destroy;
begin
  WaitFor;
  Done.Free;
  inherited Destroy;
end;

procedure TCloser.Execute;
begin
  try
    try
      if Owner <> nil then
        Owner.ConnectionRemove(Conn.Handle)
      else begin
        if Unlock then
          Conn.TryLock(true); // includes a failed acquisition once the socket is nil
        Poll.CloseProbe(Conn, Unlock);
      end;
    except
      on E: Exception do
        ErrorText := E.ClassName + ': ' + E.Message;
    end;
  finally
    Done.SetEvent;
  end;
end;

procedure TProbeOwner.Execute;
begin
  // No application I/O. The ordinary read-poll workers and connection/GC lists
  // are still created by TAsyncConnections, as in a real owner.
  while not Terminated do
    Sleep(1);
end;

function TProbeOwner.AddProbe: TProbeConnection;
var
  Addr: TNetAddr;
begin
  FillChar(Addr, SizeOf(Addr), 0);
  Result := TProbeConnection.Create(Self, Addr);
  if not ConnectionNew(NewProbeSocket, Result, true) then
    raise Exception.Create('ConnectionNew failed');
end;

function TProbeOwner.GarbageCount: integer;
begin
  fGC1.Safe.Lock;
  try
    Result := fGC1.Count;
  finally
    fGC1.Safe.UnLock;
  end;
end;

procedure Reset(Where: TPauseAt);
begin
  Entered.ResetEvent;
  ContinueClose.ResetEvent;
  PauseAt := Where;
  CloseCalls := 0;
  StopCalls := 0;
  ClosedCalls := 0;
  StopsRunning := 0;
  EarlyClosed := 0;
end;

procedure RunRace(Where: TPauseAt; FirstUnlock, SecondUnlock: boolean);
var
  Poll: TProbeSockets;
  Conn: TProbeConnection;
  A, B: TCloser;
  Addr: TNetAddr;
  Locked: boolean;
begin
  Writeln('CASE barrier=', Ord(Where), ' first_unlock=', FirstUnlock, ' second_unlock=', SecondUnlock);
  Reset(Where);
  Poll := TProbeSockets.Create([], 1);
  FillChar(Addr, SizeOf(Addr), 0);
  Conn := TProbeConnection.Create(nil, Addr);
  Conn.Arm;
  A := TCloser.Create(Poll, Conn, nil, FirstUnlock);
  B := nil;
  try
    Check(Entered.WaitFor(2000) = wrSignaled, 'first close reached barrier');
    B := TCloser.Create(Poll, Conn, nil, SecondUnlock);
    Check(B.Done.WaitFor(1500) = wrSignaled, 'second close returns without waiting for owner');
    Check(ClosedCalls = 0, 'no retirement while first close is paused');
    if Where = paWriterInCallback then begin
      Locked := Conn.TryLock(true);
      Check(not Locked, 'losing closer did not release another threads writer lock');
      if Locked then Conn.UnLock(true);
    end
    else if SecondUnlock and (Where <> paStop) then begin
      Locked := Conn.TryLock(true);
      Check(Locked, 'losing closer released its writer lock');
      if Locked then Conn.UnLock(true);
    end;
  finally
    ContinueClose.SetEvent;
    A.WaitFor;
    if B <> nil then B.WaitFor;
  end;
  Check((A.ErrorText = '') and (B.ErrorText = ''), 'no worker exception');
  Check((CloseCalls = 1) and (StopCalls = 1) and (ClosedCalls = 1), 'one callback, stop and retirement');
  Check(EarlyClosed = 0, 'retirement follows completed stop');
  B.Free;
  A.Free;
  Conn.Free;
  Poll.Free;
end;

procedure RunRecycle;
var
  Poll: TProbeSockets;
  Conn: TProbeConnection;
  Addr: TNetAddr;
begin
  Writeln('CASE reentrant and recycled connection');
  Reset(paReentrant);
  Poll := TProbeSockets.Create([], 1);
  CallbackPoll := Poll;
  FillChar(Addr, SizeOf(Addr), 0);
  Conn := TProbeConnection.Create(nil, Addr);
  Conn.Arm;
  Poll.CloseProbe(Conn, false);
  Check((CloseCalls = 1) and (StopCalls = 1) and (ClosedCalls = 1), 'reentrant close only once');
  PauseAt := paNone;
  Conn.Recycle(Addr);
  Conn.Arm;
  Poll.CloseProbe(Conn, false);
  Poll.CloseProbe(Conn, false);
  Check((CloseCalls = 2) and (StopCalls = 2) and (ClosedCalls = 2), 'recycled instance gets one new close');
  Poll.CloseProbe(nil, false);
  Conn.Free;
  Poll.Free;
  CallbackPoll := nil;
end;

procedure RunRemove(RemoveFirst: boolean);
var
  Owner: TProbeOwner;
  Conn: TProbeConnection;
  A: TCloser;
begin
  Writeln('CASE ConnectionRemove first=', RemoveFirst);
  Reset(paBeforeOnClose);
  Owner := TProbeOwner.Create(nil, nil, TProbeConnection, 'close-regression', nil, [], 1);
  Conn := Owner.AddProbe;
  // An access descendant exposes the same protected entry point on baseline.
  if RemoveFirst then
    A := TCloser.Create(nil, Conn, Owner, false)
  else
    A := TCloser.Create(TProbeSockets(Owner.Clients), Conn, nil, false);
  try
    Check(Entered.WaitFor(2000) = wrSignaled, 'owner close reached callback');
    if RemoveFirst then
      TProbeSockets(Owner.Clients).CloseProbe(Conn, false)
    else
      Check(not Owner.ConnectionRemove(Conn.Handle), 'remove cannot steal an active close');
    Check(Owner.GarbageCount = 0, 'connection not published to GC during callback');
  finally
    ContinueClose.SetEvent;
    A.WaitFor;
  end;
  Check(A.ErrorText = '', 'remove worker completed');
  Check((CloseCalls = 1) and (Owner.ConnectionCount = 0) and (Owner.GarbageCount = 1),
    'remove and close share one retirement');
  A.Free;
  PauseAt := paNone;
  Owner.Free;
end;

procedure RunRemoveCallback;
var
  Owner: TProbeOwner;
  Conn: TProbeConnection;
begin
  Writeln('CASE ConnectionRemove callback acquires connection list');
  Reset(paNone);
  Owner := TProbeOwner.Create(nil, nil, TProbeConnection, 'remove-callback', nil, [], 1);
  Conn := Owner.AddProbe;
  CallbackOwner := Owner;
  Check(Owner.ConnectionRemove(Conn.Handle), 'first remove claims connection');
  Check((CloseCalls = 1) and (Owner.GarbageCount = 1), 'remove calls OnClose before retirement');
  CallbackOwner := nil;
  Owner.Free;
end;

procedure RunCloseException;
var
  Poll: TProbeSockets;
  Conn: TProbeConnection;
  Addr: TNetAddr;
begin
  Writeln('CASE OnClose exception');
  Reset(paRaiseOnClose);
  Poll := TProbeSockets.Create([], 1);
  FillChar(Addr, SizeOf(Addr), 0);
  Conn := TProbeConnection.Create(nil, Addr);
  Conn.Arm;
  Poll.CloseProbe(Conn, false);
  Check((CloseCalls = 1) and (StopCalls = 1) and (ClosedCalls = 1), 'callback exception still closes socket');
  Conn.Free;
  Check(CloseCalls = 1, 'destructor does not repeat failed callback');
  Poll.Free;
end;

procedure RunUntracked;
var
  Owner: TProbeOwner;
  Conn: TProbeConnection;
  A: TCloser;
begin
  Writeln('CASE untracked connection');
  Reset(paBeforeOnClose);
  Owner := TProbeOwner.Create(nil, nil, TProbeConnection, 'untracked-close', nil, [acoNoConnectionTrack], 1);
  Conn := Owner.AddProbe;
  A := TCloser.Create(TProbeSockets(Owner.Clients), Conn, nil, false);
  try
    Check(Entered.WaitFor(2000) = wrSignaled, 'untracked close reached barrier');
    TProbeSockets(Owner.Clients).CloseProbe(Conn, false);
    Check(Owner.GarbageCount = 0, 'untracked connection not retired early');
  finally
    ContinueClose.SetEvent;
    A.WaitFor;
  end;
  Check((Owner.ConnectionCount = 0) and (Owner.GarbageCount = 1), 'untracked count decremented exactly once');
  A.Free;
  PauseAt := paNone;
  Owner.Free;
end;

function TLoopConnection.OnRead: TPollAsyncSocketOnReadWrite;
begin
  fRd.Reset;
  Owner.WriteString(Self, 'ok');
  Result := soClose;
end;

function TLoopConnection.AfterWrite: TPollAsyncSocketOnReadWrite;
begin
  if LoopCloseAfterWrite then Result := soClose else Result := soContinue;
end;

procedure TLoopConnection.OnClose;
begin
  inherited OnClose;
  InterlockedIncrement(LoopClosed);
end;

procedure RunLoopback(AfterWriteClose: boolean);
var
  Server: TAsyncServer;
  Client: TNetSocket;
  Addr: TNetAddr;
  Port: RawUtf8;
  I, N, Received: integer;
  Data: array[0..1] of AnsiChar;
  Deadline: Int64;
  Ok: boolean;
begin
  Writeln('CASE live loopback afterwrite_close=', AfterWriteClose);
  LoopClosed := 0;
  LoopCloseAfterWrite := AfterWriteClose;
  Server := TAsyncServer.Create('127.0.0.1:0', nil, nil, TLoopConnection, 'close-loopback', nil, [], 2);
  try
    Server.WaitStarted(5);
    if Server.Server.Sock.GetName(Addr) <> nrOK then
      raise Exception.Create('listener address unavailable');
    Port := RawUtf8(AnsiString(IntToStr(Addr.Port)));
    Ok := true;
    for I := 1 to 50 do begin
      if NewSocket('127.0.0.1', Port, nlTcp, false, 1000, 1000, 1000, 0, Client) <> nrOK then
        raise Exception.Create('loopback connect failed');
      try
        Data[0] := 'x';
        N := 1;
        Ok := Ok and (Client.Send(@Data[0], N) = nrOK) and (N = 1);
        Received := 0;
        while Received < 2 do begin
          N := 2 - Received;
          if Client.Recv(@Data[Received], N) <> nrOK then Break;
          if N <= 0 then Break;
          Inc(Received, N);
        end;
        Ok := Ok and (Received = 2) and (Data[0] = 'o') and (Data[1] = 'k');
      finally
        Client.ShutdownAndClose(false);
      end;
    end;
    Deadline := mormot.core.os.GetTickCount64 + 2000;
    while ((LoopClosed <> 50) or (Server.ConnectionCount <> 0)) and
      (mormot.core.os.GetTickCount64 < Deadline) do Sleep(1);
    Check(Ok, '50 replies delivered through async socket polling');
    Check((LoopClosed = 50) and (Server.ConnectionCount = 0), '50 connections closed once and removed');
  finally
    Server.Free;
  end;
end;

var
  Where: TPauseAt;
  A, B: boolean;
begin
  Entered := TEvent.Create(nil, true, false, '');
  ContinueClose := TEvent.Create(nil, true, false, '');
  for Where := paBeforeOnClose to paStop do
    for A := false to true do
      for B := false to true do
        RunRace(Where, A, B);
  RunRace(paWriterInCallback, false, true);
  RunRecycle;
  RunRemove(false);
  RunRemove(true);
  RunRemoveCallback;
  RunCloseException;
  RunUntracked;
  RunLoopback(false);
  RunLoopback(true);
  ContinueClose.Free;
  Entered.Free;
  Writeln('FAILURES=', Failures);
  if Failures <> 0 then Halt(1);
end.
