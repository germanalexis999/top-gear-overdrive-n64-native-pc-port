# Investigación: tambaleo intermitente del primer circuito

Fecha: 2026-10-22. Estado: causa del síntoma sin confirmar; corrección preventiva
aplicada a una carrera de datos observada en el código. Compilado, no validado en
una carrera nueva durante esta entrega. No se afirma que el tambaleo esté resuelto.

## Observación del usuario

El auto del jugador y los de IA a veces tambalean temporalmente y luego se
recuperan. Puede ocurrir al comienzo o durante la carrera, sin un punto fijo.
El usuario lo notó más en 720p/1080p/4K; no confirmó si también ocurre en nativo.
El posible redondeo fue una hipótesis inicial del usuario, no un diagnóstico.

## Hallazgo concreto: fin de RSP prematuro

En la revisión fijada de N64ModernRuntime, gfx_thread_func realiza:

    sp_complete();
    renderer_context->send_dl(task);
    dp_complete();

El propio comentario upstream advierte que se presupone que el juego conserva
las entradas RSP hasta DP completo y que ese supuesto podría fallar en un juego.
Para nuestro intérprete CPU, send_dl todavía lee las listas, matrices y vértices
desde RDRAM; además, AsyncHDRenderer::submit copia 8 MiB para la reproducción HD.
Por tanto, notificar SP antes de consumir esos datos permite que un hilo emulado
reanudado reutilice entradas que el renderer aún está leyendo/copiendo.

Esto constituye una ventana de carrera en el contrato de lectura. Que sea la
causa del tambaleo aún requiere comprobar la escena. La corrupción temporal de
matrices/vértices podría parecer una falla física y afectar también a la IA,
aunque la simulación no hubiese cambiado; ambas posibilidades siguen abiertas.
Una mayor carga o una copia HD puede cambiar la probabilidad de la carrera.
No se ha medido esa correlación en una reproducción nueva.

## Corrección incorporada

En patches/n64modernruntime.patch se cambió el orden a:

    renderer_context->send_dl(task);  // consumir entradas nativas y capturar HD
    sp_complete();
    dp_complete();

Se mantiene SP antes de DP, pero ambos se emiten después de que el consumidor
nativo termina. El hilo HD permanece asíncrono: no se espera al rasterizado GPU,
a Present, ni a completar el cuadro HD para liberar SP/DP. No se cambia la ruta
de tareas de audio ni la frecuencia VI, el paso de simulación o la resolución
que consulta la lógica del juego (continúa nativa).

El tiempo de entrega SP gráfico sí cambia: ahora incluye el consumo nativo de
la lista. Eso es intencional para impedir reutilizar sus entradas demasiado pronto.
Debe comprobarse que el planificador y la preparación de audio conservan su
comportamiento durante una carrera prolongada. No confundir esto con una promesa
de conservar exactamente la temporización anterior de SP.

La modificación está en el parche reproducible y en el checkout local usado
para compilar; no se editó el C generado del juego. El parche completo se volvió
a generar ignorando el estado sucio del submódulo N64Recomp, y se comprobó que
puede revertirse sobre el checkout actual para evitar incluir metadatos gitlink.

## Qué se revisó sobre precisión numérica

- El header recomp.h fijado emplea RECOMP_FUNC con optimize("rounding-math")
  para GCC; set_cop1_cs traduce los cuatro modos de redondeo a fesetround.
- CVT_W_S/D usan lrintf/lrint para respetar el modo de redondeo.
- La compilación Release revisada usa -O3; no se configuró -ffast-math,
  -march=native ni instrucciones FMA específicas para el juego.
- Los hilos emulados se alojan en hilos del sistema; la revisión no mostró una
  modificación global de redondeo ligada a elegir 720p/1080p/4K.
- Esto no prueba equivalencia completa de la FPU con N64 ni descarta errores de
  aliasing/conversiones. Sin una reproducción o una divergencia identificada,
  no se cambiaron floats, coeficientes, redondeos ni rutinas de suspensión.

## Entrada: quitar trabajo de dispositivos del hilo de juego

La integración SDL3 también cambia el sondeo: SDL_Init, actualización, apertura,
cierre y búsqueda de dispositivos se ejecutan en el hilo Win32 principal.
Las llamadas del runtime y el hook SI solo leen un snapshot corto protegido.
El mutex compartido no se mantiene mientras SDL accede a dispositivos. Así, una
consulta HID/USB lenta no bloquea el hilo de simulación por ese mutex. Esto
mejora el aislamiento, pero por sí solo no explica un síntoma compartido con IA.
Si la interfaz deja de publicar input durante 250 ms se entregan controles
neutros para no dejar un acelerador pulsado por una ventana bloqueada.

## Comprobación pendiente para confirmar el diagnóstico

1. Comparar varias carreras en el primer circuito, nativo y 720p/1080p/4K,
   conservando auto, cámara, ruta y controles lo más parecidos posible.
2. Registrar vídeo donde se vea si cambia solo la orientación dibujada o también
   la trayectoria/velocidad del jugador y la IA.
3. Escuchar audio y comprobar que no hay nuevos bloqueos o pérdida de avance tras
   posponer SP. La escala autoritativa debe continuar en 1.0.
4. Si persiste, localizar las estructuras de vehículos y el tick lógico en los
   símbolos; registrar matrices, posición, velocidad, contactos y delta por tick,
   usando hooks de generación explícitos en lugar de editar C generado a mano.
5. Comparar esa traza con nativo y con una referencia de consola/emulador antes de
   modificar aritmética. No suavizar la carrocería para ocultar una divergencia.

## Fuentes revisadas

- N64ModernRuntime cdf5abbd5026fef5c364c676e4667c45e42b6863:
  ultramodern/src/events.cpp, gfx_thread_func, sp_complete y dp_complete.
- Header N64Recomp del runtime, revisión 81213c1831fab2521a6a5459c67b63437d67e253:
  include/recomp.h, RECOMP_FUNC, set_cop1_cs y conversiones.
- Host propio src/main.cpp: AsyncHDRenderer::submit, process_graphics_task,
  get_resolution_scale y adaptadores del renderer.
- Flags de la configuración CMake/Ninja local. No se ejecutaron benchmarks ni
  pruebas de juego nuevas en esta entrega; el resultado comprobado es compilación.
