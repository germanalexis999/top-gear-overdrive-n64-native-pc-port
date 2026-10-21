## v0.1.0-preview.3 — Windows 10/11 x64

Descarga el ZIP de Windows de los archivos adjuntos, extráelo completo y abre
`Jugar.cmd` o `tgod_native.exe`. Selecciona una **ROM propia de Top Gear Overdrive
USA v0** (`.z64`, `.v64` o `.n64`). No necesitas compilar ni instalar herramientas.
El archivo «Source code» de GitHub corresponde a las fuentes para desarrollo.

El ZIP incluye el ejecutable, las tres DLL del runtime MinGW, instrucciones,
licencias y un manifiesto SHA-256. **No incluye ROM, datos extraídos del juego,
capturas, música ni guardados.** La ROM se lee localmente y no se envía a servidores.

Cambios principales:

- Recuperado el showroom: presentación del modo VI entrelazado de 640 píxeles,
  cuyo stride entre líneas del campo era 1280 y se interpretaba como 320x240.
- Coche con color y detalles al corregir el alfa de texturas I4/I8.
- TEXEL1 independiente, niveles/fracciones LOD y coordenadas por tile.
- Rectángulos 2D: filas impares de TMEM, TEXRECTFLIP y paso correcto en COPY.
- Formatos IA16 y RGBA32 con bancos separados; repintado Win32 sincronizado.
- Capturas desde el último cuadro presentado para evitar imágenes a medio dibujar.
- Script único de compilación, pruebas, empaquetado y publicación opcional.

Las pruebas de fuentes incluyen casos de materiales, LOD, formatos y VI. Un
recorrido de 20 s mantuvo carrera, 4/4 catálogos, EEPROM y cero descartes de audio.
El ZIP se comprueba por separado con PATH limitado a Windows, tres órdenes de
bytes de ROM y una carrera de 16 s. La ROM de prueba no forma parte del paquete.

Persisten textos recortados del showroom, detalles del HUD y diferencias en
blender, transparencias, profundidad y filtros RDP/VI. La música conserva las
mejoras anteriores; queda validación prolongada de pistas y modos de juego.
El ejecutable y las DLL no tienen firma Authenticode.

El código propio del port se publica bajo GPLv3; cada dependencia conserva su
licencia y sus avisos. El paquete incluye enlaces al código fuente y los parches.
