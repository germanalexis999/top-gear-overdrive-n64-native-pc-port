# Seguimiento de glitches: audio e iluminación

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

La carretera y el coche se dibujan, pero el coche todavía aparece plano/claro y
el showroom capturado permanece negro. No se atribuye una corrección visual
completa a esta implementación. Siguen pendientes TEXEL1 independiente, manejo
de LOD, combiner/blender, transparencias, HUD y la secuencia de cámara/showroom.

## Distribución

Las fuentes se mantienen en main, las descargas en Releases y los ZIP locales
en la carpeta hermana release-packages. Cada paquete conserva su commit fuente
y SHA-256. Las capturas, PCM, ROMs y guardados de estas comprobaciones permanecen
locales y excluidos de Git y de los ZIP publicados.
