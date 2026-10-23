# Estado del port — 2026-10-23

Revisión preparada para preview.12: rasterizado nativo acelerado por GPU,
con copia privada, readbacks completos y commit de targets antes de SP/DP.
`NativeRasterizer=gpu|cpu`; GPU por defecto en el perfil mejorado, CPU en legacy.
En la comparación 4K/Full nueva: CPU de referencia ~33.4 ms/tarea frente a
~3.3 ms en la ruta nativa GPU; GFX ~22.7/s frente a ~28.2/s. Nativo con GPU:
~29.9 tareas/s. Full y Off conservan presupuesto original de 2 VIs en carrera;
no se encontró un limitador exclusivo de Full. No se fuerza 60 FPS ni se quita
el paso nativo de buffers. Hay diferencias pequeñas de cuantización CPU/GPU,
documentadas en DETAIL-AND-DRAW-DISTANCE.md. Compilado y comprobado localmente;
no está en el ZIP preview.11 publicado.

Optimización CPU de texturas aplicada y comprobada: tres tareas reales bajaron
de 69.53/68.28/61.97 ms a 34.54/34.03/30.80 ms. Los 8 MiB de salida coinciden
byte por byte con la versión anterior. Carrera 4K/Full con las mismas mejoras:
CPU ~33 ms y ~22 tareas/s (antes ~67 ms y ~12.6 tareas/s). Nativo: ~22.8 tareas/s.
Pasaron regresiones CPU/GPU y presentación DXGI; las dos carreras de 24 s
salieron con código 0. Revisión preparada para v0.1.0-preview.11. No confirma aún
cuadros nuevos realmente mostrados por segundo ni resolución del síntoma
subjetivo específico de F6; tampoco establece el ritmo original de la consola.

Fluidez 4K frente a F6 nativo, diagnóstico previo: el contador anterior mostraba VI/s como FPS.
El título ahora separa VI, tareas gráficas/s y tiempos de rasterizado CPU/GPU;
la consola añade edad del snapshot. Son métricas diferentes de FPS de física y
de latencia completa. Comparaciones automáticas locales: nativo y 4K/Full dieron
12.68 y 12.60 tareas/s, respectivamente; CPU nativa ~67 ms, GPU 4K ~7 ms.
No reproducen una caída de ritmo exclusiva de 4K; sí muestran coste nativo alto
y preparación HD adicional. Capturas de carrera revisadas. Se optimizó la
importación de buffers HD conservando el muestreo: replay medio 19.4 → 13.3 ms,
sin mejora del ritmo GFX ni de edad visible demostrada en ese primer cambio.
La optimización posterior de texturas descrita arriba sí mejoró GFX. Evidencia y límites en
DETAIL-AND-DRAW-DISTANCE.md. No se modificó VSync ni temporización para este síntoma.

Cierre al seleccionar ROM con Full: reproducido con GDB y corregido. Había una
dirección KSEG sin extensión de signo en el callback de arranque; se corrigieron
también dos lecturas similares del módulo de mejoras. El EXE local arranca con
el perfil mejorado 4K/Full. Full/1080p llegó a Mode 2 en diagnóstico; quedan
límites de comprobación descritos en DETAIL-AND-DRAW-DISTANCE.md. No publicado.

Cambios locales experimentales de detalle/distancia: opciones `ForceHighDetail`,
`DrawDistancePercent` y `ExtraTrackSections` en tgod.ini. Hooks de selección del
primer modelo por distancia, parámetros de cámara y consulta extra de decorado.
LegacyVersion=true fuerza los valores originales. LegacyVersion=false activa
el perfil mejorado, personalizable con las otras claves. Compilado; no hay
confirmación de desaparición del popping; primeras mediciones de rendimiento arriba. Alcance
y direcciones en [DETAIL-AND-DRAW-DISTANCE.md](DETAIL-AND-DRAW-DISTANCE.md).

Expansion Pak: se conservan los 8 MiB y se incorporó una preferencia reversible
de arranque `OriginalResolution=game|off|half|full`, separada de F6, con `game`
por defecto en preview.10. El perfil mejorado local usa Full si no hay otra
preferencia guardada; legacy deja la selección al juego. Hook explícito tras la
inicialización, sin escrituras continuas.
El renderer recibe las dimensiones originales y usa escalas X/Y separadas en
CPU, GPU, LOD, readbacks y scanout; Half conserva el área visible reducida.
Se recrean superficies al cambiar su altura y se distinguen modos en los frames
HD para evitar reutilizar la imagen de otra resolución. Compilación correcta;
pendiente de comparación visual, cambios de modo y multijugador. Evidencia y
límites en [EXPANSION-PAK-RESEARCH.md](EXPANSION-PAK-RESEARCH.md).
Preview.10: el usuario confirmó el buen funcionamiento general y autorizó la
publicación. Esto no equivale a completar la comparación de todos los modos.

Cambios de preview.9: SDL3 3.4.18 incorporado para gamepads y
joysticks genéricos, con captura manual de botones/ejes/crucetas sin mapping.
Se incluyen DLL, licencia y procedencia fijada. Migración de controles v1 a v2.
Compilado; pendiente de validación con dispositivos físicos.

Tambaleo del primer circuito: se detectó un aviso SP anterior al consumo de las
entradas gráficas; se pospuso hasta terminar el procesamiento nativo, manteniendo
HD asíncrono. Es una corrección candidata: el síntoma sigue sin confirmarse como
resuelto y no se modificaron redondeos/física. Ver PHYSICS-INVESTIGATION-2026-10-22.md.


El usuario confirma que el HUD quedó bien y el humo quedó arreglado en los casos
probados con preview.4. También mantiene la mejora musical. Se cierra ese síntoma;
esto no equivale a paridad completa con consola.

Cambios de preview.7: salida HD por GPU Direct3D 11, con F6
y `tgod.ini`. Mantiene el intérprete y los eventos SP/DP en nativo; la GPU dibuja
solo sobre memoria privada. En una RTX 3080 Ti, una misma lista de carrera pasa
de 326/726 ms por CPU a unos 12 ms por GPU a 720p/1080p. Se elimina el límite de
captura de 100 ms. Si no hay hardware compatible se usa el fallback HD por CPU,
que conserva las limitaciones de rendimiento anteriores. Las diferencias de
redondeo y otras escenas siguen en revisión. Estos cambios forman parte de preview.7.

La presentación de preview.7 reutiliza superficies/buffers y presenta HD por DXGI
sin descarga final a CPU. Conserva el rasterizado y eventos nativos, los readbacks
para efectos y una recuperación a GDI. Pasaron regresiones de presentación, una
carrera con cambios nativo/720p/1080p, resize, fullscreen y filtro, sin descartes
de audio. Las cifras CPU y GPU se miden por separado; no son FPS garantizados.

La comparación de Mario Kart, Zelda, Wave Race y Ship of Harkinian está en
[RENDERER-ARCHITECTURE.md](RENDERER-ARCHITECTURE.md). Se implementaron la reutilización
de recursos y la presentación directa en el backend propio. Un futuro prototipo RT64 debe resolver primero
las líneas L3DEX2 de la revisión elegida y comprobar sincronización/readback.

También están corregidas las máscaras de textura
que dejaban los logos de la intro negros y solo permitían ver puntos de nieve.
Vuelven a verse KEMCO, Snowblind y la portada. La máscara limita coordenadas;
no determina el tamaño de la imagen. Se conserva el soporte de L3DEX2 de preview.5.
Continúa la revisión de fidelidad RDP/VI y escenas fuera del recorrido comprobado.
Evidencia y límites en
[DIAGNOSTICS-2026-10-21.md](DIAGNOSTICS-2026-10-21.md).

## Distribución Windows preliminar

Fuentes en `main`; binarios separados en GitHub Releases y localmente en
`../release-packages/`. El paquete x64 contiene el EXE, tres DLL MinGW, launcher,
instrucciones, GPLv3/avisos upstream, fuentes por revisión y manifiesto SHA-256.
No contiene ROM ni datos extraídos. El selector carga una ROM local y verifica
USA v0; `.z64`, `.v64` y `.n64` se normalizan solo en memoria.
El paquete preview.4 incorpora esta revisión de HUD y partículas; preview.3
contiene la recuperación inicial del showroom y los materiales del coche.
Preview.5 añade el manejo de líneas L3DEX2 y los cambios de microcódigo.
Preview.6 repara la intro y añade salida HD asíncrona con F6 y configuración.
`tools/release_windows.ps1` permite repetir compilación, pruebas y empaquetado,
y publicar opcionalmente con `-Publish`. No hay publicación automática por push.

La extracción temporal del ZIP pasó con PATH limitado a Windows: inicio sin
compilador instalado en PATH, rutas Unicode/con espacios, tres órdenes de bytes,
rechazo de ROM truncada/modificada, 16 s de carrera, EEPROM y audio sin descartes.
Las capturas, ROMs de prueba y guardados quedaron en la extracción privada y no
se incorporaron al ZIP. Los glitches de fidelidad siguientes siguen abiertos.

Port experimental para Windows x64. El ejecutable arranca, recorre los menús y
entra a carrera con una ROM USA v0 local. No se declara paridad completa con N64.

## Correcciones verificadas

- Audio: FULL usa el bit 31 de AI con extensión de signo a los registros de
  64 bits. Los eventos AI se entregan al retirar buffers de WaveOut.
- Scheduler: si todos los hilos esperan hardware, entrega eventos externos hasta
  despertar un hilo. Se corrigió la eliminación de nodos de las listas de hilos
  y la reinserción tras cambiar prioridades.
- Render: `G_MOVEMEM` lee el identificador en el byte bajo, evitando interpretar
  una carga de luces como viewport. Se corrigieron PUSH de F3DEX2, el avance DXT
  después de cada palabra y la lectura de las filas impares de TMEM.
- Materiales: TEXEL1 separado, niveles LOD limitados por G_TEXTURE y alfa de
  intensidad. Rectángulos 2D comparten el muestreador y respetan filas impares.
- Presentación: modo VI entrelazado del showroom, tamaño RGB dinámico,
  sincronización del repintado y capturas de cuadros presentados.
- HUD/humo: controles Z_CMP/Z_UPD, profundidad primitiva, combinador compartido,
  blender y filtro de tres muestras. Confirmación visual del usuario.
- Líneas: cambio de microcódigo F3DEX2/L3DEX2 2.05, LINE3D y bordes de TRI1/TRI2
  bajo L3DEX2. Cada nueva tarea vuelve a seleccionar su microcódigo.
- Intro: máscaras de coordenadas separadas de dimensiones/stride del tile,
  incluidas máscaras 15 y bandas COPY de 320x6. Clamp se aplica antes de wrap/mirror.
- Resolución: carrera a 320x240 nativo, 960x720 o 1440x1080 (4:3), con buffers host
  fuera de RDRAM y comparación de la misma lista de dibujo en los tres modos.
- Generación: las esperas cooperativas se aplican desde un script versionado.
  EEPROM read/write se identifican mediante símbolos; no requieren editar el C
  generado ni cambiar la función de lectura de mandos del runtime.

## Evidencia local

Pasaron las regresiones de 720p/1080p: geometría subpíxel que no existe en el
cuadro nativo, profundidad, HUD, líneas, offsets VI, CPU/readback y límite del
cache. Una prueba de 30 s con frenado/derrape y replay de un mismo cuadro
mantuvo carrera, 4/4 catálogos, EEPROM, cero descartes/reemplazos de audio y
ningún comando/microcódigo desconocido; 3 transiciones a dispositivo vacío.
Otra ejecución presentó cuadros de intro de 1440x1080 durante 8 s.
Tras el reporte de físicas alteradas se separó la salida HD del evento DP:
recorridos de 30 s a 720p/1080p mantuvieron carrera y 4/4 catálogos/EEPROM, sin
descartes/reemplazos de audio. El renderer nativo procesó 783/765 listas mientras
el hilo HD completó 128/64 cuadros; no se espera a cada cuadro HD para avanzar.

La intro sin saltar se capturó durante 38 s: KEMCO, animación de Snowblind,
portada y menú visibles. La batería sin ROM incluye bandas sintéticas 320x6,
máscaras 15, clamp+mask, coordenadas sin máscara y tiles de triángulos.
Las capturas siguen siendo privadas y no se incluyen en el código ni el ZIP.

La revisión L3DEX2 pasó la batería sin ROM y un recorrido de gráficos de 60 s:
786 cambios de microcódigo, 5278 segmentos aceptados, 4554 píxeles de línea,
cero comandos desconocidos y cero cargas de microcódigo sin reconocer. La carrera
siguió activa, 4/4 catálogos y EEPROM verificados, sin descartes/reemplazos de
audio; 6 transiciones observadas a dispositivo vacío. Es un recorrido parcial.

Después de regenerar desde la ROM y compilar: prueba automatizada de 16 segundos,
Modo 2 activo hasta el final, 4/4 catálogos verificados, EEPROM de 2048 bytes.
Audio: 548 bloques encolados, 546 completados y 2 en vuelo; 468 bloques con señal
enviados al dispositivo. Cero overruns, descartes silenciosos o frames reemplazados.
Las capturas locales muestran carretera, montañas, cielo y vehículo; los assets,
capturas y el PCM de diagnóstico no se incluyen en el repositorio.

La copia pública también se regeneró y compiló desde sus fuentes. Una ejecución
de 16 s de ese binario terminó con código 0, carrera activa, 4/4 catálogos y
EEPROM verificada: 549 bloques de audio, 547 completados, 2 en vuelo, 470 con
señal y cero descartes/reemplazos.

`tgod_regression_checks` verifica sin ROM texturas/filtrado/LOD, HUD/combinador,
blender/alfa, controles Z, matrices/viewport, luces/reflejos, VI y colas de hilos.
Incluye selección y cambios de microcódigo, anchos y colores de líneas, recorte
cerca de cámara, scissor, profundidad, índices inválidos y vuelta a F3DEX2.

## Pendientes

- Rendimiento HD: comprobar GPU en otros adaptadores y presentación en ventana;
  medir latencia prolongada y los intervalos largos iniciales observados. El
  fallback por CPU sigue siendo lento; el dibujo nativo aún ocupa CPU.
- Comparación de carteles/LOD/filtrado a distancia con referencia: la resolución
  interna mejora contornos pero no crea detalle en los assets originales.
- Revisar transparencias, reflejos y geometría en otras escenas y circuitos.
- Antialias/coverage y extremos de líneas con fidelidad al RDP; el trazado actual
  evalúa el centro del píxel y aproxima el ancho en pantalla.
- Combiner/blender, Z/coverage, subpíxeles, dither y campos VI con precisión N64.
- Escucha/comparación prolongada de música/efectos; prioridad actual: gráficos.
- Cobertura de comandos RSP y formatos fuera de las escenas comprobadas.
- Circuitos, modalidades, guardados y estabilidad fuera de la ruta automatizada.

Los contadores de cobertura de pantalla y de catálogos son diagnósticos parciales;
no demuestran por sí solos exactitud gráfica ni integridad de todos los assets.

Opción 4K de preview.7: F6 alterna nativo/720p/1080p/4K; mantiene 4:3 (2880×2160).
Pasaron regresiones de CPU y GPU, presentación residente y feedback explícito
a 2160p, además de una carrera de 20 s con capturas reales 2880×2160 y escala
autoritativa 1.0. El usuario confirmó la mejora de la presentación DXGI previa.


Cambios de preview.8: soporte de cuatro puertos XInput en el
sondeo, callbacks y hook SI; teclado compartido con P1. F11/Alt+Enter suprimen
repetición. Pasaron pruebas simuladas de cuatro mandos y desconexión/reconexión,
F11 sobre ventana real y prueba completa del ZIP. Pendiente validar mandos físicos
y una carrera multijugador completa.


Mapper F7 de preview.8: perfiles por jugador, asignación XInput,
tres bindings por acción, teclado, ejes/gatillos, zona muerta, inversión,
guardado controls.json con respaldo y bloqueo de entrada durante la edición.
El usuario confirmó su funcionamiento el 2026-10-22. **SDL3 para mandos genéricos
sin XInput sigue pendiente**, junto con identificación por modelo/serie.
