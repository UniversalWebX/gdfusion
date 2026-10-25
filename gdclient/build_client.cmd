@echo off
setlocal
python gd_client.py --build
set RC=%ERRORLEVEL%
echo.
echo Build exit code: %RC%
pause
exit /b %RC%
