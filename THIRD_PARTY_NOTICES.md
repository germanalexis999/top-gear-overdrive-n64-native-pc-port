# Dependencias y referencias

Este repositorio distribuye el host del port, metadatos de configuración,
herramientas propias y parches locales. No incluye árboles de código de terceros,
ROMs, assets, desensamblados, C generado del juego, bibliotecas ni ejecutables.

Las dependencias se descargan por separado desde sus repositorios originales:

| Proyecto | Función | Licencia upstream |
| --- | --- | --- |
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) | Runtime y APIs de N64 | GPL versión 3; ver `COPYING` en el checkout |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Generación local de C y ABI | MIT, copyright Wiseguy; ver `LICENSE` |

Las revisiones se fijan en `dependencies.lock.json`. Sus subdependencias conservan
sus licencias upstream. Los parches locales modifican esos proyectos y conservan
las licencias de los archivos afectados; no sustituyen los avisos originales.
Los textos de licencia pertinentes se conservan en `LICENSES/`.

El trabajo sobre semántica de TMEM, matrices y decodificación de comandos se
contrastó con [RT64](https://github.com/rt64/rt64), MIT, copyright RT64 Contributors.
Se conserva su aviso MIT por las adaptaciones y referencias del rasterizador.
RT64 no se incorpora como dependencia ni se distribuye su árbol de fuentes.

Este proyecto no está afiliado a los titulares de Top Gear Overdrive. El nombre
del juego identifica la compatibilidad del port; sus datos no se distribuyen aquí.
