# Expansion Pak y resolución original — 2026-10-23

## Conclusión

El port ya proporciona el equivalente al Expansion Pak: reserva 8 MiB de RDRAM
y escribe ese tamaño en `osMemSize`, dirección `0x80000318`. No hace falta
activar más memoria ni distribuir datos adicionales para que el juego la detecte.

La selección gráfica original es independiente de F6 / `render_height` del
host. Podemos establecer un valor original por defecto mediante un parche
controlado, pero no conviene forzarlo todavía: hay diferencias de viewport,
proyección y presentación que nuestro escalado HD debe tratar explícitamente.
La implementación posterior descrita abajo está compilada; no confirma todavía
resultados en carrera. Se mantiene `game` como valor predeterminado.

## Evidencia del juego USA v0

Se inspeccionaron las funciones generadas localmente a partir de la ROM del
usuario. Se publican direcciones y conclusiones; no sus cuerpos ni datos.

- `0x80102344` almacena la opción gráfica. La ruta de menú en
  `0x8001E19C–0x8001E1F0` recorre tres valores si `osMemSize > 0x7FFFFF`;
  con menos memoria recorre dos. Por tanto, el tercer ajuste requiere 8 MiB.
- `func_80006B3C`, desde `0x80006B44`, comprueba de nuevo la memoria y elimina
  el valor 2 si no hay suficiente. Además deriva un modo efectivo según el
  estado del juego y otros campos; no basta con escribir un tamaño de buffer.
- En la rama de visualización 1, `0x80006DBC–0x80006DE8` selecciona ancho
  320 cuando el modo efectivo es 0, y 640 en los otros casos; alto 240.
- Una rama adicional para el modo efectivo 1,
  `0x80006EAC–0x80006F38`, fija 640×120 y cambia parámetros de proyección
  y las tablas de vídeo/viewport. Es coherente con la opción denominada
  «Half» en fuentes comunitarias: no debe confundirse con 640×480 ni con HD.
- El valor 2 corresponde por exclusión al modo completo, normalmente descrito
  como «Full»: 640×240 en esa rama, sujeto a las condiciones originales.
  Los nombres Off/Half/Full no se han decodificado todavía de las etiquetas
  del menú USA; la relación numérica/dimensional sí procede del código.
- La rama 0 tiene 640×480 sin depender de esa opción. Encontrar 480 líneas
  en menús/intro no demuestra un modo secreto de carrera a 480i.
- `0x80054A10` guarda un modo derivado (0, 1 o 2). Sus referencias cambian
  coordenadas y comandos gráficos, por ejemplo en `0x80002410` y
  `0x80002E00`. Queda por completar su seguimiento antes de afirmar que se
  conocen todos los efectos de la opción.
- La inicialización escribe cero en `0x80102344` en `0x8002C9F0`.
  Un futuro valor por defecto debe aplicarse después de inicialización/carga
  de preferencias y permitir al usuario volver a los otros modos.
- `func_800494B0`, pese a recibir argumentos de esta rutina, solo guarda
  registros en la pila y retorna. No es el configurador VI; no debe usarse
  como punto de parche por su firma aparente.

### Qué mejoras están comprobadas

Resolución horizontal, altura del área de dibujo en el modo parcial,
tablas de vídeo/viewport y parámetros de proyección. No se ha encontrado
evidencia suficiente para atribuir al Expansion Pak nuevas texturas, modelos,
iluminación, partículas, música, pistas o jugadores adicionales en este juego.
Esto no prueba que no haya otras diferencias: requiere completar el flujo
indirecto del modo derivado y comparar escenas equivalentes.

No hay evidencia verificada de una cuarta opción «Ultra» en esta revisión.
Las listas de accesorios agrupan muchos juegos y algunos resultados de búsqueda
mezclan sus filas. No aplicar contraseñas de otros juegos a Top Gear Overdrive.
El manual australiano consultado describe Setup en páginas impresas 19–22,
pero no documenta ahí estos modos; no resuelve las etiquetas de la ROM USA.

## Nuestro renderer y el punto pendiente

`src/main.cpp` ya reserva 8 MiB y anuncia `osMemSize = 8 MiB`.
El HD del host mantiene los datos y eventos nativos y dibuja otra versión sobre
superficies privadas. Aumentar la resolución con F6 no selecciona el modo
original de Expansion Pak.

En la revisión publicada preview.9, `include/render_surface.hpp`,
`RenderSurfaces::acquire`, calcula la escala
como `render_height / (width == 320 ? 240 : 480)`. Esto distingue el ancho,
pero no separa 640×120, 640×240 y 640×480. El backend GPU usa las dimensiones
devueltas por esa misma ruta (`src/gpu_raster.cpp`).

Como ejemplo aritmético, una superficie de 640×240 a altura solicitada 1080
recibe escala 2,25 y queda en 1440×540. La presentación puede estirarla, pero
no equivale a 1080 muestras verticales. Es una limitación inferida del código;
no se ejecutó una comparación visual de esa combinación en esta entrega.
Usar solo la altura tampoco basta: deben conservarse el aspecto previsto por
VI, el viewport parcial, márgenes, readbacks y la proyección original.

## Cómo lo manejan otros proyectos

1. **N64ModernRuntime:** su inicio común escribe directamente 8 MiB en
   `osMemSize` (`librecomp/src/recomp.cpp`, revisión
   `cdf5abbd5026fef5c364c676e4667c45e42b6863`). Nuestro host ya coincide con
   esa práctica aunque prepara el arranque por su propia ruta.
2. **RaceWave46, ejemplo local:** su copia del runtime hace lo mismo.
   Disponer de 8 MiB no obliga a cambiar las opciones internas de cada juego.
3. **MarioKart64Recomp, ejemplo local:** declara N64ModernRuntime y RT64 como
   dependencias; `src/ui/ui_config.cpp` maneja opciones del renderer como Auto,
   Original y Original2x. La mejora de resolución se controla en el renderer.
4. **Zelda64Recomp:** su documentación oficial atribuye las mejoras gráficas
   a RT64 y ofrece configuración gráfica propia. Es una separación útil entre
   la lógica original y la imagen mejorada. No hace falta adoptar RT64 para
   aplicar esa separación a nuestro backend Direct3D 11.
5. **wave-race-64-recomp, ejemplo local:** los comentarios de
   `src/callbacks.cpp` explican que el runtime anuncia 8 MiB incluso cuando el
   juego original de 4 MiB no consulta `osMemSize`. Es evidencia de que memoria
   anunciada y resolución del host son decisiones diferentes; no una receta
   para reutilizar memoria sobrante en Top Gear, que sí comprueba el tamaño.

## Recomendación y orden de trabajo

1. Mantener siempre los 8 MiB que ya anunciamos.
2. Conservar F6 / 720p / 1080p / 4K como control de calidad del host.
3. Identificar explícitamente los modos 320×240, 640×120, 640×240 y 640×480
   usando VI y viewport, y adaptar el escalado/presentación por modo.
4. Completar las referencias de `0x80054A10` y decodificar etiquetas del menú.
5. Después comparar Off/Half/Full en la misma escena y en multijugador,
   incluyendo HUD, intro, humo, readbacks y avance lógico. Pendiente de ejecución.
6. Solo entonces ofrecer el modo original como preferencia reversible. Si se
   demuestra una mejora adicional al HD del host, considerar Full por defecto;
   no escribirlo continuamente en RDRAM ni alterar temporización para activarlo.

## Implementación local posterior a la investigación

- El host captura ancho `0x80117D14` y alto `0x8011D55C` en cada tarea.
  El intérprete acepta solo combinaciones conocidas y conserva un fallback
  para targets cuyo ancho no coincide. No usa el scissor de HUD como altura.
- Las superficies conservan escala X y escala Y; triángulos, líneas,
  rectángulos, muestreo, derivadas LOD y readbacks usan su eje correspondiente.
  Full a 1080p genera 1440×1080, Half 1440×540; 640×480 mantiene escala uniforme.
- La rutina original configura Half con rango VI vertical `0x009D0187`,
  frente a `0x002501FF` del modo 640×240 (`0x800072E4–0x80007498`).
  Esto confirma que debe conservarse el área reducida en presentación.
  DXGI y GDI centran ese modo con aspecto 8:3 dentro de la ventana 4:3.
- CPU y GPU recrean un target si su dirección/ancho se reutilizan con otra
  altura. La identificación de escena incluye las dimensiones; la captura VI
  de la tarea debe tener altura compatible antes de reutilizar su scanout.
- `OriginalResolution=game|off|half|full` en `[Graphics]` y
  `--original-resolution` seleccionan una preferencia de arranque. El hook
  declarativo de `func_8002C930`, antes de `0x8002CBA0`, aplica la elección
  una vez tras los valores iniciales y las llamadas de carga. El callback no
  reconfigura VI ni escribe continuamente en el juego. `game` no escribe nada.
- La generación exige que haya exactamente un callback. Los archivos generados
  que no cambian conservan su fecha para permitir compilación incremental.
- Se generó de nuevo el código local y se compiló `tgod_native`, incluido el
  shader HLSL. Sin ejecutar pruebas nuevas o el juego: falta confirmar modos,
  transiciones de escena, HUD/humo y multijugador.

Confirmación posterior del usuario (2026-10-23): «funciona genial», con
autorización de push y publicación de preview.10. Se registra como confirmación
general; no especifica qué combinaciones o número de jugadores se probaron.
No se ejecutaron nuevas pruebas de juego por el agente durante la publicación.

No se ha completado todavía la decodificación de etiquetas ni el seguimiento
de todas las referencias indirectas de `0x80054A10`; no se atribuyen efectos
adicionales a la opción ni se fuerza Full por defecto.

### Relación posible con el tambaleo

Una guía de MrSpeedrun relata oscilaciones del coche a alta velocidad y con
resolución máxima. Es un testimonio comunitario, no una reproducción nuestra
ni prueba de causa: no permite concluir que el fallo actual sea del original.
Tampoco equipara el High Res interno con F6. Mantener abierta la investigación
de física/sincronización de `PHYSICS-INVESTIGATION-2026-10-22.md`.

## Fuentes y límites

- [Manual original AUS, escaneo, páginas impresas 19–22](https://nintendo64.pl/wp-content/uploads/Top-Gear-Overdrive-AUS.pdf).
  Revisado visualmente; región distinta de la ROM USA del port.
- [N64ModernRuntime, código de arranque fijado por revisión](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/recomp.cpp).
- [Zelda64Recomp, documentación del proyecto](https://github.com/Zelda64Recomp/Zelda64Recomp).
- [Guía de MrSpeedrun](https://www.speedrun.com/top_gear_overdrive/guides/74z8s).
  Consultado el texto indexado; apertura directa no disponible en la herramienta.
- Ejemplos locales y código propio indicados arriba, inspeccionados sin ejecutar
  juegos ni pruebas nuevas. No se incluyeron manual, ROM o código generado en
  el árbol público.
