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

## Distribución

Las fuentes se mantienen en main, las descargas en Releases y los ZIP locales
en la carpeta hermana release-packages. Cada paquete conserva su commit fuente
y SHA-256. Las capturas, PCM, ROMs y guardados de estas comprobaciones permanecen
locales y excluidos de Git y de los ZIP publicados.
