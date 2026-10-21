# Estado del port — 2026-10-21

El usuario confirma que el HUD quedó bien y el humo quedó arreglado en los casos
probados con preview.4. También mantiene la mejora musical. Se cierra ese síntoma;
esto no equivale a paridad completa con consola.

Última revisión, preview.6: resolución interna nativa/720p/1080p
con F6 y `tgod.ini`. HD vuelve a rasterizar la geometría en copias privadas, con
profundidad y HUD. Un hilo separado evita bloquear el avance nativo del juego;
la salida HD sigue siendo experimental, lenta y con latencia por CPU.
La comparación de un mismo cuadro dio 318 ms a 720p y 714 ms a 1080p.

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

- Rendimiento HD: el rasterizador por CPU no ofrece 720p/1080p fluidos. Nativo
  sigue siendo el valor predeterminado. Evaluar aceleración o backend de GPU.
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
