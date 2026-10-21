@echo off
setlocal
pushd "%~dp0"
"%~dp0tgod_native.exe" %*
set "tgod_exit=%errorlevel%"
if not "%tgod_exit%"=="0" (
  echo.
  echo No se pudo iniciar el port. Revisa el mensaje anterior y selecciona una ROM USA v0 compatible.
  pause
)
popd
exit /b %tgod_exit%
