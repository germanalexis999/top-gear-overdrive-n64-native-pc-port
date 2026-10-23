# Modelos y aparición a distancia — 2026-10-23

## Solicitud e implementación

### Interruptor general

En `[Graphics]`, `LegacyVersion=true` fuerza el perfil original: resolución
interna nativa, elección original de Expansion Pak a cargo del juego, LOD
original, distancias al 100% y ningún sector extra. F6 permanece en nativo.
Conserva los 8 MiB, las correcciones del port y sus controles.

`LegacyVersion=false` selecciona el perfil mejorado. Las claves ausentes toman
1080p, `OriginalResolution=full`, detalle alto, 200% de distancia y dos sectores
extra. Las claves individuales existentes permiten personalizar ese perfil:
por ejemplo, `RenderHeight=2160` conserva 4K. Para activar todas las mejoras:

```ini
[Graphics]
LegacyVersion=false
RenderHeight=1080
OriginalResolution=full
ForceHighDetail=1
DrawDistancePercent=200
ExtraTrackSections=2
```

Reinicia después de cambiarlo. Legacy tiene prioridad sobre las opciones
individuales y los argumentos gráficos; no sobrescribe sus valores guardados.
Al volver a false se recuperan. Se aceptan true/false sin distinguir mayúsculas
(o 1/0). Un valor inválido selecciona legacy y avisa en consola. El modo
headless ignora el perfil del INI y conserva sus opciones de diagnóstico.

### Ajustes individuales del perfil mejorado

Se agregaron tres opciones independientes a `[Graphics]` de `tgod.ini`:

| Opción | Rango | Valor inicial | Efecto |
| --- | --- | --- | --- |
| `ForceHighDetail` | 0–1 | 1 | Selecciona el primer modelo en la rama que usa variantes por distancia. |
| `DrawDistancePercent` | 100–200 | 200 | Amplía far y los dos umbrales secundarios de cámara hasta el porcentaje indicado. |
| `ExtraTrackSections` | 0–2 | 2 | Consulta uno o dos sectores adicionales por cada lado, después de los tres originales. |

Se leen al arrancar; reiniciar tras editarlas. Sus valores se limitan a esos
rangos. Las claves ausentes se crean con los valores del perfil mejorado. En headless se
usan los valores originales para conservar el comportamiento de las herramientas
existentes. F6 y `OriginalResolution` siguen siendo opciones independientes.

Ejemplo para activar las mejoras en el EXE local:

```ini
[Graphics]
LegacyVersion=false
ForceHighDetail=1
DrawDistancePercent=200
ExtraTrackSections=2
```

Para volver al comportamiento anterior: `0`, `100`, `0`, respectivamente.
No hace falta cambiar resolución ni borrar el guardado. Los callbacks actúan
solo durante carrera (`0x80117DA4 == 2`); no fuerzan modelos en HUD/intro/menús.

## Evidencia estática en USA v0

### Selección de modelos

`func_80013BA8` calcula una profundidad mediante `func_800134E0` y la compara
con dos umbrales de cámara en `0x80013E38–0x80013E78`. Produce niveles 0, 1 o 2
y los guarda en el byte +4 de una entrada de la cola gráfica. También existen
flags de desaparición a esos umbrales (`0x100` y `0x200`): ampliar solo far no
es suficiente para mover todos esos cortes.

`func_80004B50`, rama `0x80005CB0–0x80005CD0`, comprueba el flag `0x400`, lee
ese byte y lo usa para indexar una tabla de modelos. El hook antes de
`0x80005CC4` fuerza únicamente el índice de dibujo a cero, el que se usa cerca.
No reescribe la cola, los flags del objeto ni distancias usadas por su lógica.
No se modifica el bit `0x8000` de los materiales: también aparece en segundos
pases y no hay evidencia de que quitarlo sea una solución general de LOD.

Se utiliza el detalle disponible en la ROM. Objetos con un único modelo o
sprites no ganan polígonos nuevos. No está demostrado que todas las familias de
geometría del juego usen esta misma tabla; esto no acredita que cada modelo de
cada circuito haya sido reemplazado.

### Cámaras y límites por distancia

`func_800121A8` recibe near en f12, far en f14 y dos distancias secundarias
en a2/a3. Guarda los parámetros por cámara y llama `func_800122F8` para derivar
los planos y matrices; `func_800134E0` usa el far derivado en el descarte de
esferas. El hook de entrada escala far y ambas distancias secundarias antes de
la derivación. Near se conserva. Los cambios posteriores de FOV recalculan a
partir de los valores almacenados, sin acumular repetidamente el multiplicador.

Far se limita a 30000 unidades para esta ampliación; si una cámara ya supera
ese valor, no se reduce. No es una validación de todos los límites de coordenadas,
profundidad o capacidad gráfica del juego. Las distancias secundarias se escalan
con el factor efectivo y no se extienden más allá de far.

### Visibilidad por sectores

`func_8003597C` consulta el sector actual, anterior y siguiente a través de
`func_80017C7C`. Se usa un callback de frustum (`func_80035C9C`), un bitmap de
deduplicación y un contador en `0x800F8780`, limitado a 255 entradas. Después
se clasifican listas con límites adicionales de 32/32/256.

El hook antes de `0x80035AA8` consulta los sectores ±2 y, si se solicita, ±3.
Mantiene primero los tres originales para priorizar su contenido. Normaliza el
índice en el circuito cerrado y omite índices exteriores de rutas no cíclicas.
Cada llamada usa una copia del contexto MIPS para conservar los registros vivos
del llamador. No se cambia el callback ni se salta la deduplicación o las
comprobaciones de capacidad. Los datos se consultan en las mismas tablas ya
cargadas; no se añade un cargador nuevo de sectores.

## Alcance y pendientes

El objetivo es quitar los cambios de modelo en esa familia y alejar la aparición
del decorado. No equivale a dibujar el circuito completo: persisten los límites
de las listas, planos de cámara, visibilidad por sectores y modelos existentes.
Cuando una lista se llena, las entradas extra no desplazan deliberadamente a las
originales; aun así, no hay garantía de ausencia de otros saltos visibles.

El renderer HD solo puede mejorar lo que llega en la display list; desactivar
su culling no recuperaría objetos omitidos por el juego. Por eso estos cambios
están en la generación de las listas, mediante hooks declarativos de la revisión
USA v0. Se mantienen frustum, clipping, profundidad, transparencia y los eventos
SP/DP. Dibujar más geometría aumenta también el trabajo del camino nativo que
precede a SP; no se ha medido su efecto en rendimiento o avance de carrera.

Se regeneró el C local y se compiló el EXE. No se ejecutaron pruebas nuevas ni
una carrera por el agente. Falta comparar cada opción por separado y combinada,
en los cinco circuitos, con niebla, cambios de cámara, varios coches y 1–4
jugadores. Revisar además las listas saturadas y otras posibles familias de LOD
antes de afirmar que desapareció todo el popping. El usuario solicitó después
el interruptor para activar todas las mejoras: el perfil mejorado es ahora el
valor inicial, con legacy disponible. Esto no confirma su rendimiento.
No se publicaron estos cambios.

## Cierre tras seleccionar la ROM: diagnóstico y corrección

El usuario reportó un cierre al cargar la ROM con el perfil mejorado. Se
reprodujo bajo GDB: SIGSEGV en `tgod_apply_original_resolution`, llamado desde
`func_8002C930`. La causa era pasar `0x80102344` como offset positivo a MEM_B.
Las macros MEM_* restan `0xFFFFFFFF80000000`: requieren una base KSEG extendida
con signo. El acceso anterior apuntaba 4 GiB más allá de la RDRAM.

Se corrigió la escritura de Full y dos lecturas con el mismo problema:
estado de carrera `0x80117DA4` y contador de visibilidad `0x800F8780`.
Ahora usan offset cero y base `(gpr)(int32_t)direccion`. Los accesos relativos
a registros del juego ya reciben bases extendidas y conservan su forma.
El cierre no demuestra un problema de la ROM ni falta de capacidad de GPU.
La escritura defectuosa ya estaba en preview.10 cuando se elegía Full mediante
la preferencia del host; la corrección está en el EXE local, aún sin publicar.

Comprobaciones de diagnóstico realizadas para este incidente, en una carpeta
aislada con EXE/DLL/INI propios, sin reutilizar los guardados del usuario:

- Antes: cierre inmediato en la escritura de Full, con backtrace de GDB.
- Después: Full y HD 1080p alcanzaron Mode 2 y mantuvieron procesamiento hasta
  el intervalo registrado de 16 segundos, sin repetir esa excepción. El log llegó
  a «runner completed», pero la sesión de depuración no devolvió control y se
  interrumpió; no se acredita una salida limpia de esa ejecución.
- Perfil interactivo mejorado real: 4K, Full, detalle alto, 200% y dos sectores
  extra. Siguió abierto durante ocho segundos y avanzó la intro; se terminó solo
  esa instancia de diagnóstico. No se condujo una carrera con todas las opciones.

Logs locales ignorados: `build/startup-debug-before.log`,
`build/startup-debug-after.log` y `build/startup-debug-interactive.log`.
Esto confirma la corrección del acceso que impedía arrancar; no sustituye la
comparación visual/rendimiento de las nuevas mejoras ni valida todos los modos.

## Sensación de lentitud al cambiar F6 de nativo a 4K

El usuario precisó que solo cambia F6: las opciones de geometría y el modo
original de Expansion Pak permanecen iguales. Por tanto, comparar legacy con
el perfil mejorado no aislaría este síntoma. Se debe comparar la misma carrera
con F6 en nativo/1080p/4K, conservando el resto de la configuración.

Hallazgo confirmado en código: el antiguo FPS del título era el incremento de
`total_vis` por segundo, es decir, frecuencia VI. Puede marcar 60 aunque se
repita una imagen o lleguen menos listas gráficas. No mide ni pasos de física
ni cuadros realmente mostrados por la pantalla. El backend llama
`Present(0, DXGI_PRESENT_DO_NOT_WAIT)`; no solicita espera por VSync. Esto no
excluye latencia del compositor o del controlador.

4K 4:3 dibuja 2880×2160, cuatro veces los píxeles de 1440×1080. Su rasterizador
compute evalúa comandos/materiales por píxel; además hay carga de preparación,
transferencias y readbacks para ciertos efectos. La cola pendiente de snapshots
se limita en CPU, pero no hay una política explícita que limite por finalización
GPU todas las reproducciones ya enviadas. Puede aumentar el tiempo de dibujo
o la antigüedad visible. Es una hipótesis de este síntoma, no una medición nueva
de la carrera del usuario; no se atribuye todavía a una sola de esas causas.

Se reemplazó la etiqueta engañosa y se añadieron métricas en título/consola:

- VI Hz: frecuencia de interrupciones VI.
- GFX/s: tareas gráficas nativas completadas por segundo, no FPS de simulación.
- CPU raster ms: media de rasterizado nativo en el intervalo. Excluye preparación
  del juego, copia del snapshot y trabajo de GPU.
- GPU raster ms: media de las nuevas muestras completadas de timestamp GPU en
  el último sondeo que obtuvo resultados. No se usa tiempo de envío CPU como
  sustituto. `n/a` significa que no hay muestra disponible; no significa 0 ms.
- HD snapshot age (consola): edad del snapshot al enviarlo a presentación,
  no latencia completa hasta la pantalla ni una medida entrada-a-fotón.

Los timestamps se consultan sin espera en el trabajador propietario del device;
la UI recibe valores atómicos. Se eliminó la lectura de triángulos mutable desde
la UI en ese título. No se cambió VSync, la sincronización SP/DP ni la velocidad
del juego para ocultar la lentitud.

### Ejecuciones locales del agente (2026-10-23)

Se agregó `--headless --performance-check --seconds N`: navega automáticamente
hasta la primera carrera, lee las mejoras del INI como una sesión interactiva,
crea una ventana DXGI real y conserva la resolución indicada por
`--render-height`. No combina los cambios automáticos de los otros modos de
diagnóstico. Los logs y las capturas se guardan con el ejecutable de diagnóstico.
Usar una copia del paquete y del INI en una carpeta aislada: el juego puede
generar guardados y capturas. Se ejecutaron las pruebas en carpetas independientes
bajo `build/performance-check`, sin tocar los guardados del usuario.

Tres ejecuciones iniciales de 30 s: nativo, 1080p y 4K, todas con Full, detalle
alto, distancia 200% y dos sectores adicionales. Llegaron a Mode 2 y salieron
con código 0. La navegación no es una reproducción determinista cuadro a cuadro.
La GPU detectada fue NVIDIA GeForce RTX 3080 Ti. Las muestras de carrera mostraron
unas 12–13 tareas gráficas/s en los tres modos, con VI alrededor de 60 Hz.
GPU: aproximadamente 2 ms en 1080p y 6–7 ms en 4K; CPU nativa: 63–77 ms.
No hubo readbacks finales ni agotamiento del anillo de timestamps en HD.

Se repitieron nativo y 4K durante 24 s, bombeando mensajes de ventana cada 2 ms
entre entradas automáticas (el diagnóstico antiguo solo bombeaba cada 100 ms).
Medias de muestras completas de T=15 a T=23, con la misma configuración:

| Modo | VI Hz | Tareas gráficas/s | CPU nativa ms | GPU ms | Edad snapshot ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Nativo / Full | 60.04 | 12.68 | 67.34 | — | — |
| 4K / Full | 59.93 | 12.60 | 67.80 | 6.98 | 45.56 |
| 4K / Off (solo diagnóstico adicional) | 59.99 | 21.04 | 34.57 | 7.22 | 37.56 |

Las tres salieron con código 0. Off mejora el coste nativo, pero cambia el modo
original y no aísla la diferencia de F6; no se cambió la preferencia del usuario.
En 4K/Full el replay HD costó 19.4 ms de CPU de media sobre toda la ejecución.
La edad incluye snapshots repetidos y no equivale a latencia entrada-a-pantalla.
Se abrió y revisó una captura de carrera nativa y la salida DXGI de 4K. La
ventana de prueba era 640×480, con render interno 2880×2160: esto no comprueba
el comportamiento del compositor en pantalla completa ni la sensación al conducir.
Los datos no reproducen una caída de ritmo de tareas exclusiva de 4K. Sí revelan
un cuello de botella nativo por CPU y trabajo adicional de preparación HD.

Optimización aplicada a `RenderSurfaces::import`: calcula una sola vez las
coordenadas horizontales de muestreo y copia filas ampliadas idénticas. Conserva
las mismas divisiones flotantes y truncamientos de la implementación original.
Una comprobación C++ temporal comparó todos los píxeles con la fórmula anterior
en 48 importaciones: anchos 320/640, altos 120/240/480, render nativo/720/1080/2160
y dos patrones, incluyendo escrituras de CPU entre tareas. Todas coincidieron.
No cambia el rasterizador nativo, las físicas ni la selección de resolución.

Una corrida posterior de 24 s en 4K/Full, mismo perfil y carpeta aislada, salió
con código 0. El replay HD medio de toda la ejecución bajó de 19.4 a 13.3 ms
(aproximadamente 31%). En T=15–23: VI 59.86 Hz, GFX 12.62/s, CPU nativa 67.56 ms
y GPU 6.52 ms. La edad muestreada fue 51.67 ms: no mejoró de forma consistente
y contiene cuadros repetidos. Por tanto, se confirma menor trabajo de replay,
no una corrección demostrada de fluidez ni una reducción global de latencia.
No hubo skips por swapchain ocupado, readbacks finales ni agotamiento de
timestamps. Queda investigar y optimizar el rasterizado nativo por CPU; también
falta repetir en pantalla completa y medir el ritmo de presentación/entrada.
El apagado de estas corridas completó normalmente, sin GDB.

Evidencia ignorada: `build/performance-check/{native,1080,2160,native-pump,4k-pump,4k-off,4k-optimized}/run.log`;
captura `4k-pump/framebuffer_performance_hd.png` y comprobación temporal
`build/performance-check/import-equivalence.cpp`. No se incorporan capturas,
ROM, guardados ni binarios de diagnóstico al código público.

### Optimización posterior del rasterizador CPU

El muestreo instrumentado de una tarea real señaló `sample_tile` y
`select_texture_lod` como costes importantes (el tiempo inclusivo del sampler
contiene los fetches). Una tentativa con gprof no produjo muestras útiles en
este entorno Windows; no se usa como evidencia. Se preservó un benchmark
Release anterior y se capturaron las entradas de las tareas de carrera 16, 60
y 120 antes de modificarlas. El benchmark restaura los mismos 8 MiB y estado
RSP/RDP antes de cada repetición, sin audio, ventana ni temporización del juego.

Cambios de implementación:

- Direcciones con máscara: reducción binaria equivalente al módulo positivo de
  potencia de dos, incluyendo coordenadas negativas, clamp y mirror.
- Shifts de texturas: multiplicación por factores exactos de potencia de dos,
  evitando llamadas repetidas a `ldexp` para los 16 shifts válidos de RDP.
- Filtrado de tres puntos: omite fetches de vecinos de peso cero; conserva las
  cuatro muestras del centro especial G_TF_AVERAGE y el orden de acumulación.
- LOD: usa el exponente del float en el caso común. Cerca del límite superior
  de cada intervalo conserva log2f, que puede redondear hacia el entero siguiente.
  Conserva la fracción y el escalado originales; no cambia la distancia/modelos.
- No calcula derivadas por píxel si el LOD de texturas está desactivado; los
  flags de filtrado se resuelven una vez por triángulo.

Medias de diez reproducciones por tarea (una previa excluida):

| Tarea capturada | CPU anterior ms | CPU optimizada ms | Salida RDRAM |
| --- | ---: | ---: | --- |
| 16 | 69.5289 | 34.5363 | 8 MiB idénticos |
| 60 | 68.2779 | 34.0346 | 8 MiB idénticos |
| 120 | 61.9706 | 30.8045 | 8 MiB idénticos |

Cada benchmark comprobó también repetibilidad interna. La comparación binaria
entre versiones cubrió toda RDRAM, incluidos framebuffer, profundidad y escrituras
intermedias visibles al juego; no solo una captura escalada.

Pruebas nuevas de referencia: 620446 valores de footprint (alrededor de las
potencias de dos y aleatorios), con flags LOD/detail/sharpen y límites variados;
50000 casos de shifts/filtrado/formato y 50000 de módulo con coordenadas negativas.
Pasaron, junto con la batería existente de CPU, GPU y presentación DXGI. La paridad
GPU admite sus tolerancias existentes de cuantización; no se afirma igualdad
binaria CPU/GPU para todos los materiales.

Dos carreras posteriores de 24 s, perfil Full/alto/200%/dos sectores, con salida
normal código 0. Medias de muestras completas T=15–23:

| Render | VI Hz | Tareas/s | CPU nativa ms | GPU ms | Edad snapshot ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Nativo | 60.10 | 22.82 | 33.20 | — | — |
| 4K | 59.99 | 22.00 | 33.19 | 7.49 | 23.22 |

Antes, 4K con la primera optimización de importación daba 12.62 tareas/s,
67.56 ms de CPU y edad muestreada 51.67 ms. La carrera completa mejora, aunque
no es un replay determinista y las edades muestreadas no miden entrada-a-fotón.
La captura DXGI posterior se abrió y revisó. No se alteraron los valores del INI,
las físicas, el reloj nativo ni los avisos de finalización SP/DP.
Persiste trabajo para alcanzar mayor ritmo; no se acredita 60 cuadros/s, todas
las pistas, pantalla completa ni multijugador. El cambio está compilado localmente,
sin publicar. Evidencia en `build/performance-check/cpu-capture`,
`cpu-optimized-native` y `cpu-optimized-4k`; herramienta pública propia
`tools/cpu_raster_benchmark.cpp` (capturas y buffers siempre privados).

Fuentes: inspección del código generado localmente de USA v0 en las funciones
y direcciones indicadas; no se copian cuerpos generados ni datos de la ROM al
árbol público. Implementación propia en `src/graphics_enhancements.cpp` y hooks
en `config/topgearoverdrive.us.v0.toml`.
