@echo off
setlocal
set "SDK_ENTRY_PYTHON="
if defined SDK_PYTHON (
    call :sdk_try_python "%SDK_PYTHON%"
    if errorlevel 1 goto sdk_bad_override
    goto sdk_run
)
call :sdk_try_python "%~dp0.venv\Scripts\python.exe"
if defined SDK_ENTRY_PYTHON goto sdk_run
where py.exe >nul 2>&1
if not errorlevel 1 (
    py -3 -I -c "import sys;sys.exit(0 if sys.version_info >= (3,11) and sys.maxsize > 2**32 else 1)" >nul 2>&1
    if not errorlevel 1 goto sdk_run_launcher
)
for /f "delims=" %%I in ('where python.exe 2^>nul ^| findstr /v /i /c:WindowsApps') do call :sdk_try_python "%%I"
for /f "delims=" %%I in ('where python3.exe 2^>nul ^| findstr /v /i /c:WindowsApps') do call :sdk_try_python "%%I"
if defined SDK_ENTRY_PYTHON goto sdk_run
echo Install 64-bit Python 3.11+ with the Python launcher or PATH enabled, or set SDK_PYTHON to its executable. 1>&2
exit /b 1

:sdk_bad_override
echo SDK_PYTHON must point to a working 64-bit Python 3.11+ executable. 1>&2
exit /b 1

:sdk_run
"%SDK_ENTRY_PYTHON%" -I "%~dp0tools\bootstrap.py" %*
exit /b %errorlevel%

:sdk_run_launcher
py -3 -I "%~dp0tools\bootstrap.py" %*
exit /b %errorlevel%

:sdk_try_python
if defined SDK_ENTRY_PYTHON exit /b 0
if not exist "%~1" exit /b 1
"%~1" -I -c "import sys;sys.exit(0 if sys.version_info >= (3,11) and sys.maxsize > 2**32 else 1)" >nul 2>&1
if errorlevel 1 exit /b 1
set "SDK_ENTRY_PYTHON=%~1"
exit /b 0
