@echo off
setlocal
python -m pip install -r requirements-dev.txt
python -m pytest -q tests
set RC=%ERRORLEVEL%
echo.
echo Test exit code: %RC%
pause
exit /b %RC%
