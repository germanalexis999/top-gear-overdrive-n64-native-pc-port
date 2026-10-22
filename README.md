# Top Gear Overdrive — N64 Native PC Port

Port experimental para Windows x64 mediante recompilación estática. Incluye un
host Win32, audio WaveOut, controles de teclado/XInput, rasterizador nativo de
software y salida HD por GPU Direct3D 11 para display lists F3DEX2.
Requiere una ROM USA v0 aportada localmente.

## Descargar y jugar en Windows

Descarga el ZIP **Windows x64** de [Releases](https://github.com/germanalexis999/top-gear-overdrive-n64-native-pc-port/releases),
extráelo completo y abre `Jugar.cmd` o `tgod_native.exe`. Selecciona una ROM propia
USA v0 (`.z64`, `.v64` o `.n64`), que se comprueba y lee localmente. No necesitas
compilar. El paquete contiene el EXE, sus DLL y licencias; la ROM no se incluye.

El código continúa en `main`; los binarios se publican por separado como adjuntos.
El código propio del port está bajo GPL-3.0-only; ver [LICENSE](LICENSE).
Para crear tu propio ZIP y publicarlo, consulta el
[script de release y sus instrucciones](docs/RELEASES.md). Un push de fuentes
no publica automáticamente una nueva descarga.

Arranca, recorre los menús y entra a carrera. Se corrigieron la sobreproducción
de audio, bloqueos del scheduler y la corrupción del viewport que ocultaba la
escena 3D. La fidelidad visual y de audio todavía está en desarrollo.

F6 selecciona resolución interna nativa/720p/1080p/4K y guarda la configuración
en `tgod.ini`. Desde preview.7 se acelera HD con GPU y conservan
la sincronización nativa. Si no hay hardware compatible se usa la salida por CPU,
que es lenta en HD. Consulta los requisitos y la comparación de cuadros
en [BUILDING.md](docs/BUILDING.md#resolución-interna-y-configuración-gráfica).
La presentación HD se realiza directamente con DXGI, reutilizando recursos de GPU
y conservando las lecturas de framebuffer que necesitan los efectos del juego.

- [Compilación, ROM local, pruebas y controles](docs/BUILDING.md)
- [Estado, evidencia y pendientes](docs/STATUS.md)
- [Dependencias y avisos de terceros](THIRD_PARTY_NOTICES.md)
- [Empaquetar y publicar versiones separadas](docs/RELEASES.md)

## Contenido

| Ruta | Contenido |
| --- | --- |
| `src/`, `include/` | Host nativo y rasterizador |
| `config/`, `symbols/` | Configuración y metadatos de direcciones de USA v0 |
| `tools/` | Preparación de dependencias, generación y comprobaciones |
| `patches/` | Cambios locales para las revisiones upstream fijadas |
| `docs/` | Instrucciones y estado actual |

Los árboles de dependencias se descargan con `tools/bootstrap.ps1` y permanecen
excluidos de Git. El C del juego se genera localmente con `tools/prepare_recomp.py`.
El árbol Git no distribuye ROMs, assets, desensamblados, código generado,
guardados, capturas, música ni ejecutables. Las releases contienen el ejecutable
con lógica recompilada, las DLL del runtime y las licencias; los datos del juego
continúan en la ROM del usuario.

Antes de publicar cambios, revisar el índice y ejecutar:

```powershell
python tools/check_public_tree.py
```

El auditor usa una lista explícita de archivos permitidos y examina los bytes
del índice, de modo que también detecta contenido ya preparado para un commit.

4K conserva el formato 4:3: renderiza 2880×2160 y muestra barras laterales en una pantalla 3840×2160. Se selecciona con F6, RenderHeight=2160 o --render-height 4k. Requiere más trabajo de GPU que 1080p.

Mandos: hasta cuatro dispositivos Xbox/XInput, incluidos genéricos en ese modo.
Los slots XInput 0–3 corresponden a jugadores 1–4; el teclado comparte jugador 1,
sin consumir un puerto extra. Los puertos conservan su asignación al desconectar
un mando. La consola informa conexiones/desconexiones por jugador. Si el genérico
tiene selector X/D, usar X (XInput); un dispositivo solo DirectInput no usa esta ruta.
F11 o Alt+Enter alterna pantalla completa sin bordes; mantener la tecla pulsada
no vuelve a alternar. La resolución interna se cambia por separado con F6.

La propuesta de mapper de mandos, con comparación de los otros ports, está en
[CONTROLLER-MAPPING-RESEARCH.md](docs/CONTROLLER-MAPPING-RESEARCH.md).


Mapper de mandos: F7 abre Controles. Elige jugador 1–4 y mando XInput 1–4;
si el mando ya estaba asignado, se intercambia con el del otro jugador. El teclado
puede compartirse con un solo jugador. Selecciona Mando o Teclado, una acción y
una de sus tres asignaciones; suelta los controles y pulsa el botón/tecla o mueve
el eje. Escape cancela la captura. Hay zona muerta (0–50%) e inversión de ejes.
Guardar y cerrar aplica y escribe controls.json junto al EXE (con respaldo .bak);
Cancelar descarta los cambios. Mientras el panel está abierto el juego recibe
controles neutros; al cerrar, suelta los controles para volver a jugar.

Pendiente: SDL3 para mandos genéricos sin XInput. Esta versión del mapper usa
Xbox/XInput; si el mando genérico tiene selector X/D, usar X.
