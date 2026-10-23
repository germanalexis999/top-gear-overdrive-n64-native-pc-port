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

### Revisión del diagnóstico aportado: GPU nativa y esperas VI

Estado: cambio local posterior a preview.11, sin publicar. Se revisó el análisis
del usuario contra el código del host, el C generado local y mediciones A/B.

| Afirmación | Resultado |
| --- | --- |
| VI Hz no son FPS | Confirmado; el título ya los separaba. |
| GFX/s es exactamente el framerate del motor | Cuenta tareas nativas completadas, no entradas ni imágenes visibles. En esta ruta se aproxima al ritmo gráfico, pero no acredita FPS de simulación/pantalla. |
| CPU y GPU dibujan la escena | Confirmado. La pasada nativa proporciona buffers y conserva el contrato de finalización; borrarla sin sustitución es incorrecto. |
| Se copian dos veces 8 MiB en submit/queue_screen | Incorrecto en preview.11: submit copia 8 MiB una vez, queue_screen copia ancho×alto×2 del scanout. |
| Prioridad baja añade siempre 15–33 ms | No demostrado. La prioridad afecta la selección de hilos listos, no define una penalización temporal fija. |
| El mutex se mantiene durante Present | No: las llamadas DXGI se realizan fuera del mutex de cola. Había destrucción de objetos reemplazados bajo el mutex; se movió fuera. |
| Full impone por sí solo 2–3 VIs | No confirmado. El presupuesto observado es 2 VIs tanto con Full como con Off; hay espera original en la lógica de tareas. |
| Estas correcciones garantizan menos de 2 ms | No. El tiempo completo incluye interpretación, copias, envío, sincronización GPU y readback. |

Fuentes de Windows: [prioridades de planificación](https://learn.microsoft.com/en-us/windows/win32/procthread/scheduling-priorities)
y [sincronización al acceder a recursos de GPU](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/copying-and-accessing-resource-data).
En D3D11, Map de staging puede esperar a que termine la copia; por ello el
tiempo de envío no equivale al tiempo de finalización.

#### Cambio de implementación

El backend D3D11 propio ahora admite targets nativos a escala 1. La ruta GPU
nativa interpreta sobre una copia privada de los 8 MiB. Los readbacks de efectos
y el readback final completan color/profundidad en esa copia. Después, el commit
copia solo las regiones de targets escritos a la RDRAM del juego. El aviso SP/DP
continúa después de ese retorno: no se libera prematuramente ni se espera a HD.
El trabajador HD y el nativo tienen dispositivos/contextos separados, cada uno
usado por su hilo propietario; no comparten un immediate context entre hilos.

El dibujo nativo y el HD siguen siendo dos pasadas necesarias en esta estrategia,
pero la nativa ya no recorre todos los píxeles en CPU. La referencia CPU continúa
disponible con `[Graphics] NativeRasterizer=cpu`, y legacy siempre la selecciona.
El perfil mejorado inicia con `gpu` si falta la clave. F6 sigue siendo independiente
y las comparaciones de resolución conservan CPU como referencia.

Si el dispositivo no está disponible, se usa CPU. Si falla una tarea, se restaura
el estado inicial del intérprete y se reintenta en CPU; la RDRAM viva aún no recibió
sus escrituras parciales. Se desactiva esa ruta GPU durante el resto de la sesión.
Se rechazan targets nativos parcialmente superpuestos o anchuras no soportadas.
El commit valida todas las regiones antes de copiar, exige readbacks terminados
y no restaura los 8 MiB completos: así preserva escrituras concurrentes de audio
y del juego fuera de sus buffers. La nueva ruta nativa sí requiere otra copia
de 8 MiB para ese aislamiento; no se presenta como eliminación de todas las copias.

El trabajador HD usa prioridad normal. Las colas siguen limitadas al último
pendiente; swaps de propietarios permiten liberar buffers antiguos fuera del
mutex. Se mide la copia de snapshot HD: ~0.7–0.8 ms por 8 MiB en estas corridas,
no ~33 ms. El título cambia CPU raster por Native task: es tiempo de pared de
la tarea nativa, incluyendo GPU/readback cuando se usa ese backend.

#### Evidencia de ritmo original

La inicialización de eventos VI en `0x80001D44–0x80001D48` pasa frecuencia 1 a
osViSetEvent. No pide notificaciones cada dos/tres VIs al seleccionar Full.
La tarea de gráficos original lee el float `0x80051EC4` en `0x800019CC–0x800019FC`
y consume mensajes adicionales en `0x80001A10–0x80001A54`. Ese campo puede
configurarse mediante mensajes, por ejemplo `0x8000102C–0x80001078`, donde la
opción por defecto vale 2. No se publica el cuerpo generado ni se parchean estos
puntos. El diagnóstico solo lee el presupuesto y la opción `0x80102344`.

Con GPU nativa, Full y Off dieron `VI_budget=2` durante la carrera, y el render
nativo sin HD llegó a ~29.9 tareas/s. Esto respalda una cadencia original cercana
a 30 tareas/s en este caso. No demuestra todos los modos, ni que toda caída a
20 tareas/s sea un limitador de Full. Forzar 60 requiere estudiar la separación
entre actualización, dibujo y temporización; no se hizo un parche arbitrario.

#### Comparaciones y límites

Tres tareas idénticas capturadas, diez repeticiones tras calentamiento:

| Tarea | CPU optimizada ms | GPU nativa con readback ms |
| --- | ---: | ---: |
| 16 | 34.54 | 2.53 |
| 60 | 34.03 | 2.51 |
| 120 | 30.80 | 1.96 |

No son buffers binariamente idénticos entre CPU y GPU. En toda RDRAM, las
diferencias se limitaron a las páginas de color/profundidad: 84/399/1021 píxeles
de color diferentes y 45/54/32 de profundidad. Máximo RGB: 1/2/2 niveles de
5 bits, con 0/1/5 píxeles de error mayor que un nivel; alfa sin cambios.
Profundidad: máximo 1 unidad. Son diferencias de cuantización observadas, no
una promesa de equivalencia completa en todos los efectos.

A/B con el mismo ejecutable, Full/4K/alto/200%/dos sectores y muestras T=15–23:

| Nativo | Prioridad HD | Tareas/s | Tiempo nativo ms | HD GPU ms | Edad ms |
| --- | --- | ---: | ---: | ---: | ---: |
| CPU | Normal | 22.71 | 33.38 | 6.69 | 27.33 |
| GPU | Normal | 26.78 | 3.18 | 6.65 | 23.88 |
| GPU | Inferior | 27.93 | 3.22 | 6.64 | 15.56 |

La variación entre carreras no demuestra que subir prioridad reduzca latencia;
de hecho, no mejoró estas medias. La mejora nativa procede de sustituir el
rasterizado software pesado. No se atribuye el salto a un quantum fijo de Windows.

Corridas con guardas de commit/alias y presupuesto instrumentado:

| Render | Original | Duración | VI Hz | Tareas/s | Nativo ms | Presupuesto VI |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 4K | Full | 40 s | 60.02 | 28.20 | 3.29 | 2 |
| 4K | Off | 24 s | 59.96 | 28.24 | 3.08 | 2 |
| Nativo | Full | 24 s | 60.00 | 29.86 | 3.30 | 2 |

Las medias corresponden a T=15–23, no a toda la duración. Las tres salieron con
código 0; tareas GPU completadas 1316/837/879, sin reintentos por fallo, y audio
con desbordes/entradas silenciosas descartadas/cuadros pendientes reemplazados
0/0/0. No son replays deterministas ni mediciones de latencia entrada-a-fotón.

Pasaron las regresiones CPU, GPU y presentación. Se añadieron casos nativos
320×240 y 640×120/240/480 con lectura completa, rechazo de commit prematuro,
escrituras exclusivamente en targets, preservación de RDRAM ajena y rechazo de
superposición parcial. Una primera fixture falló porque sus buffers de color y
profundidad de 640×480 se superponían; se corrigió su distribución y se añadió
la guarda de alias. Las corridas reales anteriores no encontraron ese caso.
La captura automática inicial se tomó durante negro de transición: se ajustó
para esperar 60 tareas de carrera y una salida compatible visible. La captura
estable nativa se revisó y mostró pista, auto y HUD.

Comprobaciones finales tras ajustar la captura: perfil mejorado con INI sin
NativeRasterizer explícito (20 s, 706 tareas GPU), cambios de resolución/ventana/
filtro y F11 con frenado/derrape (30 s, 1075 tareas GPU), y legacy con override
CLI GPU (4 s: CPU forzada, cero tareas GPU). Las tres salieron con código 0,
sin fallos GPU ni desbordes/descartes de audio; las dos alternancias F11 y la
supresión de repetición dieron PASS. La captura DXGI corregida se abrió y mostró
pista, auto y HUD. Logs en `gpu-default-ini`, `gpu-switches` y `gpu-legacy`
bajo `build/performance-check`. No equivalen a validación de toda la física.

Logs privados: `build/performance-check/{gpu-native-normal,cpu-native-normal,gpu-native-below,gpu-full-final,gpu-off-final,gpu-native-final}/run.log`.
Regresiones en `build/native-{gpu,cpu}-regression.log`. Quedan conducción manual,
todos los circuitos, modos y confirmación del tambaleo del usuario. No se cambia
la física, el presupuesto VI ni la preferencia de resolución original para
obtener estas cifras. El ZIP preview.11 ya publicado conserva la versión previa.

Fuentes: inspección del código generado localmente de USA v0 en las funciones
y direcciones indicadas; no se copian cuerpos generados ni datos de la ROM al
árbol público. Implementación propia en `src/graphics_enhancements.cpp` y hooks
en `config/topgearoverdrive.us.v0.toml`.
