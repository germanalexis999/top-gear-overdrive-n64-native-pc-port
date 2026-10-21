# Dependencias y referencias

El árbol Git distribuye el host del port, metadatos de configuración,
herramientas propias y parches locales. No incluye árboles de código de terceros,
ROMs, assets, desensamblados, C generado del juego, bibliotecas ni ejecutables.
El código propio del port se publica bajo GPL-3.0-only; ver `LICENSE`.

Las releases de Windows se distribuyen por separado con el EXE y las DLL
`libgcc_s_seh-1.dll`, `libstdc++-6.dll` y `libwinpthread-1.dll`. No contienen ROM
ni archivos de datos extraídos del juego. El EXE contiene la lógica recompilada
y necesita una ROM propia para cargar los datos durante la ejecución.

Las dependencias se descargan por separado desde sus repositorios originales:

| Proyecto | Función | Licencia upstream |
| --- | --- | --- |
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) | Runtime y APIs de N64 | GPL versión 3; ver `COPYING` en el checkout |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Generación local de C y ABI | MIT, copyright Wiseguy; ver `LICENSE` |
| GNU libgcc / libstdc++ 16.2.0 | DLL del compilador | GPLv3 con GCC Runtime Library Exception 3.1 |
| MinGW-w64 / winpthreads 14.0.0 | Runtime Windows / hilos POSIX | Avisos de MinGW-w64 y MIT/BSD en `LICENSES/` |
| miniz, o1heap, fmt, nlohmann-json, ELFIO, tomlplusplus | Bibliotecas enlazadas | MIT y avisos upstream en `LICENSES/` |
| xxHash, concurrentqueue, rabbitizer, sljit | Bibliotecas enlazadas | Licencias permisivas y avisos completos en `LICENSES/` |

Las revisiones se fijan en `dependencies.lock.json`. Sus subdependencias conservan
sus licencias upstream. Los parches locales modifican esos proyectos y conservan
las licencias de los archivos afectados; no sustituyen los avisos originales.
Los textos de licencia pertinentes se conservan en `LICENSES/`.
El paquete binario también incluye `SOURCE.md` con la revisión de fuentes,
instrucciones de reconstrucción, fuentes de dependencias y parches. No se copian
DLL del sistema de Windows. Se conserva `GCC-RUNTIME-EXCEPTION.txt` sin modificar.

El trabajo sobre semántica de TMEM, matrices y decodificación de comandos se
contrastó con [RT64](https://github.com/rt64/rt64), MIT, copyright RT64 Contributors.
Se conserva su aviso MIT por las adaptaciones y referencias del rasterizador.
RT64 no se incorpora como dependencia ni se distribuye su árbol de fuentes.

Este proyecto no está afiliado a los titulares de Top Gear Overdrive. El nombre
del juego identifica la compatibilidad del port; sus datos no se distribuyen aquí.
