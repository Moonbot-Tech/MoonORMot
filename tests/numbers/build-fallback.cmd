@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat"
if not exist bin mkdir bin
if not exist dcu-win32 mkdir dcu-win32
"%BDS%\bin\dcc32.exe" FallbackSmoke.dpr -B -Q -$O+ -U"..\..\core" -Ebin -Ndcu-win32 -NSSystem;Winapi
if errorlevel 1 exit /b %ERRORLEVEL%
bin\FallbackSmoke.exe
exit /b %ERRORLEVEL%
