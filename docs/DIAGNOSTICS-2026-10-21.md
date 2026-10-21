# Seguimiento de glitches: audio y gráficos

## Audio: corregir la espera del backend

Con el pacing AI ya corregido, todavía se observaban huecos: una prueba de
16 segundos detectó 56 transiciones de bloques en vuelo a cero, 520 bloques
encolados y cero descartes. El worker de WaveOut esperaba hasta 20 ms en una
condition variable antes de revisar los bloques terminados. WOM_DONE señalaba
otro evento, por lo que la espera del mixer no se reanudaba inmediatamente.

Ahora el worker espera sobre el evento de Windows compartido por WOM_DONE,
nuevas muestras, cambios de frecuencia y apagado. Una prueba de 16 s con solo
este cambio registró 3 transiciones a cero y 668 bloques. Después de implementar
iluminación/texgen, otra prueba de 16 s mantuvo carrera, 4/4 catálogos y EEPROM,
con 671 bloques, cero descartes/reemplazos y cero vaciados del dispositivo.

La prueba final del paquete preview.2, extraído con PATH limitado a Windows,
volvió a pasar carrera, catálogos y EEPROM: 664 bloques, cero descartes/reemplazos
y 4 vaciados observados. Por tanto el rango observado tras el cambio es 0–4;
todavía hay que distinguir las transiciones de arranque de huecos durante música.

El contador mide el estado observado por el backend; no sustituye una escucha
ni una medición del audio físico. El primer fragmento PCM analizado tiene 34,560
frames (1.567 s a 22,050 Hz), RMS 2,093, pico 23,311 y L/R idénticos. Los saltos
medios entre bloques fueron 128 frente a 93 dentro del bloque; sus máximos fueron
1,731 frente a 16,106. Este fragmento no confirma la identidad/fidelidad de toda
la pista ni resuelve por sí solo los glitches musicales restantes.

## Gráficos: iluminación y coordenadas de reflexión

La traza confirmó 546,042 vértices con G_LIGHTING y 2,592 cargas de luces en una
prueba de 16 s, pero el rasterizador interpretaba los bytes de normales como
colores. Se implementaron luz ambiente/direccional, NUMLIGHT, LIGHTCOL y las
cargas de luces/look-at de F3DEX2. El cálculo usa normales firmadas y el
modelview, sin aplicar traslación y conservando el alfa original del vértice.

G_TEXTURE_GEN/G_TEXTURE_GEN_LINEAR ahora generan coordenadas de reflexión desde
las normales, vectores look-at y escalas de textura. La ejecución posterior
procesó 549,391 vértices iluminados y 224,940 con texgen.

Las pruebas sin ROM verifican luz frontal, luz de espaldas, suma ambiente,
rotación del modelview, preservación de alfa y coordenadas de reflexión.
La semántica se contrastó con los handlers y shaders RSP de RT64; los avisos se
conservan en THIRD_PARTY_NOTICES.md y LICENSES/RT64-MIT.txt.

En esa iteración la carretera y el coche se dibujaban, pero el coche aparecía plano/claro y
el showroom capturado permanece negro. No se atribuye una corrección visual
completa a esta implementación. Siguen pendientes TEXEL1 independiente, manejo
de LOD, combiner/blender, transparencias, HUD y la secuencia de cámara/showroom.

## Gráficos: materiales, rectángulos y presentación VI

### TEXEL1, coordenadas y LOD

TEXEL1 reutilizaba TEXEL0. El muestreador ahora lee cada tile por separado,
aplica sus shifts/origen y respeta el modo punto en triángulos. En dos ciclos
se intercambian las entradas TEXEL0/TEXEL1 en el segundo ciclo, según RT64.
Se conservó la aproximación de TEXEL1 a TEXEL0 en un ciclo.

La primera prueba descubrió una regresión del HUD: con LOD activo y cero
mipmaps, el segundo selector debe leer la textura base, no el tile siguiente.
Ahora G_TEXTURE conserva el nivel máximo y su bit ON correcto; los gradientes
perspectivos seleccionan los niveles y calculan LOD_FRACTION. También se leen
PRIM_LOD_FRAC y el mínimo de SETPRIMCOLOR. Hay pruebas de magnificación,
minificación, límite máximo, selección real de un mip al dibujar y ambas
fracciones del combiner. Se usa aritmética flotante, no precisión RDP exacta.

### Alfa y rectángulos 2D

I4/I8 devolvían alfa 255: el hardware usa la propia intensidad como alfa.
Corregirlo recuperó visualmente carrocería amarilla, cristales oscuros y luces
rojas; la captura previa mostraba un coche casi blanco. Se añadieron IA16 y
RGBA32 con bancos RG/BA separados, comprobados con valores conocidos.

G_TEXRECT tenía otro lector TMEM que ignoraba el intercambio de las filas
impares. Ahora comparte el lector de tiles con los triángulos. Se corrigieron
TEXRECTFLIP, extremos exclusivos en uno/dos ciclos e inclusivos en COPY, y la
división del paso S por cuatro solo en COPY. Las pruebas comprueban las dos
filas conocidas, el paso 2x y el intercambio de ejes.

### Showroom negro: stride de VI entrelazado

La traza mostró targets de 640 píxeles, viewport 1280/960 en cuartos de píxel,
scissor 640x480 y VI_WIDTH=1280. El presentador descartaba anchuras mayores de
640 y volvía a 320x240. En este modo entrelazado VI_WIDTH es el salto entre
líneas del campo: el ancho almacenado es 640. El layout compartido de VI ahora
infiere 640x476 con los registros observados, en vez de presentar 320x240.

La captura posterior muestra coche, ruedas, suelo/reflejo y estadísticas en el
showroom. Persisten letras superiores cortadas y otros defectos de interfaz.
La inferencia de dimensiones sigue `hle/rt64_vi.cpp::fbSize`; no implementa
todos los filtros VI ni la reconstrucción exacta de campos entrelazados.

También había capturas leyendo RDRAM mientras el renderer borraba/dibujaba el
target: una llegó a mostrar pista sin coche ni HUD. Las capturas de showroom y
carrera ahora copian el último cuadro presentado desde el hilo de render.
Se eliminó el fallback que elegía otro buffer por tener más píxeles no negros.
El repintado Win32 protege el buffer RGB con un mutex y adapta tamaño/BITMAPINFO
al modo VI; ya no mantiene un vector fijo de 320x240 para imágenes mayores.

### Comprobación final y límites

- Build de `tgod_native` y `tgod_regression_checks`: correcto.
- Pruebas sin ROM: órdenes de bytes, filas en triángulos y rectángulos, shifts,
  origen, filtros, TEXEL1/ciclos/alfa, mipmaps/fracciones, formatos, VI normal y
  entrelazado, límites RDRAM, matrices, viewport, colas e iluminación/texgen.
- Recorrido final de 20 s: carrera activa hasta T+20, 4/4 catálogos, EEPROM de
  2048 bytes y 190,232 triángulos que requieren una segunda textura independiente.
- Audio en ese recorrido: cero descartes/reemplazos y cero vaciados observados.
  El usuario confirmó que la música se escucha bastante bien y pidió priorizar
  gráficos. No se modificó la mezcla ni el pacing para esta fase gráfica.

Quedan combiner/blender completos, alfa/coverage y profundidad exactos, textos
del showroom/HUD, orígenes de tile fraccionarios y filtrado RDP de tres puntos.
La presentación entrelazada necesita una comparación visual prolongada; las
capturas y las pruebas del layout no sustituyen esa comprobación interactiva.

### Diagnóstico de audio conservado

Antes de cambiar el foco a gráficos se amplió el diagnóstico headless: WAV de
PCM enviado a WaveOut, con canales host, segmentos al cambiar la frecuencia y
un máximo total de 60 s. La captura se finaliza explícitamente al apagar el
backend antes de quick_exit. Ya no se limita a los primeros 60 buffers.

Una ejecución de 30 s no registró llamadas al callback RSP vacío. La comparación
local con miniaudio 1.71 identificó stream 6 en menú y stream 1 en carrera; un
fragmento de menú obtuvo correlación 0.987 tras promediar grupos de 8 muestras.
Esto confirma contenido musical en ese fragmento, no fidelidad de toda la mezcla.
El decodificador de referencia se instaló solo en el build ignorado; no es una
dependencia del port y no se incluye en fuentes ni paquetes. Los PCM siguen
excluidos de Git. Fuente de la API de comparación: implementación oficial de
[pyminiaudio](https://github.com/irmen/pyminiaudio/blob/master/miniaudio.py).

## HUD y partículas: profundidad, combinador y filtrado

### Reproducción y resultado visible

Se conservó una captura anterior y se repitió una secuencia de 30 s con
aceleración, A+B durante los ticks 65–84 de carrera y A+Z/dirección durante
90–109. La navegación avanza por tiempo real; no es una comparación exacta del
mismo frame N64, porque el renderer software puede tardar distinto entre builds.

La imagen anterior ocultaba velocidad, marcha, posición de carrera y gran parte
del título del showroom. Con las correcciones aparecen esos elementos y el
título «Car Select / Press Z for color selection». En la prueba de frenado el
renderer anterior dibujaba destellos blancos; el nuevo conserva el tinte rojo
del material. La estela del derrape deja de ser una cadena de puffs blancos
opacos y se mezcla con el fondo. El usuario identificó color/tamaño/posición
como el síntoma del humo; no se declara paridad visual con consola.

### Causas verificadas en el código

- Se comparaba Z en todos los triángulos con G_ZBUFFER, incluso si el render
  mode desactivaba Z_CMP. Así se descartaban letras y números del HUD detrás
  del Z que había dejado la escena. Ahora se respeta Z_CMP (0x10).
- La escritura Z dependía de alfa >=192, en vez de Z_UPD (0x20). Los materiales
  de partículas pueden comparar profundidad sin escribirla. Ahora ambos
  controles son independientes y G_ZS_PRIM selecciona SETPRIMDEPTH.
- Los rectángulos de partículas ignoraban por completo Z. Ahora comparan y
  actualizan el Z primitivo cuando corresponde; COPY omite ese procesamiento.
- El blender siempre hacía mezcla source-alpha. Ahora decodifica P/M/A/B,
  FORCE_BL y los ciclos, con fuentes combiner/framebuffer/blend/fog y alfa
  combiner/fog/shade. Se implementaron umbral de alfa con el resultado del
  primer ciclo y una aproximación de dithering ordenado.
- Triángulos modificados a coordenadas de pantalla y rectángulos omitían el
  combinador de color. Ahora comparten su evaluación, incluidos color/alfa
  primitivo y de entorno. MODIFYVTX XY/Z conserva clip W/Z; el modo de
  perspectiva sigue el bit RDP, no el indicador de posición de pantalla.

### Material y filtrado de las partículas

La traza durante el derrape identificó G_TEXRECT con textura I8 de 8x8 ampliada,
combiner `ff9bff/fffefe38`, OtherMode `112ca0/0c1849dc` y colores de entorno
como `922800ad`, `7b1500b0` y `982d00b9`. El shader recibe, por tanto, un tinte
rojizo/marrón y un alfa variable desde el propio juego. No se sustituyeron por
un color gris arbitrario ni se cambió el tamaño de los emisores del juego.

El rectángulo pedía filtrado pero se muestreaba por puntos. Ahora usa filtrado
RDP de tres muestras y fracciones de 5 bits. También se sustituyó la aproximación
de cuatro taps de los triángulos, se eliminó su desplazamiento incorrecto de
medio texel y se conservó el caso central de G_TF_AVERAGE. SETTILESIZE mantiene
las fracciones de cuarto de texel del origen. Referencias locales contrastadas:
RT64 `TextureSampler.hlsli`, `rt64_blender.h`, `rt64_color_combiner.h`,
`rt64_rdp.cpp` y `rt64_rsp.cpp`; los avisos upstream permanecen en LICENSES/.

### Comprobaciones y límites

Las pruebas sin ROM cubren mezcla opaca/translúcida, alfa de shade/fog, ambos
ciclos, cobertura interior aproximada, umbral/dither, alfa del primer ciclo,
combinador de HUD/rectángulos, Z_CMP/Z_UPD, profundidad primitiva de partículas,
MODIFYVTX, las dos mitades del filtro de tres muestras, AVERAGE y origen
fraccionario. El chequeo del paquete rechaza --graphics-check sin --headless.

La ejecución final de 30 s con frenado/derrape terminó en Modo 2, verificó
4/4 catálogos y EEPROM de 2048 bytes, sin overruns ni descartes/reemplazos de
audio; hubo 3 transiciones observadas a cola de dispositivo vacía. No se cambió
el backend de audio en esta iteración.

`--headless --graphics-check --seconds 30` permite repetir la secuencia y crea
capturas de frenado, derrape y continuación en el directorio del EXE. No cambia
la conducción normal ni la navegación headless sin ese parámetro.

Actualización tras la prueba del usuario: confirma el HUD y el humo corregidos
en los casos observados. Se cierra el síntoma de color/escala/posición del humo.
Quedan coverage subpíxel, Z comprimido/delta/decal, el desbordamiento del blender,
noise/dither de hardware y los detalles de posicionamiento subpíxel de rectángulos.
Las trazas temporales detectaron también cambio de microcódigo a L3DEX y opcode
0x08 sin implementar; su efecto concreto no se atribuyó al humo. La siguiente
sección documenta la corrección de esa ruta.
Las trazas privadas y capturas no se publican con el código ni el ZIP.

## Líneas y cambios de microcódigo F3DEX2/L3DEX2

### Evidencia y causa

La prueba previa de 30 s registraba miles de opcode 0x08 ignorados y cambios
0xDD que tampoco se interpretaron. Una traza privada identificó un LOAD_UCODE
con texto físico `0x505E0`, datos `0x5EF00` y tamaño `0x800`; la vuelta usa
`0x4F250`/`0x5EAE0`. Los identificadores son `RSP Gfx ucode L3DEX ... fifo 2.05`
y `F3DEX ... fifo 2.05`: el nombre no lleva el sufijo «2», pero el ABI es 2.x.
La referencia RT64 local deja LINE3D como TODO/assert; copiar esa ruta no lo
resolvía. Se contrastaron los formatos con la implementación primaria de
[L3DEX2 en GLideN64](https://github.com/gonetz/GLideN64/blob/master/src/uCodes/L3DEX2.cpp)
y el ancho con su
[gSPLine3D](https://github.com/gonetz/GLideN64/blob/master/src/gSP.cpp).

Los primeros comandos, como `08000201/00000000`, unen los vértices 0 y 1 con
ancho 2 píxeles. Sus segmentos son pequeños y próximos al vehículo en la
secuencia observada; se comprobó que faltaba su dibujo, sin atribuirles un
efecto concreto del juego ni confundirlos con el humo ya confirmado.

### Corrección

- Selección por identificador en memoria N64, con lectura de bytes y límites
  de RDRAM; no se incorporan binarios de microcódigo ni direcciones fijas al código.
- Cada OSTask proporciona su dirección/tamaño de datos. LOAD_UCODE usa la dirección
  física de RDPHALF_1, cambia el despacho y descarta los enlaces de retorno del DL.
  Mantiene TMEM y estado RDP. Se diagnostican microcódigos no reconocidos.
- LINE3D decodifica índices duplicados y ancho firmado `1.5 + wd/2`.
  Ignora culling de polígonos y texturas residuales; usa shade plano/suave,
  combinador, blender y Z_CMP/Z_UPD/Z primitivo. Bajo L3DEX2, TRI1/TRI2 dibujan bordes.
- Recorte del segmento antes de dividir por W, scissor y límites de framebuffer.
  Cada píxel se procesa una vez para evitar doble mezcla en una diagonal interna.

La semántica de ancho, estado y profundidad también se contrastó con los manuales
primarios de
[gSPLine3D](https://ultra64.ca/files/documentation/online-manuals/functions_reference_manual_2.0i/gsp/gSPLine3D.html)
y [gSPLoadUcode](https://ultra64.ca/files/documentation/online-manuals/functions_reference_manual_2.0i/gsp/gSPLoadUcode.html).
La implementación software es propia; no se incorporan archivos de GLideN64.

### Comprobaciones y límites

Las pruebas sin ROM verifican identificadores, memoria truncada/fuera de rango,
LOAD_UCODE y regreso a F3DEX2, TRI1/TRI2 de líneas, color plano/suave, ancho,
mezcla sin doble escritura, scissor, recorte cerca de cámara, índices inválidos,
ancho no positivo, profundidad primitiva y comparación/escritura Z independientes.

En la primera ejecución corregida de 30 s: 300 cambios, 2224 segmentos aceptados
para rasterización y 1098 píxeles de línea escritos. Carrera activa hasta el final,
4/4 catálogos, EEPROM de 2048 bytes, cero overruns/descartes/reemplazos de audio y
6 transiciones a dispositivo vacío. No se registraron comandos desconocidos.
Las capturas conservan los elementos recuperados del HUD y partículas.

La ejecución final del binario de 60 s mantuvo carrera activa y verificó 4/4
catálogos y EEPROM de 2048 bytes. Registró 786 cambios, 5278 segmentos aceptados
y 4554 píxeles de línea; cero comandos desconocidos y cero microcódigos sin
reconocer. Audio: cero overruns/descartes/reemplazos y 6 transiciones observadas
a dispositivo vacío. La batería sin ROM también pasó después de añadir casos
de retorno al microcódigo de triángulos y LOAD_UCODE dentro de un DL hijo.

El trazado de líneas usa centros de píxel y extremos rectos: no reproduce la
cobertura/antialias subpíxel ni todos los detalles del trapezoide RDP. Esta revisión
no implica fidelidad completa de líneas ni validación de todos los circuitos.
Esta corrección se incorpora al paquete preview.5; la descarga preview.4
conserva el renderer anterior sin soporte de líneas L3DEX2.

## Intro: logos negros y nieve sin fondo

### Reproducción y causa

El usuario informó gráficos rotos en los logos de arranque, incluido Snowblind.
La navegación automática usada antes pulsaba Start/A a partir de 2,5 s, antes
de observar la animación completa. El nuevo recorrido `--headless --intro-check`
mantiene los botones en cero y captura un cuadro presentado cada 0,5 s.

En la reproducción de 40 s, KEMCO salía negro; después solo se veían puntos de
nieve y restos del mismo patrón al avanzar a la portada. No aparecían comandos
desconocidos. La imagen fuente de KEMCO estaba intacta en RDRAM: el fallo ocurría
al muestrear TMEM, sin modificar ni sustituir los assets de la ROM.

La traza identificó bandas RGBA16 de 320x6, COPY, stride de 640 bytes y máscaras
S/T de 15. El código confundía el período de máscara `1 << 15` con las dimensiones
de la imagen y rechazaba el muestreo por exceder 1024. Los píxeles de fondo no
se escribían, aunque la carga a TMEM contenía datos válidos. Las partículas sí
usaban tiles pequeños y aparecían sobre negro. La portada quedaba afectada por
el mismo rechazo de sus bandas.

### Corrección y alcance

- Las dimensiones/stride se calculan desde SETTILESIZE y `line`, independientes
  de las máscaras. Se aceptan máscaras 15 sin crear una imagen de 32768x32768.
- Clamp usa los límites del tile; después se aplican máscara y mirror. Sin clamp
  ni máscara, no se añade un wrap artificial en el borde de SETTILESIZE. Los
  accesos a TMEM siguen siendo circulares y el direccionamiento de nibbles
  negativos conserva división hacia menos infinito.
- Se elimina la misma confusión en la elección de tiles de triángulos, que podía
  seleccionar un tile 0 residual al encontrar máscaras grandes.
- La semántica se contrastó con `TextureSampler.hlsli::clampWrapMirrorSample`
  y `Math.hlsli::modulo` de la referencia RT64 local; se conserva el aviso MIT.
  No fue necesario cambiar los logos, emisores de nieve ni código generado.

### Evidencia y límites

La secuencia final corregida de 38 s mostró KEMCO, Snowblind con su animación de nieve,
portada y menú. Se verificaron 4/4 catálogos, EEPROM de 2048 bytes y cero
overruns/descartes/reemplazos de audio; hubo 1 transición observada a dispositivo
vacío y ningún microcódigo sin reconocer. La carrera con frenado/derrape se repitió
durante 30 s: Modo 2 activo hasta el final, HUD/partículas visibles, 4/4 catálogos,
EEPROM y cero descartes/reemplazos; 2 transiciones observadas a dispositivo vacío.

Las regresiones sin ROM incluyen una banda COPY sintética 320x6 con máscaras 15,
clamp+mask, ausencia de wrap artificial sin máscara, nibbles negativos y selección
de textura de triángulos. Los controles del recorrido de intro también rechazan
combinaciones inválidas. Las capturas y los volcados temporales de diagnóstico
permanecen en build ignorado y no se distribuyen.

Es una comprobación visual local; no implica paridad de timing, coverage o VI
con consola. La corrección se incorpora a preview.6; preview.5 conserva la intro anterior.

## Resolución interna: comparación de elementos lejanos

El usuario preguntó por carteles y objetos lejanos con aspecto de baja resolución
y pidió poder probar 1080p. Se verificó que F1–F4 únicamente cambiaban el tamaño
de ventana y F5 el filtro GDI: el renderer de carrera seguía rasterizando 320x240.

### Implementación

- F6 selecciona nativo/720p/1080p al inicio de la siguiente lista de dibujo.
  Se guarda `RenderHeight=0/720/1080` en `[Graphics]` de `tgod.ini`, junto al EXE.
  `--render-height native|720|1080` permite una elección por sesión.
- Se conserva 4:3: 960x720 o 1440x1080 en carrera. La pantalla completa 1920x1080
  presenta barras laterales. No se modifica FOV, relación de aspecto ni assets.
- `render_surface.hpp` aloja color y profundidad ampliados fuera de los 8 MiB de
  RDRAM, con un máximo de ocho superficies. Se conservan direcciones y strides
  nativos del juego y un espejo para texturas de framebuffer dentro del replay.
- Triángulos, líneas, rectángulos y clears comparten escala. Las pruebas Z usan
  profundidad ampliada; el LOD tiene en cuenta el tamaño de un píxel interno.
  La presentación y las capturas leen el target ampliado y aplican el offset VI.
- En la copia privada se sincroniza el espejo nativo al terminar un DL y antes de cargar una textura
  que solape un framebuffer modificado. Los cambios de CPU entre tareas se
  importan al cache. La RDRAM real y sus efectos producidos por CPU siguen siendo nativos;
  esto no implementa fidelidad completa del framebuffer RDP ni su endian layout.
- Nativo sigue siendo el valor predeterminado. En headless se ignora la preferencia
  guardada salvo `--render-height`, para conservar el recorrido de pruebas.

### Comparación y comprobaciones

`--headless --resolution-check --seconds 30` captura una lista de carrera y su
estado inicial/RDRAM y la reproduce en 720p y 1080p sobre copias privadas. Guarda
`framebuffer_compare_native.bmp`, `framebuffer_compare_720p.bmp` y
`framebuffer_compare_1080p.bmp`. No compara dos carreras que estén avanzando.
La salida gráfica y los volcados temporales permanecen excluidos del repositorio.

La batería sin ROM prueba que un triángulo pequeño sin píxeles en nativo sí
genera muestras a 1080p: no es un reescalado de la imagen 320x240. También cubre
dimensiones 4:3, target 640x480, cambios de resolución, Z/clears, HUD, L3DEX2,
offset VI, escrituras CPU, readback antes de LOADBLOCK y expulsión del cache.
Se rechazan valores y combinaciones CLI inválidos.

En el recorrido final de 30 s con frenado/derrape y comparación: carrera activa,
4/4 catálogos, EEPROM de 2048 bytes, cero overruns/descartes/reemplazos de audio,
3 transiciones observadas a dispositivo vacío y ningún comando/microcódigo
desconocido. Una ejecución separada de 8 s en 1080p presentó y capturó imágenes
de intro de 1440x1080, con 4/4 catálogos y cero descartes/reemplazos de audio.

### Resultado y límites

El mismo cuadro de carrera se dibujó en aproximadamente 318 ms a 720p y 714 ms
a 1080p en esta máquina. Es el coste de un replay concreto; no un promedio de
FPS ni un benchmark de todos los modos. HD es muy lento con el rasterizador
por CPU actual, por lo que se ofrece para diagnóstico y comparación. Para jugar
en HD con fluidez hace falta acelerar el rasterizador o incorporar un backend GPU.

En las capturas mejoran los contornos de coche, barreras y geometría lejana.
Los carteles y otros materiales conservan el detalle de sus texturas y mipmaps
originales. El usuario confirmó que la apariencia pixelada a distancia desaparece
al activar HD. Aumentar resolución no permite concluir que todos los defectos a
distancia sean propios del juego; sigue pendiente contrastar LOD/filtrado con
una referencia. VI, coverage y efectos de CPU conservan las aproximaciones
documentadas. Estos cambios son posteriores al ZIP preview.5.

### Reporte de físicas y separación del trabajo HD

El usuario observó físicas alteradas al activar 720p/1080p. La primera versión
hacía el trabajo HD dentro de `RendererContext::send_dl`. La ruta de eventos del
runtime emite `dp_complete()` después de que ese método retorna; un cuadro HD de
cientos de ms retrasaba ese evento mientras avanzaban los VI. No era suficiente
conservar las coordenadas y direcciones nativas para mantener el ritmo del juego.

La corrección mantiene siempre el intérprete autoritativo y `get_resolution_scale`
en nativo. Un hilo Windows separado, con prioridad reducida, reproduce las listas
sobre RDRAM y estado RSP/RDP privados. El submit solo captura una instantánea como
máximo cada 100 ms; hay un trabajo activo y uno pendiente, que se sustituye por
el más reciente. Los cambios de F6 invalidan resultados anteriores y nunca esperan
a que termine una imagen HD. Las transiciones de escena descartan imágenes viejas.

El hilo HD no entrega eventos SP/DP, no modifica la RDRAM real ni controla el
paso de simulación. La presentación usa el último resultado compatible; mientras
no está disponible muestra el cuadro nativo. Esto conserva el avance habitual,
pero la imagen HD aún puede ser poco fluida y tener retraso. Nativo sigue siendo
recomendable para jugar con menor latencia. El replay sincrónico de
`--resolution-check` es un diagnóstico headless de un cuadro, separado de F6.

En dos recorridos de 30 s con aceleración/frenado/derrape:

- 1080p: 765 listas nativas, 64 imágenes HD completadas y 204 instantáneas
  pendientes sustituidas; último replay HD de 595 ms. Una transición de audio
  a dispositivo vacío, cero overruns/descartes/reemplazos, 4/4 catálogos y EEPROM.
- 720p: 783 listas nativas, 128 imágenes HD y 140 sustituciones; último replay
  de 287 ms. Trabajo nativo medio de 29913 us, máximo 85450 us, escala lógica 1.
  Dos transiciones de audio a vacío y cero descartes/reemplazos; 4/4 y EEPROM.

Se observaron HUD, vehículo y partículas en las capturas HD de carrera. Estos
recorridos verifican el avance del motor y la separación del evento DP, no una
comparación completa de todas las colisiones/físicas contra consola. La prueba
del ZIP incluye una carrera adicional de 20 s con salida 1080p asíncrona y
comprueba que hay imágenes HD terminadas y que el renderer autoritativo sigue en 1x.

## Distribución

Las fuentes se mantienen en main, las descargas en Releases y los ZIP locales
en la carpeta hermana release-packages. Cada paquete conserva su commit fuente
y SHA-256. Las capturas, PCM, ROMs y guardados de estas comprobaciones permanecen
locales y excluidos de Git y de los ZIP publicados.
