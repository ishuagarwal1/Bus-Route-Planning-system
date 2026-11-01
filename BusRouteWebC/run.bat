@echo off
cd /d "%~dp0"
where gcc >nul 2>nul
if errorlevel 1 (
  echo GCC compiler not found. Install the free one - see README.md, step 1.
  pause & exit /b 1
)
gcc -Wall -Iinclude src\main.c src\db.c src\backend.c src\server.c -o busroute.exe -lws2_32
if errorlevel 1 (pause & exit /b 1)
start "" http://localhost:8080
busroute.exe
pause
