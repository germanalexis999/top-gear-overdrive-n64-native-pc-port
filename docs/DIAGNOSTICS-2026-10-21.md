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

## Distribución

Las fuentes se mantienen en main, las descargas en Releases y los ZIP locales
en la carpeta hermana release-packages. Cada paquete conserva su commit fuente
y SHA-256. Las capturas, PCM, ROMs y guardados de estas comprobaciones permanecen
locales y excluidos de Git y de los ZIP publicados.
