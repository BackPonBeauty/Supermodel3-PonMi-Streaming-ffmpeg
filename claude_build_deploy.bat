@echo off
cd /d H:\Supermodel3-PonMi-main5
set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
echo BUILD START %DATE% %TIME% > claude_build.log
mingw32-make -f Makefiles/Makefile.Win32 >> claude_build.log 2>&1
set RC=%ERRORLEVEL%
echo EXIT=%RC% >> claude_build.log
if not "%RC%"=="0" goto end
powershell -NoProfile -ExecutionPolicy Bypass -File deploy.ps1 >> claude_build.log 2>&1
echo DEPLOY_EXIT=%ERRORLEVEL% >> claude_build.log
:end
echo DONE %DATE% %TIME% >> claude_build.log
