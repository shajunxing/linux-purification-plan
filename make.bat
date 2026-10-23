@echo off
cd /d %~dp0
call :cc bin2c
nasm src\boot.s -f bin -o bin\boot.bin
bin\bin2c bin\boot.bin bin\boot.inc
call :cc raw
bin\raw.exe -o bin\output.raw bin\bzImage bin\init
exit /b 0

:cc
cl /nologo /W3 /utf-8 /std:clatest /O2 /MD src\%1.c /Fo: tmp\%1.obj /Fe: bin\%1.exe
exit /b %errorlevel%
