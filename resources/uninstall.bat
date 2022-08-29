@echo off

if exist %windir%\SysWOW64 (
"%~dp0devcon\x64\devcon.exe" -r remove "KTCBusDriver"
) else (
"%~dp0devcon\x86\devcon.exe" -r remove "KTCBusDriver"
)