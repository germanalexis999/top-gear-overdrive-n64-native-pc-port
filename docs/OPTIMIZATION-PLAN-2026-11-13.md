# Plan de optimización del port

Fecha: 2026-11-13. Fuente revisada: commit
`8ab3e9de59be7443372926d790f99466391c8519`, árbol inicialmente limpio.
El análisis inicial propone trabajo a partir de esa revisión; el avance posterior
se registra a continuación.

**Avance posterior:** P01 incorpora repeticiones y percentiles en el benchmark,
y contadores agregados de preparación, uploads y Map en la instancia GPU HD;
quedaban pendientes percentiles por etapa, el replay HD y el formato de captura
estable, abordados en avances posteriores. P02 y P03 tienen implementaciones candidatas de máscaras y bins planos;
P04 encola las copias antes de mapearlas. P12 ofrece IPO experimental, apagado
por defecto. Las pruebas CPU, GPU y presentación pasaron. Tres tareas de una
carrera reprodujeron exactamente los 8 MiB de RDRAM antes/después en GPU; los
tiempos fueron variables y no demuestran una ganancia. También hubo una carrera
automática de 26 s con 1080p y GPU nativa, sin reintentos GPU. Faltan pruebas
ampliadas de máscaras, comparación A/B con suficientes pares y el resto de las
etapas; estas modificaciones aún son candidatas, no una optimización validada.

**Avance adicional:** P05 importa targets HD desde píxeles nativos y los expande
en GPU con tablas de coordenadas generadas en CPU; las superficies ya existentes
en CPU conservan su contenido mediante la ruta anterior. La prueba sintética
comparó todos los píxeles para los cuatro layouts nativos en 720p/1080p/4K.
P06 reutiliza un buffer de pantalla de reserva; P07b evita derivadas si no se
activa LOD. El benchmark GPU reproduce HD y verifica repetibilidad fuera de la
medición. En carreras automáticas separadas de 26 s a 1080p, la media de replay
HD CPU bajó de 3,7 a 1,1 ms; el intervalo GPU subió de 2,1 a 2,5 ms. Estas
medias incluyen arranque y no prueban mejora global ni equivalencia visual en
todas las escenas. Pasó una ejecución de 30 s con alternancia de presentación.
Faltan pares A/B suficientes en ejecución real, validación manual, pruebas de otros adaptadores y
las tareas condicionales, por lo que el plan aún no puede darse por completo.

**Replay A/B posterior:** se compiló la referencia del commit inicial y el
candidato con el mismo harness. Las tareas 16, 60 y 120 dieron superficies HD
idénticas byte a byte a 720p, 1080p y 4K. Cinco pares alternados de la tarea 60,
30 repeticiones por ejecución, en RTX 3080 Ti dieron las siguientes medias de
las cinco ejecuciones. El p95 es la media de los p95 de cada ejecución; no es el
p95 conjunto. La salida y las capturas privadas permanecen bajo `build/`.

| Resolución | Versión | CPU envío medio | CPU p95 medio | Span GPU medio |
| --- | --- | ---: | ---: | ---: |
| 1080p | inicial | 3,284 ms | 4,816 ms | 1,912 ms |
| 1080p | candidata | 0,999 ms | 1,189 ms | 1,402 ms |
| 4K | inicial | 13,908 ms | 18,256 ms | 6,738 ms |
| 4K | candidata | 1,134 ms | 1,290 ms | 4,456 ms |

El span GPU es la query de la tarea completa, no un kernel aislado. La prueba
mide un replay con readback de verificación fuera del intervalo de CPU; no mide
FPS de carrera ni latencia de entrada. La candidata también precalcula por
comando el uso de segunda textura del combinador. Se mantienen pendientes la
validación en otros adaptadores, escenas y condiciones interactivas.

**Comprobaciones y experimentos adicionales:** el shader de coordenadas de
producción se ejecutó en GPU para 1024 combinaciones de máscara, clamp, mirror
y extremos con signo; coincidió exactamente con el módulo de referencia. Una
fixture adicional comprueba un combinador de dos ciclos con texturas distintas.
El benchmark permite exportar la superficie HD privada y registra muestras de
timestamps GPU completadas, además de los percentiles CPU.

Se evaluó acumular localmente los contadores del rasterizador CPU: cinco pares
de 30 repeticiones de la tarea 60 conservaron los 8 MiB de RDRAM, pero el tiempo
medio pasó aproximadamente de 33,6 a 34,2 ms. Durante la comparación se estaba
compilando IPO en segundo plano, por lo que no es una medición aislada; ante la
ausencia de mejora demostrada se descartó el cambio. Se compiló
IPO y se ejecutó una carrera automática de 26 s, sin fallos GPU ni desbordes de
audio; una corrida comparable sin IPO tampoco mostró una diferencia convincente.
IPO permanece desactivado por defecto; faltan A/B y pruebas representativas antes
de recomendarlo. El ejecutable IPO observado ocupó 3.953.533 bytes frente a
3.815.125 bytes del build normal de esa comparación.

**PGO experimental (P12):** se añadieron modos GCC `GENERATE` y `USE`, apagados
por defecto, documentados en `BUILDING.md`. El entrenamiento aislado incluyó
intro (38 s), gráficos/carrera (30 s) y carrera con GPU (26 s), y produjo 22
archivos de perfil. El primer build USE rechazó contadores inconsistentes; se
utilizó `-fprofile-correction`, conservando el error por perfiles ausentes.
La corrección aproxima datos inconsistentes; no convierte ese entrenamiento en
representativo de todos los modos. El port solicita el volcado explícito antes
de `quick_exit`, tras cerrar sus trabajadores; `ultramodern::quit()` no garantiza
por sí solo que todos los hilos emulados hayan terminado.

Cinco pares alternados de 24 s en 1080p con GPU nativa, muestras T=15–23 de
carrera (45 muestras por variante), dieron medias normal/PGO de 29,182/29,064
tareas por segundo, 3,464/3,344 ms por tarea nativa y 2,300/2,289 ms de span
GPU. Las diez ejecuciones terminaron con código 0, sin reintentos GPU ni
desbordes/descartes/reemplazos de audio. Los recorridos no son deterministas,
solo cubren una carrera y la medición no es latencia de entrada ni p95 global.
La diferencia es pequeña frente a la variación: PGO permanece optativo y no se
recomienda como configuración de distribución hasta entrenar y validar más
circuitos, efectos y multijugador. Una prueba adicional de 30 s con PGO, GPU
nativa, cambios de resolución, filtros, tamaño de ventana y F11 salió con código
0, 1.084 tareas GPU, cero reintentos y cero desbordes/descartes de audio; las
dos comprobaciones de pantalla completa dieron PASS. Datos y ejecutables de
la prueba permanecen en `build/`.

**Rechazo temprano de triángulos (P07c):** se adelantó la comparación de Z
antes del muestreo de texturas, manteniendo la escritura de profundidad después
de aceptar alpha/mezcla. Las nueve superficies de las tareas 16/60/120 a
720p/1080p/4K fueron idénticas a la versión anterior. Se añadieron casos de alpha
rechazado seguido de geometría visible y de color/profundidad en el mismo target.
Cinco pares de 30 repeticiones de la tarea 60 dieron span GPU medio anterior y
nuevo de 1,388/1,320 ms a 1080p y 4,225/4,098 ms a 4K. La media de los p95
del envío CPU a 4K pasó de 1,197 a 1,331 ms; no mide p95 global de carrera.
El cambio queda como candidato pendiente de comparar escenas y adaptadores, no
como una ganancia garantizada en el juego.

**Captura explícita (P01):** la versión 2 serializa campos del intérprete con
orden de bytes definido, sin punteros, objetos de superficies ni padding; valida
el tamaño y los límites de las pilas. El benchmark conserva soporte para versión
1 compatible y puede convertirla con `--export-v2`. Las pruebas verificaron ida
y vuelta y rechazo de archivos truncados, sobredimensionados y versiones no
soportadas. Tres tareas convertidas produjeron la misma RDRAM en CPU y GPU y las
mismas superficies HD a 720p/1080p/4K que sus originales. También se generaron
y reprodujeron tres capturas v2 de una carrera automática. El formato sigue
siendo diagnóstico privado; no es un save-state ni contiene datos publicables.

**Bins estables (P03):** el armado se aisló en un helper comprobable. Cien
fixtures de tamaños y rectángulos variados, una con 8.192 comandos, compararon
spans y orden con la implementación de referencia; también se verificaron
reutilización de capacidad y rechazo de comandos fuera de límites. Las nueve
superficies HD de las tres tareas reales permanecieron idénticas a la referencia.

**Tamaño de bin (P08):** variantes aisladas de 16, 32 y 64 mantuvieron imágenes
idénticas para las tareas 16/60/120 en 720p/1080p/4K. Cinco rondas de 30
repeticiones de la tarea 60, alternando el orden y usando el mismo compilador y
flags, dieron los siguientes promedios. El p95 es la media de los cinco p95, no
el percentil combinado. Las cifras son replays en una RTX 3080 Ti.

| Resolución | Bin | Envío CPU medio | CPU p95 medio | Span GPU medio |
| --- | ---: | ---: | ---: | ---: |
| 1080p | 16 | 1,117 ms | 1,273 ms | 1,352 ms |
| 1080p | 32 | 1,064 ms | 1,232 ms | 1,331 ms |
| 1080p | 64 | 1,037 ms | 1,227 ms | 1,320 ms |
| 4K | 16 | 1,424 ms | 1,729 ms | 4,096 ms |
| 4K | 32 | 1,158 ms | 1,443 ms | 4,051 ms |
| 4K | 64 | 1,109 ms | 1,342 ms | 4,088 ms |

Se conserva 32: 16 aumentó la preparación CPU y 64 no mostró una mejora
consistente suficiente en GPU para justificar cambiar el comportamiento en
otras escenas o adaptadores. No se modificó el grupo de hilos de 8×8.
En la carrera automática 1080p instrumentada hubo 498.895 bins vacíos de
2.772.360 (18,0%), no una mayoría; los uploads acumularon 247.385 µs para
943 replays (0,262 ms/replay). Es un promedio de toda esa sesión, no prueba que
el upload nunca limite una escena. Por ahora no se añade un anillo de recursos
ni un dispatch compacto: faltan perfiles que justifiquen su complejidad.

**Readback agrupado (P04):** se compilaron variantes aisladas con lectura
secuencial y agrupada, ambas con el renderer y los flags actuales. La RDRAM
completa de las tareas nativas 16/60/120 fue idéntica. Cinco pares alternados de
30 repeticiones de la tarea 60 dieron una media CPU de 2,250/2,103 ms y una
media de los p95 de 2,702/2,455 ms, respectivamente; el span GPU medio fue
0,747/0,758 ms. Son tiempos aislados de una RTX 3080 Ti y no demuestran FPS de
carrera. La variante secuencial usa el código anterior sin la instrumentación
de espera Map, lo que debe considerarse al interpretar la comparación.

El mapeo de staging usa una guarda RAII que garantiza `Unmap` incluso si falla
una asignación durante la conversión o una captura; se conservaron las mismas
barreras y los tests existentes de feedback y presentación.

**Instantáneas (P09):** en la carrera automática 4K de 26 s, la copia HD de
8 MiB promedió 0,5 ms; la copia privada nativa acumuló 422.738 µs en 941 tareas
(aproximadamente 0,45 ms por copia), sin snapshots HD reemplazados. Son tiempos
de pared de esa sesión, no resultados universales. No se comparte RAM mutable ni
se introduce copy-on-write: esos datos no justifican asumir el riesgo de cambiar
el contrato de audio/juego. En esa misma corrida se registraron cero desbordes,
descartes de silencio o muestras pendientes reemplazadas; no reemplaza una
prueba auditiva prolongada ni un perfil de latencia de entrada (P11).

## 1. Decisión y alcance

Conservar el backend D3D11 y mejorar sus transferencias, preparación de comandos
y trabajo por píxel. Primero medir; después implementar cambios pequeños y
comparables. El objetivo es reducir tiempo de tarea, consumo de memoria y
variación entre cuadros, manteniendo imagen, audio y simulación.

No hay evidencia nueva suficiente para prometer un porcentaje de mejora o
60 FPS. Las mediciones históricas del repositorio sitúan la carrera estudiada
cerca de 30 tareas gráficas/s, con presupuesto VI de 2. VI a 60 Hz no equivale a
60 imágenes nuevas/s. Aumentar esa cadencia requiere otro proyecto sobre la
temporización del juego; no debe mezclarse con este plan.

La prioridad inicial es **P01 → P02 → P03 → P04**. P05 tiene mayor potencial
para HD/4K, pero se divide porque modifica la residencia de las superficies.
P06 reduce asignaciones del host. El resto depende de los perfiles obtenidos.
Las prioridades expresan beneficio probable frente a riesgo, no ganancias medidas.

### Qué se comprobó en esta revisión

- Lectura del host, intérprete F3DEX2/L3DEX2, shaders, cachés de superficies,
  entrada SDL, audio WaveOut, mejoras gráficas, CMake, parche del runtime,
  benchmarks y pruebas; contraste con la documentación histórica.
- Compilación Release de `tgod_regression_checks`, `tgod_gpu_checks`,
  `tgod_cpu_benchmark` y `tgod_native_gpu_benchmark` en `build-checks`: código 0.
- Ejecución nueva de `tgod_regression_checks`: PASS, código 0.
- Ejecución nueva de `tgod_gpu_checks`: PASS, código 0, NVIDIA GeForce RTX 3080 Ti.
  Incluyó paridad con tolerancias existentes, commit nativo, preservación de RAM
  ajena, rechazo de alias parcial y presentación nativa/720p/1080p/4K.
- El compilador HLSL emitió X3556 por módulo/división enteros. Es una pista;
  no demuestra qué proporción del tiempo ocupan esas instrucciones.
- Auditoría `check_public_tree.py --worktree`: PASS, 75 archivos de texto;
  `git diff --check`: sin errores. Se añadió este documento al manifiesto
  explícito y se enlazó desde README; no se cambiaron fuentes del juego.
- No se ejecutaron carreras ni benchmarks con capturas del juego en esta
  revisión. No se encontraron `cpu_task_*` en `build/` o `build-checks`.
  Los benchmarks se compilaron, pero no se ejecutaron. Las cifras históricas
  que siguen no constituyen una medición de este equipo en esta sesión.

## 2. Arquitectura actual que debe preservarse

```text
Hilo gráfico del runtime
  process_graphics_task
    1. Captura HD de RDRAM (8 MiB), si hay HD; publica último trabajo pendiente.
    2. Nativo GPU: otra copia privada de 8 MiB → interpreta → rasteriza.
       Lee targets a CPU → valida todos → copia solo targets a RAM viva.
       Nativo CPU: interpreta y rasteriza directamente como referencia/fallback.
    3. Retorna send_dl → SP completo → DP completo.

Trabajador HD independiente
  snapshot privado → interpreta a resolución HD → D3D11 → presentación DXGI.
  Las cargas de texturas desde framebuffer siguen provocando readback.
  Un trabajo en curso y como máximo un pendiente; generaciones invalidan salidas.

VI/update_screen
  captura scanout nativo para fallback → trabajador de presentación.
  Conserva también PresentedFrame para capturas/ruta CPU.
```

Referencias: `src/main.cpp`, `process_graphics_task`, `AsyncHDRenderer`,
`Win32RendererContext::update_screen`; `src/gpu_raster.cpp`, `finish`,
`readback`, `commit_native_results`; `patches/n64modernruntime.patch`.

Ya existen y **no son trabajo pendiente**: caché de hasta ocho superficies GPU,
buffers de entrada con capacidad reutilizable, staging creado bajo demanda,
presentación DXGI sin readback final HD en la ruta directa, supresión de
presentaciones repetidas, queries GPU sin espera y renderizador nativo GPU.
El CPU ya optimizó máscaras, shifts, filtrado de tres puntos y selección de LOD.
`docs/RENDERER-ARCHITECTURE.md` contiene análisis anterior a varios de esos cambios.

### Contratos no negociables

1. Finalizar consumo nativo y commit antes de SP/DP. Nunca esperar HD o Present
   para completar la tarea nativa; tampoco adelantar SP para ganar tiempo aparente.
2. Conservar RAM privada para la ejecución GPU nativa y fallback transaccional:
   validar todos los targets antes del primer commit; nunca restaurar 8 MiB sobre
   RAM viva, porque borraría escrituras concurrentes de audio/juego.
3. Mantener orden de primitivas dentro de cada píxel, mezcla, profundidad,
   microcódigo, TMEM/TLUT y barreras de framebuffer. No ordenar por material.
4. No cambiar resolución lógica, VI, física, FPU, distancias, LOD o sectores para
   atribuir esa reducción de calidad a una optimización equivalente.
5. Un propietario por immediate context y por operaciones DXGI. Los dispositivos
   nativo y HD son separados; compartirlos requiere un diseño distinto.
   Microsoft documenta esta restricción de los contextos y recomienda ejecutar
   DXGI/Present en el mismo hilo propietario del contexto. [Fuente oficial](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro).
6. Capacidad reutilizable no implica contenido vigente. Direcciones iguales en
   tareas distintas pueden contener datos diferentes; invalidar por generación,
   dimensiones y contenido según corresponda.

## 3. Hallazgos y prioridades

Las líneas son orientativas para el commit revisado; localizar por símbolo al implementar.

| Lugar | Hecho observado | Oportunidad / cautela | Tarea |
| --- | --- | --- | --- |
| `gpu_raster.cpp:177`, `Impl::surface`; `render_surface.hpp:24`, `import` | Obtiene un buffer HD CPU, lo amplía, convierte uint16 a uint32 y sube toda la superficie antes de dibujar | Subir origen nativo y expandir en GPU; potencial alto en 4K, feedback delicado | P05 |
| `main.cpp:508,538`, `recycle`/`run` | Limpia superficies CPU del replay HD; `clear` destruye sus vectores internos | El cache GPU sobrevive, pero los buffers HD CPU vuelven a asignarse | P05a |
| `gpu_raster.cpp:229`, `flush` | Usa `vector<vector<uint32_t>>`, después copia sus contenidos a `refs` | Construir índices contiguos en dos pasadas, preservando el orden | P03 |
| `gpu_raster.cpp:410`, `readback` | Para cada superficie: CopyResource → Map bloqueante → conversión → siguiente | Encolar todas las copias necesarias antes del primer Map | P04 |
| `gpu_raster_shader.inc:15`, `coord` | Usa módulo con signo para periodos potencia de dos | Máscaras equivalentes incluso para coordenadas negativas | P02 |
| `gpu_raster_shader.inc:157–198`, `main` | Decodifica estado y calcula trabajo de texturas antes de rechazar por profundidad | Predecodificar invariantes y estudiar rechazo temprano seguro | P07 |
| `gpu_raster.cpp:210`, `upload` | Reescribe los mismos cuatro buffers DEFAULT entre lotes | Medir contención antes de introducir un anillo de recursos | P08 |
| `main.cpp:675,1090`, `queue_screen`/`update_screen` | Asigna Screen/vector por VI y mantiene otra copia para PresentedFrame | Pool acotado y propiedad clara; conservar fallback y capturas | P06 |
| `main.cpp:652,847` | Copias HD y nativa de 8 MiB por tarea con ambos backends activos | Medir por separado; no compartir memoria mutable ingenuamente | P09 |
| `f3dex2.cpp:652`, raster CPU | Recalcula cobertura por píxel; muchos contadores siguen activos sin logs | Optimización secundaria para legacy/fallback, preservando referencia | P10 |
| `main.cpp:368–380`, audio | Compacta vector mediante erase bajo mutex | Ring buffer solo si el perfil muestra coste o picos | P11 |
| `CMakeLists.txt`, compilador shader | Release ya usa -O3; HLSL nivel 3 e IEEE strict | IPO/PGO como experimento separado; no fast-math global | P12 |

### Volumen de memoria: cálculo, no benchmark

Para superficie completa 4:3, antes de considerar recortes y modos Half:

| Resolución | Píxeles | Buffer CPU uint16 | Buffer GPU/upload uint32 |
| --- | ---: | ---: | ---: |
| 1440×1080 | 1.555.200 | 2,97 MiB | 5,93 MiB |
| 2880×2160 | 6.220.800 | 11,87 MiB | 23,73 MiB |

Si una tarea importa color y profundidad completos a 4K, sube unos 47,46 MiB.
A 30 tareas/s serían 1,39 GiB/s de payload, sin contar staging ni las copias
internas del driver. Dos snapshots de RDRAM añaden 16 MiB/tarea, 480 MiB/s a
30 tareas/s. Son escenarios de volumen; no establecen tráfico real ni saturación.
En contraste, el contenido nativo máximo 640×480 ocupa 0,59 MiB en uint16 o
1,17 MiB si se expande a uint32 antes del upload.

La caché actual limita número de superficies, no bytes: ocho buffers GPU 4K
uint32 suman aproximadamente 190 MiB, antes de staging, CPU y otros recursos.
P01 debe registrar máximos; P05/P08 no deben introducir cachés sin presupuesto.

## 4. Protocolo común de medición y aceptación

### Línea base reproducible

Registrar commit, estado del árbol, compilador/flags, GPU/driver, CPU, modo de
energía, INI efectivo, resolución interna, ventana, backend nativo y prioridad HD.
Guardar resultados bajo un directorio ignorado, por ejemplo `build/optimization/`.
Usar una copia aislada del ejecutable con DLLs y configuración, porque el juego
escribe configuración, capturas y guardados junto al EXE. No usar una sesión
activa ni sobrescribir preferencias del usuario.

Separar tres tipos de ensayo:

| Ensayo | Procedimiento | Qué demuestra |
| --- | --- | --- |
| Replay determinista | Misma entrada/estado inicial, calentamiento excluido, al menos 30 repeticiones medidas al ampliar el harness | Coste del renderer y equivalencia; no latencia del juego |
| Ejecución real | Al menos cinco pares A/B alternados, mismo perfil; separar arranque, intro, menú y carrera | Contención, ritmo, audio, edades y estabilidad |
| Interacción | F5/F6/F11, resize, minimizar/restaurar, transiciones y multijugador | Validez de propiedad, generaciones y fallback |

Para ejecución real, proponer 15 s de calentamiento y 60 s de carrera medida
cuando la navegación lo permita. Seleccionar la fase por modo/tarea; dos carreras
iniciadas al mismo segundo del host no garantizan el mismo cuadro simulado.
Los ensayos headless actuales generan BMP, snapshots y WAV: identificar esos
intervalos o separar captura y medición para no atribuir E/S a renderizado.

Mantener constantes `LegacyVersion`, `OriginalResolution`, `ForceHighDetail`,
`DrawDistancePercent`, `ExtraTrackSections`, tamaño de ventana y filtros.
Evaluar primero CPU/nativo y GPU/nativo, luego GPU/720p/1080p/4K. Incluir perfil
original y mejorado; repetir escenas con humo, transparencias, líneas, HUD,
reflejos/feedback y 2–4 jugadores cuando existan capturas locales apropiadas.

### Métricas que importan

- CPU: p50/p95/p99 y máximo por tarea/etapa; medias para comparación histórica.
- GPU: intervalos con timestamps/disjoint, muestras válidas y omitidas. La query
  actual abarca una secuencia completa y puede incluir huecos de envío: no es
  tiempo aislado del kernel. `replay_us` del HD residente mide CPU/envío.
- Bytes de snapshot, import/upload y readback; asignaciones y memoria máxima.
- Dispatches, comandos, referencias por bin, bins vacíos, causas de flush,
  readbacks por feedback y al final, fallbacks y errores de dispositivo.
- VI/s, tareas nativas/s, imágenes únicas enviadas a Present, repeticiones,
  reemplazos pendientes y edad de snapshot al presentar. Present exitoso no
  demuestra que ese cuadro llegó al monitor ni mide entrada-a-fotón.
- Audio: desbordes, descartes, reemplazos y starvation; latencia de entrada si
  el trabajo afecta al host. No aceptar ganar renderizado empeorando audio.

El tiempo `Native task` actual comienza **después** de `g_hd_renderer.submit`.
Agregar la duración de `send_dl` completa para medir cuánto demora SP/DP,
además de las etapas internas. No sumar tiempos nativo+HD como si fueran serie.

### Regla de aceptación

Primero equivalencia; después rendimiento. Para transformaciones exactas, exigir
misma salida del mismo backend antes/después, incluidos profundidad, RAM ajena
y estado relevante del intérprete. Las tolerancias CPU↔GPU ya existentes no
autorizan degradar un cambio GPU↔GPU que debería ser exacto.

Como umbral inicial de decisión, usar mejora repetida mayor que la variabilidad
A/A; apuntar a ≥5% de la etapa atacada o a una reducción estructural demostrable
de bytes/asignaciones. El 5% es un criterio propuesto, no un resultado esperado.
No aceptar regresión repetible del p95 global, memoria sin límite, audio o
cadencia. Si el efecto queda dentro del ruido, archivar el experimento y mantener
la versión sencilla. No cambiar tolerancias para lograr PASS.

## 5. Tareas listas para implementar

### P01 — Instrumentación y benchmark confiable (obligatoria)

**Archivos:** `include/gpu_raster.hpp`, `src/gpu_raster.cpp`, `src/main.cpp`,
`tools/cpu_raster_benchmark.cpp`; un helper pequeño si evita duplicación.

**Pasos, en entregas separadas:**

1. Agregar contadores de bytes, causas de flush, tiempo CPU de bins/upload/Map/
   conversión y copia nativa. Diferenciar instancias GPU nativa y HD.
2. Acumular muestras en almacenamiento acotado por hilo y exportar fuera del
   camino crítico. No imprimir por primitiva ni usar un mutex global por píxel.
   `Map` incluye espera y coste de la API: etiquetarlo como tal.
3. Extender el replay con cantidad de iteraciones, calentamiento y salida de
   percentiles. Conservar el uso posicional actual. Agregar escala HD y modo
   residente: verificar salida una vez fuera del intervalo medido y medir GPU
   con queries completadas. En nativo, conservar readback dentro del intervalo.
4. Registrar identidad de tarea/generación y denominadores de cada estadística.
   Reutilizar el anillo de queries; no hacer spin ni Flush para cada muestra.
5. Antes de cambiar la disposición de `F3DEX2Interpreter`, resolver el formato
   de captura: hoy serializa bytes desde `layout_width` hasta el fin del objeto.
   Igual tamaño no garantiza mismo ABI. Usar versión y serialización explícita
   del estado necesario, sin punteros, contenedores ni padding; rechazar formatos
   incompatibles. No extender ese volcado crudo con campos dinámicos.

**Pruebas:** las dos suites actuales; replay repetible; captura truncada,
sobrante/incompatible y round-trip del nuevo estado. Verificar que restaurar
TMEM, matrices, tiles, stacks y cachés derivadas no cambie la siguiente DL.
**Aceptación:** medición A/A estable; saber dónde se gasta CPU/GPU y qué bytes se
transfieren. Si la instrumentación altera el perfil, usar muestreo o desactivarla
en la medición final. No confundir pruebas sintéticas con una carrera completa.

### P02 — Máscaras exactas en coordenadas del shader (pequeña)

**Depende de:** P01 para medir. **Archivos:** `src/gpu_raster_shader.inc`,
`tools/gpu_regression_checks.cpp`. **Símbolo:** `coord`.

Reemplazar el módulo positivo por potencia de dos, conservando clamp primero
y la rama `mask == 0`. Algoritmo candidato para `mask` de 1 a 15:

```text
period = 1u << mask
phase = uint(v) & ((period << 1) - 1u)
result = mirror && phase >= period ? (period << 1) - 1u - phase
                                    : phase & (period - 1u)
```

La conversión unsigned y la máscara conservan la fase de coordenadas negativas.
Existe una transformación equivalente ya probada en el sampler CPU; usarla como
referencia, no asumir que cualquier división con signo admite este reemplazo.

**Pruebas:** ejecutar el shader con valores negativos/positivos alrededor de cada
periodo, máscara 0–15, clamp/mirror, extremos representables relevantes y bandas
COPY de intro. Añadir una fixture que ejercite `coord` realmente en GPU, no solo
una copia C++ de la fórmula. Exigir resultado exacto antes/después.
**Aceptación:** desaparecen los avisos de módulo de `coord` y se demuestra menor
trabajo compilado o tiempo estable/mejor. Los avisos de divisiones del combiner
no entran en este cambio. **Reversión:** restaurar únicamente `coord`.

### P03 — Binning contiguo y estable (pequeña/media)

**Depende de:** P01. **Archivos:** `src/gpu_raster.cpp`, `Impl::flush`.

Sustituir los vectores por bin por `counts`, `offsets`, `cursors`, `refs` planos
reutilizables. Mantener bins 32×32 y comandos de 96 palabras inicialmente:

1. Contar referencias de cada bin recorriendo los bounding boxes actuales.
2. Calcular prefijos y comprobar overflow antes de reservar `refs`.
3. Recorrer comandos otra vez en orden original; escribir `i * COMMAND_WORDS`
   en `refs[cursor[bin]++]`.
4. Conservar el contrato `spans = {inicio, cantidad}` del shader. Mantener
   capacidad entre lotes; no ejecutar `shrink_to_fit` por cuadro.

Dos pasadas eliminan las asignaciones pequeñas y la concatenación posterior;
no reducen necesariamente el número de referencias. Medir si el segundo
recorrido compensa. No paralelizar todavía: un llenado no estable altera blending.
En 4K hay 6.120 bins de 32×32; es un caso útil para medir localidad/asignaciones.

**Pruebas:** comparar spans/refs contra el algoritmo anterior con cajas vacías,
bordes, dimensiones no múltiplos de 32, escenas dispersas y 8.192 comandos;
triángulos/líneas/rectángulos superpuestos, cambios de target y feedback.
**Aceptación:** listas idénticas por bin, misma salida, sin asignaciones repetidas
al estabilizar capacidad, menor tiempo de preparación o beneficio de memoria.
**Reversión:** conservar el algoritmo anterior solo como referencia de pruebas.

### P04 — Agrupar readbacks sin cambiar las barreras (pequeña/media)

**Depende de:** P01. **Archivo:** `src/gpu_raster.cpp`, `GPURaster::readback`.

Construir una lista pequeña y acotada de superficies de la generación actual,
sucias y solapadas con el rango solicitado. Después de `flush`, crear staging
faltante y encolar **todas** sus `CopyResource`; recién después hacer Map,
conversión y Unmap de cada una. Mantener exactamente el criterio de selección.

Esto permite que las copias ya estén encoladas al empezar a esperar; no elimina
la dependencia nativa. Microsoft explica que mapear staging inmediatamente
después de copiar puede detener CPU hasta ejecutar el trabajo pendiente.
[Fuente oficial](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/copying-and-accessing-resource-data).

**Pruebas:** color+depth, color==depth soportado, solapamiento parcial rechazado,
feedback intermedio seguido de más dibujo y varios targets. El flag `dirty`
solo se limpia tras copiar correctamente; todas las lecturas deben terminar
antes del commit. No sustituir Map por lectura de un frame anterior.
**Aceptación:** salida idéntica; comparar suma de esperas Map y p95 nativo.
Una sola superficie puede no mejorar. **Reversión:** bucle original en una entrega.

### P05a — Contrato de superficies y asignaciones HD (media)

**Depende de:** P01. **Archivos:** `include/render_surface.hpp`,
`src/gpu_raster.cpp`, `AsyncHDRenderer` en `src/main.cpp`.

Antes de mover trabajo a GPU, separar la descripción de un target de su buffer
CPU: dirección, dimensiones nativas/HD, escalas, generación y validez de contenido.
Una consulta de dimensiones no debe requerir ampliar todos los píxeles en CPU.
No copiar `RenderSurfaces` con sus vectores para transferir estado lógico.

Si se empieza por conservar capacidad CPU entre jobs, separar capacidad de
validez y exigir reimportación por tarea. `clear()` actual destruye los vectores;
quitarlo sin más dejaría contenido y asociaciones de RDRAM de otro snapshot.
No ejecutar `sync` hacia RAM antigua al reciclar almacenamiento. Un pool debe
pertenecer al trabajador y tener límite de bytes, no depender del Job reutilizado.

**Pruebas:** dos snapshots con misma dirección y colores distintos, cambio del
puntero RAM, 320↔640, Half/Full, F6, generación obsoleta y reutilización después
de fallo. **Aceptación:** mismas importaciones/salida con menor asignación o una
API de metadatos lista para P05b. Si no hay ganancia, evitar un pool intermedio
que P05b eliminaría; conservar solo la separación necesaria.

### P05b — Subir nativo y expandir en GPU (media/alta, mayor potencial HD)

**Depende de:** P05a y P04. **Archivos:** `src/gpu_raster.cpp`,
shader nuevo de importación, `tools/compile_gpu_shader.cpp`, `CMakeLists.txt`,
`include/render_surface.hpp` y checks GPU.

1. En el primer uso de una superficie por tarea, leer de la RAM privada los
   píxeles nativos vigentes. Subirlos inicialmente como uint32 si eso simplifica
   el contrato existente; uint16 empaquetado es otro experimento posterior.
2. Expandir mediante compute al buffer GPU final usando exactamente la selección
   actual `min(uint32_t(x/scale), native_width-1)` y su equivalente vertical.
   Para evitar diferencias CPU/HLSL en límites fraccionarios, subir tablas x/y
   calculadas como la referencia CPU; evaluar división HLSL solo después de
   demostrar igualdad para todas las dimensiones soportadas.
3. Materializar buffer HD CPU solamente cuando un readback/captura lo necesite.
   La ruta software continúa utilizando `RenderSurfaces` normalmente.
4. Mantener reimportación de contenido por tarea, caché GPU por capacidad y
   sincronización de framebuffer antes de LOADTLUT/LOADBLOCK/LOADTILE.
   La importación posterior a feedback debe leer la RAM privada ya actualizada.
5. Mantener primero el readback HD completo existente y su downsample CPU.
   Descargar solo el tamaño nativo mediante downsample GPU es otra tarea:
   necesita separar consumidores nativos de capturas/scanout HD.

**Justificación:** elimina expansión HD CPU y uploads proporcionales a píxeles
HD; paga un dispatch de expansión. Puede favorecer 4K y no compensar en nativo.
Empezar con HD; conservar la ruta nativa actual hasta medir.

**Pruebas:** patrones por fila/columna y extremos en 320×240, 640×120/240/480,
resoluciones 720/1080/2160, offsets VI, color/depth, CPU writes entre tareas,
alternancia de targets, feedback y fallo GPU. Exigir igualdad del buffer expandido
respecto de CPU, no solo de su imagen presentada. Revisar debug layer D3D11 si
está disponible; su ausencia debe quedar registrada.
**Aceptación:** reducción medida de bytes/asignaciones y mejora de p95 HD con
salida idéntica. **Reversión:** selector interno temporal entre imports CPU/GPU;
retirarlo al consolidar salvo que quede una necesidad real de compatibilidad.

### P06 — Reutilizar snapshots de pantalla y evitar duplicaciones (media)

**Depende de:** P01. **Archivos:** `AsyncHDRenderer::queue_screen`, `run`,
`PresentedFrame`, `Win32RendererContext::update_screen` en `src/main.cpp`.

Crear pool acotado de Screen/buffer por ownership: productor, pendiente y
consumidor nunca mutan el mismo almacenamiento. Tomar/devolver slots bajo mutex;
copiar fuera del mutex, publicar con generación y destruir fuera del bloqueo.
La cola mantiene solo el último pendiente. Medir primero este cambio aislado.

Después, estudiar si `PresentedFrame::update` debe ejecutarse cada VI cuando
DXGI ya presenta: se utiliza para diagnóstico y capturas. Compartir una copia
inmutable o capturar a petición con vigencia explícita; no entregar un puntero
a RDRAM viva al trabajador ni quitar silenciosamente las capturas.
La copia nativa de Screen sigue siendo necesaria para transiciones/fallback.
Tampoco omitirla solo porque VI_ORIGIN no cambió: el contenido puede cambiar.

**Pruebas:** saturar productor, transiciones negro/menú/carrera, filtro, resize,
minimizar/restaurar, F6 rápido, fallos de presentación y shutdown con HWND vivo.
**Aceptación:** menos asignaciones/copias, igual fallback, sin aumento de edad
p95. No introducir cola ilimitada ni política que bloquee el runtime esperando HD.

### P07 — Reducir trabajo por píxel del shader (experimentos separados)

**Depende de:** P01–P03; medir cada variante sin acumular cambios.
**Archivos:** `Impl::state`, `triangle`, `line`, shader raster y checks.

**P07a:** predecodificar invariantes por comando: validez de tiles, uso de segunda
textura, flags y shifts. Reservar palabras documentadas; comprobar contrato C++/
HLSL con constantes y asserts. Si se separa una tabla de estados, usar índices y
versiones de estado/TMEM: misma dirección de textura no significa mismo contenido.
Mantener primitivas en orden; no crear docenas de variantes de shader inicialmente.

**P07b:** evitar derivadas/footprint cuando LOD está desactivado. La CPU ya lo
hace. Verificar bytecode: el compilador puede haber eliminado ese trabajo al
propagar la rama de `lodTiles`; si ya lo elimina, no atribuirle una nueva mejora.

**P07c:** probar rechazo por profundidad antes del muestreo de texturas, tras
calcular cobertura y Z con las operaciones actuales. Conservar comparación,
primitive depth, alias color/depth y actualizar profundidad solo después de
alpha/blend aceptados. La ruta de líneas/rectángulos debe tratarse por separado.
Es útil si hay overdraw; introducir más ramas podría perjudicar escenas simples.

**P07d, posterior:** precalcular área, derivadas de perspectiva y longitud de
líneas por primitiva. Cambia lugar/orden de aritmética CPU/GPU: exigir evaluación
numérica específica en bordes, degenerate triangles, LOD y cobertura. No activar
aproximaciones de recíproco/sqrt ni dividir enteros mediante floats sin prueba.

**Aceptación:** mismo resultado para a–c; d necesita demostrar equivalencia o
declarar/revisar por separado diferencias antes de habilitar. Medir tiempo GPU,
registros y tamaño del comando: menos ALU puede aumentar presión de memoria.

### P08 — Uploads y dispatches condicionados al perfil (media/alta)

**Depende de:** P01/P03/P05. **Archivo:** `src/gpu_raster.cpp` y shader si aplica.

`UpdateSubresource` puede necesitar copias adicionales bajo contención; esto
justifica medir el reciclado de los cuatro inputs, no reemplazarlos ciegamente.
[Documentación oficial](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource).

Si upload/envío domina, ensayar un anillo pequeño de conjuntos de buffers DEFAULT
y queries de finalización. Reutilizar un slot solo tras terminar su GPU; tener
política acotada cuando todos estén ocupados y medir la espera. Un índice módulo
3 no demuestra finalización. Evitar Flush/espera activa por cada lote. No usar
MAP_WRITE_DISCARD/NO_OVERWRITE sobre structured buffers sin verificar primero
las restricciones reales de usage, flags y versión D3D11.

Si predominan bins vacíos, probar dispatch solo de grupos activos, conservando
un escritor por píxel y orden de comandos. Primero comparar tamaños de bin
16/32/64 sin cambiar simultáneamente numthreads 8×8: bin y thread group son
conceptos distintos. Una lista compacta de grupos requiere medir su construcción
y demostrar cobertura exacta en bordes. No usar atomics para mezclar primitivas.

**Aceptación:** mejora en GPU modesta y ausencia de regresión importante en la
GPU disponible; memoria máxima documentada. Si no hay otra GPU, dejar esa
validación pendiente y no generalizar resultados de la RTX 3080 Ti.

### P09 — Snapshots de RDRAM: investigar antes de compartir (alta)

**Depende de:** P01 y evidencia de que las copias completas importan.
**Archivos:** `process_graphics_task`, `AsyncHDRenderer::submit`.

El HD y el nativo modifican su RAM privada por feedback; por eso un
`shared_ptr<vector<uint8_t>>` mutable común no es una solución válida. Un snapshot
base inmutable con overlays de escritura necesitaría cambiar los accesos del
intérprete y probar todos los consumidores. Detectar páginas sucias requiere
abarcar escrituras del juego recompilado, runtime, DMA y audio; comparar/hash de
los 8 MiB sigue leyendo esos 8 MiB.

Primera entrega: medir copia nativa y HD, asignaciones, porcentaje de jobs HD
reemplazados y vida del pool. Optimizar reutilización de capacidad y destrucción
fuera de mutex donde falte. Si eso no aporta, terminar aquí.
Compartir snapshot inmutable solo se autoriza como diseño posterior con inventario
de lecturas/escrituras y pruebas de fallo; no dar esta refactorización como tarea
automática a un modelo modesto. No ampliar la cola para ocultar el coste.

### P10 — CPU de referencia, solo donde el perfil lo justifique (media)

**Depende de:** P01. **Archivos:** `src/f3dex2.cpp`, `include/f3dex2.hpp`, checks.

Primero separar logs/telemetría del estado funcional: `diagnostics_enabled=false`
no apaga todos los contadores por píxel/opcode. Probar acumulación local o modo
de contadores optativo conservando diagnósticos cuando se solicitan; medir el
costo de la rama nueva. Nunca omitir instrucciones ni actualizaciones funcionales.

Después evaluar datos de tile/combiner predecodificados por cambio de estado,
no por texel. Invalidar por TMEM/TLUT, tiles, other modes y combine. El sampler
ya tiene mejoras exactas; no volver a proponer log2/ldexp como si siguiera intacto.

SIMD por bloques o ecuaciones de arista incrementales quedan para un experimento
posterior: cambiar acumulación float puede mover cobertura y Z. Preservar una
referencia escalar, validar RDRAM completa y estado, y evitar AVX obligatorio
en binarios portables. No paralelizar primitivas sobre el mismo framebuffer.

### P11 — Audio, entrada y presentación secundaria (condicional)

**Depende de:** P01 y perfil del host; prioridad menor que renderer.

- **Audio:** sustituir compactación `pending_samples.erase` por ring buffer solo
  si aparecen picos. Mantener mutex inicialmente, contador AI, orden estéreo,
  cambios de frecuencia y política de desbordes. No bloquear el callback del
  scheduler ni reducir buffers por intuición. Probar wrap, entrada fragmentada,
  cierre/fallo del dispositivo y contabilidad exacta de muestras.
- **Entrada:** el trabajo SDL ya está en Win32; el runtime consume snapshot corto.
  El bucle principal duerme 8 ms. Si se busca menor latencia, medir edad de input
  y tiempo de poll; estudiar espera por mensajes con deadline de sondeo, manteniendo
  SDL en su hilo. Quitar sleep o usar busy loop puede elevar consumo sin ayudar
  a la cadencia lógica. No declarar ese cambio una mejora de FPS del renderer.
- **Presentación:** ya hay flip y deduplicación, pero compute genera `output`
  y luego CopyResource al backbuffer. Si esa copia destaca, estudiar presentar
  mediante fullscreen triangle/RTV o recurso compatible directo. Conservar
  capturas, filtros, resize y formatos; no asumir que cualquier backbuffer acepta
  UAV ni mezclarlo con P05. Medir a tamaño real de ventana, no solo a 4K interno.

### P12 — Compilación, IPO y PGO (experimento final independiente)

**Depende de:** línea base y pruebas; no necesita todas las optimizaciones previas.
**Archivos:** `CMakeLists.txt`, herramientas/documentación de build.

Añadir opción OFF por defecto para IPO usando `CheckIPOSupported` y propiedades
de targets. Separar código propio, C generado y dependencias cuando sus opciones
difieran; no inyectar flags globales en SDL/runtime. Medir enlace, tamaño, memoria
y ejecución; el código generado puede volver caro el LTO.

PGO requiere dos builds compatibles y entrenamiento representativo de intro,
menús, carrera, efectos y modos. Registrar compilador y cobertura; no usar solo
una captura del primer circuito ni ignorar perfiles obsoletos.
Mantener el modelo de redondeo y las convenciones de `recomp.h`.
GCC documenta las implicaciones de fast-math, rounding-math, LTO y optimización
guiada por perfiles; aplicar opciones de forma verificable y por target.
[Manual oficial](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).

No usar `-Ofast`, `-ffast-math`, `-march=native` globales ni retirar
`D3DCOMPILE_IEEE_STRICTNESS` para obtener cifras. Tampoco desactivar comprobaciones
de rangos, límites de DL o guardas de commit. **Aceptación:** pruebas y carrera
representativas, ventaja fuera del ruido y compatibilidad del paquete portable.

## 6. Convenciones de código para ejecutar el plan

Estas reglas favorecen decisiones eficientes y revisables; una convención de
estilo por sí sola no hace más rápido el programa.

- Mantener C++20 y estilo local, con formato legible en código nuevo. Una entrega
  cambia un mecanismo; evitar reformatear archivos completos junto con algoritmos.
- Activar advertencias apropiadas por target propio, como `-Wall -Wextra` en
  GCC, en una entrega separada. Revisar conversiones y sombreado en el código
  nuevo; no imponer `-Werror` global a dependencias o al C generado.
- Usar `uint32_t` para palabras del protocolo/GPU, `size_t` para tamaños CPU,
  `uint64_t` para productos/rangos antes de convertir a UINT. Validar overflow
  en capacidades, prefijos, bytes y offsets. Dar nombres a bits y offsets HLSL.
- Documentar invariantes de orden, ownership, contenido y generación junto al
  código. `static_assert` para tamaño/layout del protocolo C++/HLSL; serialización
  explícita para capturas. No serializar clases, punteros o padding.
- RAII para COM/Map/handles y bloqueos; Unmap garantizado tras Map exitoso.
  Errores en frontera de tarea conservan fallback, sin catch silencioso.
- Vectores contiguos con capacidad reutilizable donde haya datos masivos;
  `std::array` para los ocho targets o tamaños fijos. No cambiar la búsqueda
  lineal de ocho superficies por unordered_map sin medir: no es el problema obvio.
- Usar `span`/referencias para vistas de vida acotada; `unique_ptr` o transferencia
  de slots para ownership entre hilos. No reemplazar copias por referencias cuyo
  productor puede modificar/destruir datos antes del consumo.
- Reducir ámbito del mutex, pero conservar la publicación de datos sincronizada.
  `memory_order_relaxed` solo para contadores que no publican contenido y tras
  documentarlo. No convertir señales de validez/cierre a relaxed por rutina.
- Separar estado del intérprete, métricas y buffers de trabajo; helper pequeño
  cuando una tarea lo necesite. Evitar una reescritura masiva de `main.cpp`.
- `[[likely]]`, force-inline, restrict, alineación especial, SIMD y pools propios
  necesitan perfil/evidencia. Nunca declarar no-alias cuando color/depth pueden
  coincidir ni tratar la GPU potente disponible como hardware mínimo.
- No editar C generado ni checkouts de dependencias como solución permanente.
  Los cambios necesarios van a generador/configuración o parches reproducibles.
- Toda nueva ruta publicada se añade explícitamente a `tools/check_public_tree.py`.
  No abrir comodines para evitar el manifiesto. Datos del juego, capturas,
  perfiles, guardados, binarios y credenciales permanecen fuera de Git.

## 7. Comandos de verificación

Desde la raíz, con dependencias fijadas instaladas. En esta revisión ya existía
`build-checks` configurado Release; para crearlo desde cero:

```powershell
cmake -S . -B build-checks -G Ninja -DCMAKE_BUILD_TYPE=Release -DTGOD_BUILD_GAME=OFF
cmake --build build-checks --target tgod_regression_checks tgod_gpu_checks tgod_cpu_benchmark tgod_native_gpu_benchmark --parallel 4
.\build-checks\tgod_regression_checks.exe
.\build-checks\tgod_gpu_checks.exe
```

Comprobar `$LASTEXITCODE` después de cada proceso. GPU devuelve 77 sin hardware
compatible: registrar OMITIDO, no PASS. No usar `ctest` como sustituto: el CMake
actual crea ejecutables de checks, pero no registra pruebas con `add_test`.

Captura y replay actuales, únicamente en la copia aislada preparada del juego:

```powershell
.\tgod_native.exe --rom 'ruta-local\copia-propia.z64' --headless --performance-check --seconds 24 --render-height 2160 --native-rasterizer gpu
```

La ventana de 24 s sirve para generar entradas; no reemplaza el protocolo largo
de medición. Desde el checkout, usando entradas compatibles (la versión 1
depende del ABI; la versión 2 usa campos explícitos):

```powershell
.\build-checks\tgod_cpu_benchmark.exe 'ruta-local\cpu_task_60.bin' 'build\optimization\cpu.rdram'
.\build-checks\tgod_native_gpu_benchmark.exe 'ruta-local\cpu_task_60.bin' 'build\optimization\gpu.rdram'
python tools/check_public_tree.py --worktree
git diff --check
```

Crear antes `build/optimization`. Comparar el mismo backend antes/después byte
por byte, además de checksums. Un hash distinto solo detecta diferencias: el
comparador debe informar región, píxeles/canales, Z, máximo error y escrituras
fuera de targets. Preservar baseline de capturas y ejecutables en carpetas
ignoradas; recapturar o migrar explícitamente si cambia el formato.

Para P02–P08: ambas suites y replays pertinentes. Para P06/P09/P11: además
integración real, generaciones, fallos, cierre, audio y controles. Para P12:
build del juego y verificación portable con herramientas existentes. Una prueba
manual o hardware no disponible queda pendiente explícita; nunca inferir PASS.

## 8. Instrucción para el modelo implementador

```text
Lee AGENTS.md y docs/OPTIMIZATION-PLAN-2026-11-13.md.
Implementa solamente la tarea Pxx indicada, incluyendo sus prerrequisitos.
Si falta la medición que condiciona una tarea, realiza primero esa medición.
Comprueba git status y no sobrescribas cambios ajenos.
Localiza los símbolos actuales: los números de línea pertenecen al commit base.
Preserva los seis contratos de la sección 2.
Haz una entrega por cambio medible; no combines refactor, calidad gráfica y flags.
Antes de cambiar layout del intérprete, resuelve la compatibilidad de capturas.
Ejecuta las verificaciones aplicables y compara contra una baseline conservada.
Reporta archivos, razón, pruebas realmente ejecutadas, métricas A/B y pendientes.
Si no mejora fuera del ruido, conserva la implementación anterior y explica por qué.
Detente ante diferencias funcionales sin explicar; no aumentes tolerancias.
No avances automáticamente a tareas de alto riesgo ni publiques por este plan.
```

Ficha que debe dejar cada tarea:

| Campo | Contenido obligatorio |
| --- | --- |
| Identidad | Pxx, commit base/candidato, estado limpio/sucio y flags |
| Hipótesis | Coste concreto y evidencia que motivó el cambio |
| Alcance | Archivos/símbolos y comportamiento conservado |
| Datos | Capturas compatibles/configuración, hardware y repeticiones |
| Resultado | p50/p95/p99, bytes/asignaciones, memoria y métricas globales pertinentes |
| Corrección | Suites, comparación de salida/estado y pruebas manuales realizadas |
| Decisión | Aceptar, descartar o pendiente; motivo y cómo revertir |

No se autoriza push ni release por crear este plan. Si luego se solicita push,
aplica AGENTS.md: publicar también el prerelease correspondiente, con ZIP/DLLs,
checksum y licencias, basado en el mismo commit limpio y dejando verificaciones
pendientes explícitas.

## 9. Referencias locales y límites

- `docs/DETAIL-AND-DRAW-DISTANCE.md`: evidencia histórica de CPU optimizada,
  nativo GPU y pruebas; aproximadamente 31–35 ms CPU frente a 1,96–2,53 ms GPU
  nativa en tres replays allí documentados. Son antecedentes, no baseline nueva.
- `docs/PHYSICS-INVESTIGATION-2026-10-22.md`: razón del orden SP/DP y límites de
  la investigación del tambaleo. No declarar ese síntoma resuelto por optimizar.
- `docs/BUILDING.md`: configuración, diagnósticos, benchmarks y requisitos.
- `docs/RENDERER-ARCHITECTURE.md`: alternativas históricas. Migrar a RT64,
  Vulkan/D3D12 o reemplazar el host no es el primer paso de este plan.

La revisión no es una auditoría exhaustiva de todo el C recompilado ni una
medición por función de la simulación. Si P01 muestra que el renderer dejó de
dominar, perfilar el runtime/juego con símbolos antes de modificarlo. El resultado
útil puede ser menor consumo y mejor p95 con las mismas ~30 tareas/s originales.
