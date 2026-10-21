Versión experimental para **Windows 10/11 x64**.

Descarga el ZIP de Windows de los archivos adjuntos, extráelo completo y abre
`Jugar.cmd` o `tgod_native.exe`. Selecciona una **ROM propia de Top Gear Overdrive
USA v0** (`.z64`, `.v64` o `.n64`). No necesitas compilar ni instalar herramientas.
El archivo «Source code» de GitHub corresponde a las fuentes para desarrollo.

El ZIP incluye el ejecutable, las tres DLL del runtime MinGW, instrucciones,
licencias y un manifiesto SHA-256. **No incluye ROM, datos extraídos del juego,
capturas, música ni guardados.** La ROM se lee localmente y no se envía a servidores.

Cambios principales:

- El backend de audio se despierta por WOM_DONE en vez de esperar sondeos de
  hasta 20 ms. Los últimos recorridos redujeron los vaciados observados de 56
  a 0–3, sin descartar audio; la escucha completa continúa pendiente.
- Luces ambiente/direccionales y coordenadas de reflexión desde normales.
  Pruebas de luz frontal/trasera, rotación del modelview, alfa y texgen.

- Corregida la sobreproducción de audio y la espera del scheduler por eventos AI.
- Corregida la interpretación del viewport, matrices y filas impares de texturas.
- Selector de ROM, soporte de los tres órdenes de bytes y verificación de revisión.
- Generación y empaquetado reproducibles; fuentes separadas de los binarios.

Quedan problemas de fidelidad en colores, showroom, HUD, iluminación y
transparencias, además de validación prolongada de audio y modos de juego.
El ejecutable y las DLL no tienen firma Authenticode.

El código propio del port se publica bajo GPLv3; cada dependencia conserva su
licencia y sus avisos. El paquete incluye enlaces al código fuente y los parches.
