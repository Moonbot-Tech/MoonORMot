@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Embarcadero\Studio\23.0\bin\rsvars.bat"
if not exist bin mkdir bin
if not exist bin\sse2 mkdir bin\sse2
if not exist dcu mkdir dcu
if not exist dcu-sse2 mkdir dcu-sse2
"%BDS%\bin\dcc64.exe" NumericLibrary.dpr -B -Q -GD -$O+ -$R- -$Q- -U"..\..\core" -Ebin -Ndcu -NSSystem;Winapi
if errorlevel 1 exit /b %ERRORLEVEL%
"%BDS%\bin\dcc64.exe" NumericLibrary.dpr -B -Q -GD -$O+ -$R- -$Q- -DMORMOT_NUMERIC_FORCE_SSE2 -U"..\..\core" -Ebin\sse2 -Ndcu-sse2 -NSSystem;Winapi
exit /b %ERRORLEVEL%
