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

Para observar la intro completa sin que la navegación automática pulse Start/A:

```powershell
.\build\tgod_native.exe --headless --intro-check --seconds 38 --rom 'D:\ROMs\mi-copia.z64'
```

Guarda `framebuffer_intro_N.bmp` cada 0,5 s, donde N son décimas de segundo;
por ejemplo, `framebuffer_intro_100.bmp` corresponde aproximadamente a 10 s.
Permite revisar KEMCO, Snowblind, portada y entrada al menú. El tiempo corresponde
al host: dos builds pueden no capturar el mismo frame N64. Requiere `--headless`
y no puede combinarse con `--graphics-check`. No modifica la intro normal.

## Resolución interna y configuración gráfica

F6 alterna **nativo → 720p → 1080p → nativo**. La elección aparece en el título
y se guarda en `tgod.ini`, junto al EXE, en `[Graphics]`, clave `RenderHeight`:
`0` conserva el modo del juego; `720` y `1080` dibujan a mayor resolución.
Se puede editar ese archivo con el juego cerrado o elegir solo para una sesión:

```powershell
.\build\tgod_native.exe --render-height 1080 --rom 'D:\ROMs\mi-copia.z64'
```

Se mantiene 4:3: en carrera, 720p corresponde a **960×720** y 1080p a
**1440×1080**. En un monitor 1920×1080, F11 usa pantalla completa con barras
laterales. F1–F4 cambian el tamaño de ventana; F5 cambia el filtro de presentación.
Esos controles no reemplazan la resolución interna de F6. Los modos 2D/VI
pueden tener recortes propios del juego.

**720p/1080p son experimentales y lentos con el renderer por CPU actual.**
Un mismo cuadro de carrera tardó aproximadamente 318 ms a 720p y 714 ms a 1080p
en la comparación local. No son medidas de un recorrido completo ni una promesa
de FPS; nativo sigue siendo el valor predeterminado para jugar. El juego y sus
eventos gráficos avanzan siempre con el renderer nativo. Un hilo separado dibuja
las copias HD y descarta instantáneas intermedias si no llega a tiempo; la imagen
HD puede verse poco fluida y retrasada. Cambiar F6 no introduce la espera HD en
el avance de la simulación. Para HD fluido
queda pendiente acelerar el rasterizador o incorporar un backend de GPU.

La geometría se vuelve a dibujar con más píxeles y el LOD usa la huella de cada
píxel interno. Se conservan las texturas originales: aumentar resolución no crea
detalle en carteles ni permite descartar por sí solo fallos de LOD/filtrado.

Para comparar exactamente la misma lista de dibujo de carrera en tres tamaños:

```powershell
.\build\tgod_native.exe --headless --resolution-check --seconds 30 --rom 'D:\ROMs\mi-copia.z64'
```

Genera `framebuffer_compare_native.bmp`, `framebuffer_compare_720p.bmp` y
`framebuffer_compare_1080p.bmp`, con tiempos de replay en la consola. Continúa
la carrera en nativo; no cambia `tgod.ini`. El modo headless ignora la preferencia
guardada salvo `--render-height`, que solicita la salida HD asíncrona.
`--resolution-check` requiere headless/sin solicitud HD
y no puede combinarse con `--intro-check`. Las capturas permanecen fuera de Git.

## Controles

Enter: Start. X/Espacio: A. Z/Retroceso: B. C: gatillo Z. A/S: L/R.
Flechas: dirección/stick. Escape: salir. F1–F4: ventana. F5: filtro de salida.
F6: resolución interna nativa/720p/1080p.
F11 o Alt+Enter: pantalla completa. También se detectan mandos XInput.
