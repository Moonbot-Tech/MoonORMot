@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat"
if not exist bin mkdir bin
if not exist dcu mkdir dcu
"%BDS%\bin\dcc64.exe" NumericLibrary.dpr -B -Q -GD -$O+ -$R- -$Q- -U"..\..\core" -Ebin -Ndcu -NSSystem;Winapi
exit /b %ERRORLEVEL%
