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

F6 alterna **nativo → 720p → 1080p → 4K → nativo**. La elección aparece en el título
y se guarda en `tgod.ini`, junto al EXE, en `[Graphics]`, clave `RenderHeight`:
`0` conserva el modo del juego; `720`, `1080` y `2160` (4K) dibujan a mayor resolución.
Se puede editar ese archivo con el juego cerrado o elegir solo para una sesión:

```powershell
.\build\tgod_native.exe --render-height 1080 --rom 'D:\ROMs\mi-copia.z64'
```

Se mantiene 4:3: en carrera, 720p corresponde a **960×720** y 1080p a
**1440×1080**. En un monitor 1920×1080, F11 usa pantalla completa con barras
laterales. F1–F4 cambian el tamaño de ventana; F5 cambia el filtro de presentación.
Esos controles no reemplazan la resolución interna de F6. Los modos 2D/VI
pueden tener recortes propios del juego.

### Ajuste original de Expansion Pak

El port siempre proporciona 8 MiB de RDRAM. El ajuste original y la resolución
HD de F6 se eligen por separado. Con el juego cerrado, en `tgod.ini`:

```ini
[Graphics]
RenderHeight=1080
OriginalResolution=full
```

`OriginalResolution` admite `game` (predeterminado, deja los ajustes originales),
`off`, `half` o `full`. Se aplica una sola vez tras inicializar/cargar los ajustes
del juego; después puedes cambiarlo desde Setup. Para desactivar la preferencia
de arranque, vuelve a `game`. El menú puede guardar elecciones en el guardado
original, igual que antes; `game` no borra esas elecciones.

Para seleccionar solo durante una sesión:

```powershell
.\build\tgod_native.exe --original-resolution full --render-height 1080
```

Off normalmente dibuja 320×240; Full usa 640×240 cuando el juego lo permite.
Half conserva el área reducida de 640×120 con bandas arriba y abajo. El juego
mantiene sus restricciones de escena/multijugador. El escalado usa dimensiones
de la escena y ejes separados: a 1080p Full tiene superficie 1440×1080 y Half
1440×540 centrada; la intro de 640×480 usa su escala propia. El área VI visible
puede recortar márgenes. No se fuerza Full por defecto. Esta implementación está
compilada y pendiente de comparación visual e interactiva de todos esos modos.
Evidencia en [EXPANSION-PAK-RESEARCH.md](EXPANSION-PAK-RESEARCH.md).

**Las fuentes posteriores a preview.6 usan GPU Direct3D 11 para 720p/1080p/4K.**
Se requiere un adaptador con feature level 11.0; no se usa WARP/software como si
fuera hardware. Si no está disponible, o falla el dispositivo, se informa en la
consola y se vuelve al renderer HD por CPU, mucho más lento. Nativo sigue siendo
el valor predeterminado. El juego y los eventos SP/DP conservan el renderer nativo;
el trabajador HD usa memoria privada y nunca entrega esos eventos. Se eliminó el
tope de captura de 100 ms de preview.6; sigue habiendo un trabajo pendiente máximo,
que se sustituye por el más reciente. No se añaden cuadros a la simulación.

En una RTX 3080 Ti, una misma lista tardó 326/726 ms por CPU y aproximadamente
12 ms por GPU a 720p/1080p, incluidos upload y readback de esa reproducción.
Son medidas de un cuadro, no FPS garantizados ni mediciones de todos los equipos.
La presentación HD usa una swapchain DXGI: el resultado permanece en GPU,
con conversión de color, filtro y barras 4:3 en un shader propio. Una vez activa,
esa ventana también presenta el modo nativo por GPU. Antes de activarla, o si
falla la presentación, se usa GDI. El ritmo nativo también limita la salida.
La fidelidad gráfica sigue en desarrollo; hay pequeñas diferencias de redondeo
entre CPU y GPU. La comparación de consola y otras escenas sigue pendiente.

El shader propio se compila durante el build y su bytecode queda dentro del EXE;
no se compila al cambiar F6 ni se empaqueta una DLL de compilación. Windows aporta
`d3d11.dll` y `dxgi.dll`. El código fuente está en `src/gpu_raster_shader.inc`.
Las comprobaciones de GPU requieren hardware y son independientes de la ROM:

```powershell
cmake --build build --target tgod_gpu_checks
.\build\tgod_gpu_checks.exe
```

El código de salida 77 indica que no hay hardware compatible. Estas comprobaciones
no se añaden como requisito a las regresiones de software en equipos sin GPU.

Los recursos de GPU se reutilizan por dirección/dimensiones y capacidad. Cada
instantánea importa de nuevo sus contenidos; la reutilización no conserva datos
de un cuadro anterior sin comprobarlos. Las lecturas de framebuffer necesarias
para texturas se mantienen. En presentación directa se omite la descarga final
de color/profundidad; el diagnóstico headless normal conserva esa lectura.

Para comprobar la ruta real de ventana con navegación automática en una ventana
oculta propia, sin modificar `tgod.ini`:

```powershell
.\build\tgod_native.exe --headless --presentation-check --graphics-check --render-height 1080 --seconds 30 --rom 'D:\ROMs\mi-copia.z64'
```

Alterna nativo/720p/1080p/4K, tamaños, fullscreen y filtro. `--presentation-check`
requiere headless y no admite `--resolution-check`. Las imágenes
`framebuffer_dxgi_N.bmp` capturan puntualmente la textura GPU de salida; las otras
capturas de ese modo reflejan la copia nativa. Con `--intro-check` recorre la intro
sin los cambios automáticos de ventana/resolución.

Los logs separan el tiempo CPU del replay, los intervalos GPU medidos mediante
queries sin espera, la edad de instantánea al envío y los contadores DXGI. En
modo residente, el tiempo CPU no incluye esperar a que termine la GPU. El
intervalo GPU incluye transferencias y huecos de envío; no mide solo shaders.
Termina antes de la descarga final de diagnóstico y de la presentación.
La edad al envío tampoco es una medición completa de latencia de mando/pantalla.

La geometría se vuelve a dibujar con más píxeles y el LOD usa la huella de cada
píxel interno. Se conservan las texturas originales: aumentar resolución no crea
detalle en carteles ni permite descartar por sí solo fallos de LOD/filtrado.

Para comparar exactamente la misma lista de dibujo de carrera en tres tamaños:

```powershell
.\build\tgod_native.exe --headless --resolution-check --seconds 30 --rom 'D:\ROMs\mi-copia.z64'
```

Genera `framebuffer_compare_native.bmp`, `framebuffer_compare_720p.bmp` y
`framebuffer_compare_1080p.bmp`, y sus variantes `framebuffer_compare_gpu_720p.bmp`
y `framebuffer_compare_gpu_1080p.bmp` si hay hardware. La consola informa tiempos
y diferencias por píxel entre CPU/GPU de la misma lista. Continúa
la carrera en nativo; no cambia `tgod.ini`. El modo headless ignora la preferencia
guardada salvo `--render-height`, que solicita la salida HD asíncrona.
`--resolution-check` requiere headless/sin solicitud HD
y no puede combinarse con `--intro-check`. Las capturas permanecen fuera de Git.

## Controles

Enter: Start. X/Espacio: A. Z/Retroceso: B. C: gatillo Z. A/S: L/R.
Flechas: dirección/stick. Escape: salir. F1–F4: ventana. F5: filtro de salida.
F6: resolución interna nativa/720p/1080p/4K.
F11 o Alt+Enter: pantalla completa. Los mandos se gestionan mediante SDL3.

4K conserva el formato 4:3: renderiza 2880×2160 y muestra barras laterales en una pantalla 3840×2160. Se selecciona con F6, RenderHeight=2160 o --render-height 4k. Requiere más trabajo de GPU que 1080p.

Mandos: hasta cuatro gamepads/joysticks mediante SDL3, incluidos Xbox y genéricos
que SDL pueda abrir. El teclado comparte jugador 1 por defecto, sin consumir un
puerto extra. F7 permite reasignar dispositivos por su nombre. Un joystick sin
mapping estándar aparece como «configurar»: asigna manualmente botones, ejes y
crucetas. Se conservan los puertos de los otros jugadores al desconectar un mando.
F11 o Alt+Enter alterna pantalla completa sin bordes; mantener la tecla pulsada
no vuelve a alternar. La resolución interna se cambia por separado con F6.


Mapper de mandos: F7 abre Controles. Elige jugador 1–4 y mando 1–4;
si el mando ya estaba asignado, se intercambia con el del otro jugador. El teclado
puede compartirse con un solo jugador. Selecciona Mando o Teclado, una acción y
una de sus tres asignaciones; suelta los controles y pulsa el botón/tecla o mueve
el eje. Escape cancela la captura. Hay zona muerta (0–50%) e inversión de ejes.
Guardar y cerrar aplica y escribe controls.json junto al EXE (con respaldo .bak);
Cancelar descarta los cambios. Mientras el panel está abierto el juego recibe
controles neutros; al cerrar, suelta los controles para volver a jugar.

Esta revisión local integra SDL3 3.4.18 y requiere SDL3.dll junto al EXE.
Los perfiles anteriores de controls.json se cargan; al guardar se usa versión 2
(con respaldo). Revisa las asignaciones en F7 después del cambio desde XInput.
Para joysticks sin mapping estándar, deja los ejes en reposo antes de capturar;
se recuerda ese centro para poder asignar también gatillos con reposo desplazado.
Se admiten hasta 16 ejes, 64 botones y 4 crucetas por joystick. Opcionalmente se
puede aportar un gamecontrollerdb.txt local; el ZIP no incorpora una base externa.
La identidad entre sesiones y mandos idénticos sigue pendiente de mejorar.


## SDL3 de entrada

bootstrap.ps1 obtiene SDL3 3.4.18 en tools/SDL (commit fijado en el lockfile).
Si las dependencias están en otra carpeta, configurar también
-DTGOD_SDL_DIR=<carpeta/SDL>, igual que TGOD_RUNTIME_DIR y TGOD_RECOMPILER_DIR.
CMake compila SDL3.dll y la copia junto al EXE. SDL no sustituye DXGI ni WaveOut.
El empaquetador incluye esa DLL, su licencia zlib y su revisión en SOURCE.md.
El build MinGW compila el recurso de versión sin los -I heredados para evitar
un problema de windres con rutas que contienen espacios; SDL no se modifica.
