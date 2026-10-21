# Top Gear Overdrive — N64 Native PC Port

Port experimental para Windows x64 mediante recompilación estática. Incluye un
host Win32, audio WaveOut, controles de teclado/XInput y un rasterizador de
software para display lists F3DEX2. Requiere una ROM USA v0 aportada localmente.

Arranca, recorre los menús y entra a carrera. Se corrigieron la sobreproducción
de audio, bloqueos del scheduler y la corrupción del viewport que ocultaba la
escena 3D. La fidelidad visual y de audio todavía está en desarrollo.

- [Compilación, ROM local, pruebas y controles](docs/BUILDING.md)
- [Estado, evidencia y pendientes](docs/STATUS.md)
- [Dependencias y avisos de terceros](THIRD_PARTY_NOTICES.md)

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
No se distribuyen ROMs, assets, desensamblados, código generado, guardados,
capturas, música ni ejecutables.

Antes de publicar cambios, revisar el índice y ejecutar:

```powershell
python tools/check_public_tree.py
```

El auditor usa una lista explícita de archivos permitidos y examina los bytes
del índice, de modo que también detecta contenido ya preparado para un commit.
