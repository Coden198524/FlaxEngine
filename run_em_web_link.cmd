@echo off
setlocal
set "OUT=E:\Work\FlaxEngine\ManualWebLink.out.txt"
set "ERR=E:\Work\FlaxEngine\ManualWebLink.err.txt"
set "STATUS=E:\Work\FlaxEngine\ManualWebLink.status.txt"
echo START %date% %time% > "%STATUS%"
if exist "%OUT%" del /f /q "%OUT%"
if exist "%ERR%" del /f /q "%ERR%"
call "E:\Work\emsdk\upstream\emscripten\em++.bat" "@E:\Work\FlaxEngine\Cache\Intermediate\FlaxGame\Web\x86\Development\FlaxGame.html.response" 1>"%OUT%" 2>"%ERR%"
echo EXIT %errorlevel% %date% %time% >> "%STATUS%"
exit /b %errorlevel%
