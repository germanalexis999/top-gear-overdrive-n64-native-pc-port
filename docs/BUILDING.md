# Compilar en Windows

Requisitos: Git, PowerShell, Python 3.11 o posterior, CMake 3.20 o posterior,
Ninja y un compilador C/C++20 compatible con MinGW-w64. La revisión probada usa
GCC 16.2.0 y Ninja; otros compiladores todavía no se han validado.

## Dependencias

Desde la raíz del proyecto:

```powershell
.\tools\bootstrap.ps1
cmake -S tools/N64Recomp -B tools/N64Recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tools/N64Recomp/build --target N64Recomp --parallel 4
```

`bootstrap.ps1` descarga las revisiones exactas de `dependencies.lock.json`,
inicializa sus submódulos y aplica los parches del port. Preserva checkouts
existentes y se detiene si tienen una revisión diferente o cambios incompatibles.
Los directorios descargados se ignoran en Git; sus licencias se conservan allí.

## ROM local y generación

Aporta una copia local de la revisión USA v0. Normalízala antes de generar:

```powershell
.\tools\normalize_n64_rom.ps1 -InputPath 'C:\ruta\mi-copia.z64' -OutputPath 'top_gear_rom/normalized/Top Gear Overdrive (USA).z64'
python tools/prepare_recomp.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
.\build\tgod_native.exe
```

El SHA-1 esperado del ROM normalizado es
`75ebbe451906ce68b0dd43eef9ee44416452a4f1` (12 MiB). El generador verifica la
revisión y añade seis esperas cooperativas a los bucles de polling. Ejecutarlo
otra vez regenera esas correcciones de forma determinista.

El runtime busca el ROM en la ruta anterior. No se debe versionar la ROM, el C
generado, el ejecutable, los guardados ni los archivos de diagnóstico.

## Comprobaciones

Con el juego ya generado:

```powershell
cmake --build build --target tgod_regression_checks --parallel 4
.\build\tgod_regression_checks.exe
.\build\tgod_native.exe --headless --seconds 16
```

Las pruebas unitarias también pueden compilarse sin ROM ni C generado:

```powershell
cmake -S . -B build-checks -G Ninja -DCMAKE_BUILD_TYPE=Release -DTGOD_BUILD_GAME=OFF
cmake --build build-checks --target tgod_regression_checks --parallel 4
.\build-checks\tgod_regression_checks.exe
```

La ejecución headless activa navegación automática y capturas locales; esas
capturas contienen material del juego y permanecen excluidas de Git.

Para repetir el recorrido de gráficos con frenado y derrape:

```powershell
.\build\tgod_native.exe --headless --graphics-check --seconds 30 --rom 'D:\ROMs\mi-copia.z64'
```

Además de la captura de carrera, genera `framebuffer_graphics_brake.bmp`,
`framebuffer_graphics_drift.bmp` y `framebuffer_graphics_late.bmp` en `build/`.
`--graphics-check` requiere `--headless`; no afecta a los controles normales.

## Controles

Enter: Start. X/Espacio: A. Z/Retroceso: B. C: gatillo Z. A/S: L/R.
Flechas: dirección/stick. Escape: salir. F1–F4: escala. F5: filtro.
F11 o Alt+Enter: pantalla completa. También se detectan mandos XInput.
