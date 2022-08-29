@echo off

if exist %windir%\SysWOW64 (
::"%~dp0devcon\x64\devcon.exe"  disable =display
  "%~dp0devcon\x64\devcon.exe"  remove   "KTCBusDriver"
  "%~dp0devcon\x64\devcon.exe"  install  "%~dp0bus\x64\statbus.inf" "KTCBusDriver"
  "%~dp0devcon\x64\devcon.exe"  updateNI "%~dp0ExpansionScreen\x64\ExpansionScreen.inf" "KTCBusDriver"
::"%~dp0devcon\x64\devcon.exe"  install  "%~dp0ExpansionScreen\x64\ExpansionScreen.inf" "KTCBusDriver"
::"%~dp0devcon\x64\devcon.exe"  enable =display
) 
else (
::"%~dp0devcon\x86\devcon.exe"  disable =display
  "%~dp0devcon\x86\devcon.exe"  remove   "KTCBusDriver"
  "%~dp0devcon\x86\devcon.exe"  install  "%~dp0bus\x86\statbus.inf" "KTCBusDriver"
  "%~dp0devcon\x86\devcon.exe"  updateNI "%~dp0ExpansionScreen\x86\ExpansionScreen.inf" "KTCBusDriver"
::"%~dp0devcon\x86\devcon.exe"  install  "%~dp0ExpansionScreen\x86\ExpansionScreen.inf" "KTCBusDriver"
::"%~dp0devcon\x86\devcon.exe"  enable =display
)




