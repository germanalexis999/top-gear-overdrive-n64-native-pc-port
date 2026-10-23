## v0.1.0-preview.11 — rendimiento CPU, detalle y distancia

- Optimiza muestreo de texturas y cálculo LOD del rasterizador CPU: evita
  operaciones repetidas y lecturas de peso cero, conservando los límites de
  redondeo. Tres tareas reales tardaron aproximadamente la mitad, con salida
  RDRAM idéntica. Carrera 4K/Full: ~22 tareas/s frente a ~12.6 antes; CPU ~33 ms
  frente a ~67 ms. Pruebas de referencia, regresiones CPU/GPU y DXGI aprobadas.
- Benchmark CPU opcional con capturas privadas de tareas; esos datos de juego
  y los buffers de comparación quedan fuera del código y de los paquetes.
- Corrige el contador que llamaba FPS a la frecuencia VI. Muestra VI Hz,
  tareas gráficas/s y tiempos CPU/GPU, para diagnosticar fluidez HD. No se
  confirma todavía la causa de la lentitud 4K reportada al alternar F6.
- Modo de diagnóstico `--performance-check`: comparación con las opciones reales
  del INI y resolución fija. Corridas locales y capturas documentadas; la
  optimización reduce el coste nativo, con margen pendiente de mejora.
- Reduce trabajo al importar buffers HD: reutiliza el muestreo horizontal y
  copia filas idénticas. Equivalencia de píxeles comprobada en 48 importaciones.
  Replay HD medio 4K/Full: 19.4 → 13.3 ms en corrida local de 24 s; sin mejora
  demostrada del ritmo de tareas ni de latencia visible.

- Corrige el cierre de arranque al seleccionar Full desde el host: extensión
  de signo de la dirección KSEG. Corrige dos lecturas similares de las mejoras.
  Reproducido en GDB antes y arranque confirmado después con perfil 4K/Full.

- Interruptor LegacyVersion=true/false: legacy fuerza los gráficos originales
  y bloquea F6 HD; false usa el perfil mejorado con preferencias individuales.
  Conserva los valores guardados para volver al otro perfil. Requiere reinicio.

- Nuevas claves en tgod.ini: `ForceHighDetail`, `DrawDistancePercent` y
  `ExtraTrackSections`. Selección del modelo cercano en la rama con variantes,
  ampliación conjunta de distancias de cámara y consultas extra de decorado.
- Valores iniciales del perfil mejorado: 1080p/Full, detalle alto, 200% y dos
  sectores extra; legacy fuerza nativo/game/0/100/0. Reiniciar al cambiar. Se conservan las
  comprobaciones de capacidad y el frustum. No promete eliminar todo el popping.
- Compilado y primeras carreras automáticas de rendimiento ejecutadas. Falta
  validar conducción, todos los efectos y multijugador. Las tareas gráficas/s
  y VI Hz no equivalen a cuadros nuevos realmente mostrados; no se acredita
  un objetivo de 60 FPS ni se establece aquí el ritmo original de la consola.

## v0.1.0-preview.10 — Expansion Pak y escalado original

- Escalado separado por eje para los modos originales 640×120/240 y la intro
  640×480. Dimensiones capturadas por tarea, LOD/readbacks/scanout adaptados y
  recreación de cachés al cambiar altura. Half conserva sus bandas.
- Preferencia de arranque `OriginalResolution=game|off|half|full` en tgod.ini o
  `--original-resolution`, aplicada una vez tras cargar los ajustes originales.
  `game` por defecto; siguen disponibles Setup y la selección HD con F6.
- Compilado con los shaders actualizados. El usuario confirmó que funciona muy
  bien y autorizó su publicación. Sigue pendiente una comparación exhaustiva
  de todos los modos y multijugador; no se confirmó mejora de física.
- ZIP Windows x64 con EXE, SDL3.dll, DLL MinGW y licencias. Requiere una ROM
  propia USA v0; no incluye ROM, datos extraídos, capturas ni guardados.

## v0.1.0-preview.9 — SDL3 y sincronización gráfica

- Entrada SDL3 3.4.18: gamepads reconocidos y joysticks genéricos, con nombres de
  dispositivo y captura manual de botones, ejes y crucetas en F7. Sin doble sondeo XInput.
- Se cargan controles v1; guardado v2 con centro de eje capturado para joysticks.
  Hasta cuatro mandos, 16 ejes/64 botones/4 crucetas por joystick sin mapping.
- SDL y Win32 se sondean en el hilo principal; el juego consume snapshots sin
  esperar a lecturas de dispositivos. SDL3.dll y licencia zlib incluidas.
- Pospuesto SP gráfico hasta consumir las entradas nativas de la lista. El trabajo
  HD continúa asíncrono. Corrección candidata para una carrera de matrices/vértices;
  el tambaleo intermitente reportado sigue pendiente de confirmación en carrera.
- No se cambió el redondeo ni la simulación. Compilado; faltan pruebas interactivas.

## v0.1.0-preview.8 — mapper, mandos y pantalla completa

- Nuevo panel F7: perfiles por jugador, asignacion de mando/teclado, tres bindings
  por accion, zona muerta, inversion, captura y guardado controls.json.
  El usuario confirmó el funcionamiento del mapper. SDL3 para genéricos sin
  XInput sigue pendiente.
- Se consultan los cuatro slots XInput y se transmiten cuatro puertos al juego,
  con botones, stick y estado de conexión independientes. Se eliminó la máscara
  fija de jugador 1 y el límite de un canal en el hook SI.
- El teclado comparte jugador 1 por defecto y puede asignarse a otro jugador;
  los mandos adicionales no replican su entrada.
  Se limpia la entrada y el error de puerto se actualiza al desconectar/reconectar.
- F11 y Alt+Enter ignoran repetición de tecla al alternar pantalla completa.
- Compatible con mandos Xbox y genéricos configurados en modo XInput.

## v0.1.0-preview.7 — Windows 10/11 x64

Versión preliminar con renderizado HD por GPU y opción 4K.

Descarga el ZIP de Windows de los archivos adjuntos, extráelo completo y abre
`Jugar.cmd` o `tgod_native.exe`. Selecciona una **ROM propia de Top Gear Overdrive
USA v0** (`.z64`, `.v64` o `.n64`). No necesitas compilar ni instalar herramientas.
El archivo «Source code» de GitHub corresponde a las fuentes para desarrollo.

El ZIP incluye el ejecutable, las tres DLL del runtime MinGW, instrucciones,
licencias y un manifiesto SHA-256. **No incluye ROM, datos extraídos del juego,
capturas, música ni guardados.** La ROM se lee localmente y no se envía a servidores.

Cambios principales:

- F6 añade 4K después de 1080p: 2880×2160 en formato 4:3, persistencia
  RenderHeight=2160 y arranque con --render-height 4k (o 2160).
  Capturas y buffers admiten esa resolución; la lógica conserva su escala nativa.

- Presentación HD directa por DXGI, conversión/filtro/barras 4:3 en GPU y
  reutilización de buffers/vistas. Se omite la descarga final de pantalla a CPU;
  se mantienen los readbacks necesarios para efectos y la recuperación a GDI.
- Verificación de ventana con cambios de resolución, resize, fullscreen y filtro.
  Contadores de recursos/readback y mediciones CPU/GPU separadas sin espera.
- Salida HD acelerada con GPU Direct3D 11, reutilizando la interpretación y el
  recorte del renderer existente. El shader propio queda incrustado en el EXE.
- Se mantiene el procesamiento nativo y la entrega de eventos SP/DP; la GPU
  trabaja sobre copias privadas. Se elimina el límite fijo de 10 imágenes HD/s.
- En RTX 3080 Ti, el mismo cuadro pasó de 326/726 ms por CPU a unos 12 ms por GPU
  a 720p/1080p. Son tiempos de un replay, no una promesa de FPS del juego completo.
- Comprobaciones CPU/GPU sin ROM: formatos, filtrado, profundidad, mezcla,
  líneas, máscaras de intro y cambios entre targets 320/640.
- Reparada la intro: KEMCO, Snowblind y portada vuelven a dibujarse. Las máscaras
  de coordenadas ya no se confunden con las dimensiones de las texturas.
- F6 selecciona resolución nativa, 720p, 1080p o 4K y guarda la preferencia en
  `tgod.ini`. Se mantiene 4:3: 960x720, 1440x1080 y 2880x2160, con barras laterales en 16:9.
- El trabajo HD se realiza en un hilo separado sobre copias privadas. El juego
  conserva el render y los eventos nativos para evitar la alteración de físicas
  observada cuando el modo HD se ejecutaba de forma sincrónica.
- Comparación del mismo cuadro en tres resoluciones con `--resolution-check`,
  y recorrido completo de la intro mediante `--intro-check`, ambos headless.
- Implementado el dibujo de líneas L3DEX2, que antes se descartaban durante
  la carrera: ancho, color plano/suave, mezcla, recorte y controles de profundidad.
- Selección de microcódigo desde cada tarea y cambios mediante LOAD_UCODE,
  incluido el regreso al dibujo de triángulos F3DEX2.
- Nuevas comprobaciones de líneas y cambios de microcódigo, y contadores para
  identificar comandos que no se puedan interpretar.
- Recuperados velocidad, marcha y posición de carrera del HUD, y título del
  showroom: el renderer ahora respeta cuándo comparar o escribir profundidad.
- Rectángulos y triángulos comparten el combinador y los controles de mezcla.
  Se conservan los colores/opacidades pedidos para luces y partículas.
- Las partículas rectangulares respetan profundidad primitiva y filtrado.
- Filtrado RDP de tres muestras, AVERAGE y orígenes fraccionarios de tile.
- MODIFYVTX conserva clip W/Z y respeta los modos de perspectiva/filtrado.
- Recorrido de diagnóstico con frenado/derrape mediante --graphics-check.

**HD requiere hardware Direct3D 11 con feature level 11.0.** Si no está disponible
o falla el replay, se informa y se vuelve al renderer por CPU, que sigue siendo
lento en HD. Nativo es el valor predeterminado. La presentación HD usa DXGI
y conserva el ritmo nativo; no se convierte el juego a 60 FPS. Hay pequeñas diferencias de
redondeo CPU/GPU; todavía falta comprobar otros adaptadores y más escenas.
El hilo HD descarta cuadros pendientes para conservar el avance nativo del juego.
Las texturas siguen siendo las originales; aumentar resolución mejora contornos
pero no crea detalle en sus imágenes.

Las pruebas de fuentes incluyen máscaras de intro, resoluciones, readback,
aislamiento de RDRAM, HUD, profundidad y líneas. Los recorridos de 30 s en
720p/1080p mantuvieron carrera, 4/4 catálogos, EEPROM y cero descartes/reemplazos
de audio. La salida HD entregó 703/691 imágenes en recorridos separados de 30 s,
con replays medios de 9,1/14,4 ms y escala nativa 1. Se verificó la intro hasta
el menú durante 50 s con salida GPU 1080p y sin descartes de audio.
El ZIP se comprueba por separado con PATH limitado a Windows, tres órdenes de
bytes de ROM, una carrera de 16 s en nativo y otra de 20 s con salida 1080p
asíncrona. La ROM de prueba no forma parte del paquete.
Cuando se detecta GPU también se comprueba la ventana DXGI durante 24 s, con
cambios de resolución, resize/fullscreen/filtro y cero readbacks finales de pantalla.

El usuario confirmó que el HUD y el humo quedaron corregidos en sus pruebas.
Persisten diferencias de coverage, profundidad comprimida, dither, mezcla y
campos VI; el antialias y los extremos de las líneas son aproximados. La música conserva las
mejoras anteriores; queda validación prolongada de pistas y modos de juego.
El ejecutable y las DLL no tienen firma Authenticode.

El código propio del port se publica bajo GPLv3; cada dependencia conserva su
licencia y sus avisos. El paquete incluye enlaces al código fuente y los parches.
