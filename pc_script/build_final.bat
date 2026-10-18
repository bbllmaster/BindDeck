@echo off
rem Kept for backwards compatibility. The maintained build lives in build.bat,
rem which checks every step and only reports success when dist\BindDeck.exe
rem actually exists.
call "%~dp0build.bat"
exit /b %ERRORLEVEL%
