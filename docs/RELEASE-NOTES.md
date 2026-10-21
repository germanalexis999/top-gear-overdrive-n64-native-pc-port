## v0.1.0-preview.5 — Windows 10/11 x64

Descarga el ZIP de Windows de los archivos adjuntos, extráelo completo y abre
`Jugar.cmd` o `tgod_native.exe`. Selecciona una **ROM propia de Top Gear Overdrive
USA v0** (`.z64`, `.v64` o `.n64`). No necesitas compilar ni instalar herramientas.
El archivo «Source code» de GitHub corresponde a las fuentes para desarrollo.

El ZIP incluye el ejecutable, las tres DLL del runtime MinGW, instrucciones,
licencias y un manifiesto SHA-256. **No incluye ROM, datos extraídos del juego,
capturas, música ni guardados.** La ROM se lee localmente y no se envía a servidores.

Cambios principales:

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

Las pruebas de fuentes incluyen líneas, cambios de microcódigo, HUD, mezcla/alfa,
controles Z, profundidad primitiva y filtrado. Un recorrido de gráficos de 60 s
mantuvo carrera, 4/4 catálogos, EEPROM y cero descartes de audio; no se registraron
comandos desconocidos ni microcódigos sin reconocer.
El ZIP se comprueba por separado con PATH limitado a Windows, tres órdenes de
bytes de ROM y una carrera de 16 s. La ROM de prueba no forma parte del paquete.

El usuario confirmó que el HUD y el humo quedaron corregidos en sus pruebas.
Persisten diferencias de coverage, profundidad comprimida, dither, mezcla y
campos VI; el antialias y los extremos de las líneas son aproximados. La música conserva las
mejoras anteriores; queda validación prolongada de pistas y modos de juego.
El ejecutable y las DLL no tienen firma Authenticode.

El código propio del port se publica bajo GPLv3; cada dependencia conserva su
licencia y sus avisos. El paquete incluye enlaces al código fuente y los parches.
