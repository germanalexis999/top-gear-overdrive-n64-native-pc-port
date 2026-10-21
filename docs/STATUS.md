# Estado del port — 2026-10-21

Última revisión de fuentes: showroom recuperado al interpretar VI entrelazado
(stride 1280, imagen 640), y coche con color/detalles tras corregir alfa I4/I8.
También se añadieron TEXEL1 independiente, mipmaps/fracciones LOD y muestreo
común para rectángulos. El usuario confirma mejora musical; el foco actual es
gráficos. Quedan defectos en textos/HUD y fidelidad RDP/VI. Evidencia y límites en
[DIAGNOSTICS-2026-10-21.md](DIAGNOSTICS-2026-10-21.md).

## Distribución Windows preliminar

Fuentes en `main`; binarios separados en GitHub Releases y localmente en
`../release-packages/`. El paquete x64 contiene el EXE, tres DLL MinGW, launcher,
instrucciones, GPLv3/avisos upstream, fuentes por revisión y manifiesto SHA-256.
No contiene ROM ni datos extraídos. El selector carga una ROM local y verifica
USA v0; `.z64`, `.v64` y `.n64` se normalizan solo en memoria.
El paquete preview.3 incorpora estas correcciones gráficas; preview.2 es anterior.
`tools/release_windows.ps1` permite repetir compilación, pruebas y empaquetado,
y publicar opcionalmente con `-Publish`. No hay publicación automática por push.

La extracción temporal del ZIP pasó con PATH limitado a Windows: inicio sin
compilador instalado en PATH, rutas Unicode/con espacios, tres órdenes de bytes,
rechazo de ROM truncada/modificada, 16 s de carrera, EEPROM y audio sin descartes.
Las capturas, ROMs de prueba y guardados quedaron en la extracción privada y no
se incorporaron al ZIP. Los glitches de fidelidad siguientes siguen abiertos.

Port experimental para Windows x64. El ejecutable arranca, recorre los menús y
entra a carrera con una ROM USA v0 local. No se declara paridad completa con N64.

## Correcciones verificadas

- Audio: FULL usa el bit 31 de AI con extensión de signo a los registros de
  64 bits. Los eventos AI se entregan al retirar buffers de WaveOut.
- Scheduler: si todos los hilos esperan hardware, entrega eventos externos hasta
  despertar un hilo. Se corrigió la eliminación de nodos de las listas de hilos
  y la reinserción tras cambiar prioridades.
- Render: `G_MOVEMEM` lee el identificador en el byte bajo, evitando interpretar
  una carga de luces como viewport. Se corrigieron PUSH de F3DEX2, el avance DXT
  después de cada palabra y la lectura de las filas impares de TMEM.
- Materiales: TEXEL1 separado, niveles LOD limitados por G_TEXTURE y alfa de
  intensidad. Rectángulos 2D comparten el muestreador y respetan filas impares.
- Presentación: modo VI entrelazado del showroom, tamaño RGB dinámico,
  sincronización del repintado y capturas de cuadros presentados.
- Generación: las esperas cooperativas se aplican desde un script versionado.
  EEPROM read/write se identifican mediante símbolos; no requieren editar el C
  generado ni cambiar la función de lectura de mandos del runtime.

## Evidencia local

Después de regenerar desde la ROM y compilar: prueba automatizada de 16 segundos,
Modo 2 activo hasta el final, 4/4 catálogos verificados, EEPROM de 2048 bytes.
Audio: 548 bloques encolados, 546 completados y 2 en vuelo; 468 bloques con señal
enviados al dispositivo. Cero overruns, descartes silenciosos o frames reemplazados.
Las capturas locales muestran carretera, montañas, cielo y vehículo; los assets,
capturas y el PCM de diagnóstico no se incluyen en el repositorio.

La copia pública también se regeneró y compiló desde sus fuentes. Una ejecución
de 16 s de ese binario terminó con código 0, carrera activa, 4/4 catálogos y
EEPROM verificada: 549 bloques de audio, 547 completados, 2 en vuelo, 470 con
señal y cero descartes/reemplazos.

`tgod_regression_checks` verifica, sin ROM, el dibujo de dos filas de textura,
PUSH/POP de matrices, aislamiento del viewport respecto a cargas de luces y
eliminación de nodos intermedios/finales/ausentes en colas de hilos.

## Pendientes

- Textos superiores del showroom, HUD, transparencias y reflejos restantes.
- Combiner/blender, profundidad/coverage, filtrado y campos VI con precisión N64.
- Escucha/comparación prolongada de música/efectos; prioridad actual: gráficos.
- Cobertura de comandos RSP y formatos fuera de las escenas comprobadas.
- Circuitos, modalidades, guardados y estabilidad fuera de la ruta automatizada.

Los contadores de cobertura de pantalla y de catálogos son diagnósticos parciales;
no demuestran por sí solos exactitud gráfica ni integridad de todos los assets.
