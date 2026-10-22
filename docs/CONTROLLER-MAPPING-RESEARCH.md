# Investigación: mapper de mandos para Top Gear Overdrive

Fecha: 2026-10-21. Comparación de fuentes locales y documentación oficial.
El primer panel XInput descrito al final de este documento ya esta implementado
en la revision local. SDL3 y la identificacion persistente de unidades siguen pendientes.
No se ejecutaron los otros juegos ni pruebas nuevas durante esta investigación.

## Resultado

La mejor referencia local es **RaceWave46/RecompFrontend**: distingue dispositivos,
asignación a jugadores, perfiles y captura de botones. Conviene adoptar esa
separación en nuestro host. Para ofrecer compatibilidad más amplia con mandos,
recomiendo SDL3 exclusivamente como backend de entrada y un panel propio Win32.
Es una propuesta de implementación nueva: los ejemplos revisados usan SDL2.
No hace falta RT64 para mapear mandos. Integrar RecompFrontend completo sí requiere
resolver su interfaz gráfica, que actualmente dibuja mediante RT64/plume.

## Qué hacen los ejemplos

| Referencia local | Lo observado | Qué conviene adoptar |
| --- | --- | --- |
| MarioKart64Recomp, HEAD 3f72d23 | SDL2, campos de entrada tipados, dos bindings por acción, captura de botón/eje/tecla, restauración y controls.json. La copia revisada anuncia solamente puerto 0 y agrega entradas de los mandos conectados. | Modelo de bindings y editor; su ruta de entrada no sirve directamente para cuatro jugadores independientes. |
| RaceWave46, HEAD 5c34f37, copia incluida de RecompFrontend | Perfiles de controles, selección por jugador, identificación de dispositivos con GUID/datos de modelo y serie, captura filtrada por mando asignado, JSON con versión. Tiene selección temporal y confirmación/cancelación de asignaciones. | Referencia principal para separar perfil/dispositivo/jugador y evitar que un mando configure al otro. |
| wave-race-64-recomp, HEAD b7c677a | Con frontend, ambos jugadores se leen mediante profiles::get_n64_input; el callback rechaza puertos fuera de 0–1. Mientras el menú captura entrada, entrega controles neutros al juego. | Aplicar los bindings en el callback real; bloquear la entrada de juego mientras se configura. El límite de dos puertos pertenece a ese port. |

Son observaciones de estas copias, no afirmaciones sobre todas las versiones
publicadas de esos proyectos. Los árboles locales pueden contener cambios propios.

### Evidencia local

Rutas relativas a la carpeta de investigación, fuera del árbol público del port:

- ejemplos/MarioKart64Recomp/include/recomp_input.h:
  InputField, GameInput, bindings_per_input=2 y funciones de captura.
- ejemplos/MarioKart64Recomp/src/game/input.cpp:
  get_connected_device_info solo anuncia controller_num=0;
  controller_button_state combina el estado de cur_controllers.
- ejemplos/MarioKart64Recomp/src/game/config.cpp:
  save_controls_config/load_controls_config y restauración de bindings.
- ejemplos/RaceWave46/lib/RecompFrontend/recompinput/include/recompinput/:
  profiles.h, players.h e input_binding.h separan los contratos.
- ejemplos/RaceWave46/lib/RecompFrontend/recompinput/src/:
  profiles.cpp guarda perfiles/asociaciones; players.cpp mantiene asignación
  temporal y definitiva; input_binding.cpp filtra el dispositivo de captura.
- ejemplos/RaceWave46/lib/RecompFrontend/recompui/src/config/ui_config_page_controls.cpp:
  botones de asignar/limpiar/restablecer por acción.
- ejemplos/RaceWave46/lib/RecompFrontend/CMakeLists.txt exige ruta de RT64;
  recompinput/CMakeLists.txt también incluye cabeceras de recompui/RmlUi.
  En esta copia no es una biblioteca de entrada completamente desacoplada.
- ejemplos/wave-race-64-recomp/src/callbacks.cpp:
  get_input aplica profiles::get_n64_input y evita entrada al juego durante el menú.

## Dos mappings diferentes

1. **Dispositivo físico → mando normalizado**: identifica qué botón/eje físico
   representa cada control estándar. SDL permite cargar mappings para dispositivos
   desconocidos. Hace falta para algunos genéricos que no exponen XInput.
2. **Mando normalizado → acciones de Top Gear**: decide qué botón acelera, frena,
   cambia cámara o maneja la dirección. Este es el mapper que ve el jugador.

Cambiar el segundo nivel sobre nuestro XInput actual permitiría remapear los
mandos Xbox y genéricos en ese modo, pero no incorporaría dispositivos que XInput
no detecta. Una pantalla de configuración por sí sola no amplía esa compatibilidad.
Tampoco SDL garantiza reconocer cualquier dispositivo sin un mapping adecuado.

## Diseño propuesto

Flujo: dispositivo → normalización → perfil → jugador 1–4 → snapshot N64 → SI/callbacks.

- Un mismo snapshot debe alimentar el hook SI y los callbacks del runtime.
  No debe quedar una ruta con botones fijos que ignore los perfiles guardados.
- Cada jugador elige dispositivo y perfil. El teclado puede compartir P1 como hoy,
  o asignarse explícitamente, sin ocupar automáticamente otro jugador.
- Perfiles con nombre, hasta dos bindings por acción, ejes positivos/negativos,
  gatillos, cruceta, zona muerta e inversión de ejes.
- Mantener los controles actuales como perfil inicial. Etiquetas como
  «Acelerar (N64 A)», «Frenar (N64 B)», «Freno de mano (N64 Z)» y «Dirección».
- Guardar controls.json junto a tgod.ini, con versión, validación de rangos,
  recuperación de valores por defecto y escritura mediante reemplazo atómico.
- Conservar la asignación durante una desconexión; limpiar los botones pulsados
  y permitir reconectar o reasignar sin mover a los demás jugadores.
- No usar un índice temporal como identidad persistente. GUID/modelo sirven para
  elegir perfiles; añadir serie si está disponible para distinguir unidades.
  Si dos mandos idénticos no ofrecen serie, ofrecer «pulsa Start para asignar»:
  no prometer distinguirlos automáticamente entre sesiones.

### Panel de controles

Propuesta: **F7 → Controles**, con pestañas Jugador 1–4.

- Dispositivo y estado conectado/desconectado.
- Perfil, lista de acciones y botones «Asignar», «Borrar», «Restaurar».
- Captura «pulsa un botón o mueve un eje», Escape para cancelar y umbral de
  activación de ejes para que el drift no complete una asignación accidental.
- Capturar solo el mando del jugador seleccionado; para asignar dispositivo,
  entrar en un modo separado que acepta la pulsación de cualquier mando libre.
- Vista de botones y ejes en vivo, con zona muerta e inversión configurables.
- Aplicar/Cancelar: editar una copia temporal para no cambiar la partida al cancelar.
- Mientras el panel o la captura tenga el foco, enviar entrada de juego neutra.
  Esperar a soltar los controles antes de devolver entrada para que el botón de
  cerrar/asignar no acelere o pause el juego inmediatamente.
- Crear el panel en el hilo de interfaz, conservando la ventana DXGI del juego.
  Un solo propietario procesa eventos; el worker gráfico recibe snapshots.

## Orden de implementación recomendado

1. **Perfiles y editor sobre XInput**: separar el mapeo fijo actual de la lectura
   del dispositivo; implementar controles.json, asignación 1–4 y panel F7.
   Esta primera entrega cubre los dos mandos descritos si ambos exponen XInput.
2. **Backend SDL3**: incorporar dispositivos y mappings más amplios, manteniendo
   los perfiles de acciones. Fijar versión/dependencias e incluir licencia y DLL
   necesarias en el paquete. Al usar SDL como backend, evitar sondear también el
   mismo dispositivo con XInput y duplicarlo en dos puertos.
3. **Identidad y calibración**: perfiles por modelo/unidad cuando sea posible,
   configuración de genéricos no reconocidos y casos de reconexión/mandos idénticos.

Esta secuencia reduce el alcance de cada cambio. Si la prioridad pasa a ser un
mando que XInput no reconoce, conviene adelantar el paso 2.

## Comprobaciones previstas para la implementación

- Dos y cuatro jugadores con perfiles diferentes, sin duplicación de dispositivos.
- Dos unidades del mismo modelo y combinación de mando Xbox/genérico.
- Configurar P2 con P1 activo: P1 no completa la captura de P2.
- Desconectar/reconectar un mando con el acelerador pulsado.
- Guardar/cargar, cancelar, restablecer y recuperar JSON inválido.
- Ejes con drift, inversión, gatillos analógicos y teclado compartido con P1.
- Abrir/cerrar panel en ventana y F11, conservando controles y presentación.
- Ningún botón capturado llega al juego; un solo mapeo alimenta SI y callbacks.
- Medir sondeo y mantener los eventos nativos y el audio fuera de esperas del editor.

## Fuentes oficiales consultadas

- [N64Recomp/RecompFrontend](https://github.com/N64Recomp/RecompFrontend):
  módulos recompinput/recompui, perfiles multijugador y render de UI mediante RT64.
- [SDL3: SDL_AddGamepadMapping](https://wiki.libsdl.org/SDL3/SDL_AddGamepadMapping):
  mapping físico, GUID, botones, hats y ejes.
- [SDL3: SDL_AddGamepadMappingsFromFile](https://wiki.libsdl.org/SDL3/SDL_AddGamepadMappingsFromFile):
  carga de mappings externos.
- [SDL3: SDL_GetGamepadSerial](https://wiki.libsdl.org/SDL3/SDL_GetGamepadSerial):
  el número de serie puede no estar disponible.

No se copió código ni se incorporaron nuevas dependencias de terceros.


## Primera implementación local: panel F7 sobre XInput

Estado: código implementado y compilado; pendiente de prueba interactiva con mandos.
No se ejecutaron pruebas nuevas para esta entrega.

- F7 abre un panel Win32 con selección de jugador, dispositivo XInput 1–4 o ninguno,
  teclado opcional, nombre de perfil, zona muerta 0–50%, inversión X/Y y vista en vivo.
- Tres bindings por acción, separados entre mando y teclado. Se eligieron tres
  para conservar todos los atajos iniciales (por ejemplo X/Espacio/W para acelerar).
- Captura por mando seleccionado, botones, gatillos y sentido de ejes; espera
  liberación inicial, umbral de captura para evitar drift y cancelación/timeout.
- Guardar aplica los cuatro perfiles; Cancelar descarta la copia temporal.
  Restaurar recupera los bindings/ajustes del perfil del jugador seleccionado.
- Si se selecciona un mando ya asignado, se intercambian los puertos. Solo un
  jugador puede compartir teclado: activarlo en otro jugador transfiere ese teclado.
- controls.json de versión 1 junto al EXE; respaldo controls.json.bak, archivo
  temporal y reemplazo atómico. Un archivo inválido se conserva y se usan defaults.
- El mismo snapshot mapeado alimenta SI y callbacks; ambos respetan la presencia
  real según la asignación. F7/edición bloquea la entrada al juego, y al cerrar
  se espera a soltar botones/teclas antes de reactivarla.
- El teclado solo se lee con el foco en el juego o en el panel de controles.
- Los slots XInput se conservan durante la sesión, pero pueden cambiar entre
  sesiones por decisión de Windows. No se identifica un mando por modelo/serie.

### Pendientes explícitos

1. **SDL3 para genéricos que no exponen XInput**: no incorporado. Se conserva la
   ruta Xbox/XInput y los genéricos deben usar ese modo.
2. Identidad persistente por modelo/serie y biblioteca de perfiles reutilizables
   entre dispositivos: el panel actual guarda un perfil con nombre por jugador.
3. Captura interactiva y validación con dos/cuatro mandos físicos; reconexión,
   fullscreen, cancelación y recuperación de configuración. Esta compilación no
   implica que esas pruebas manuales ya se hayan realizado.

## Confirmación del usuario — 2026-10-22

El usuario confirmó que el mapper funciona y autorizó publicar preview.8.
Esto no confirma una carrera completa con cuatro mandos físicos. SDL3 para
genéricos sin XInput y la identidad persistente de dispositivos siguen pendientes.


## Actualización local SDL3 — 2026-10-22

SDL3 3.4.18 ya está incorporado (829a65d769d935c4852f8159e964312c0957260a).
Se eliminó el backend XInput paralelo: SDL administra hasta cuatro dispositivos.
Gamepads reconocidos mantienen las acciones normalizadas; joysticks sin mapping
pueden capturar botones físicos, ejes con centro en reposo y crucetas. El centro
por binding permite manejar gatillos cuyo reposo no sea cero. Perfiles v1 se
cargan; el nuevo guardado v2 agrega ese centro y tipos raw. No se descartan los
perfiles anteriores al arrancar, ni se obliga a sobrescribirlos.

La UI conserva Win32 y los render/audio backends existentes. SDL_Init/SetMainReady,
PumpEvents, UpdateJoysticks/Gamepads y los handles son propiedad del hilo principal.
No se mantiene el mutex de snapshots mientras SDL consulta dispositivos. Se
conserva la asignación de los demás puertos al desconectar; se intenta recuperar
una conexión de la sesión por GUID/serie/ruta. Dos unidades sin identidad única
siguen siendo ambiguas, y no se persiste aún identidad entre sesiones.

Se puede cargar un gamecontrollerdb.txt aportado localmente; no se copia una base
de mappings de terceros al repositorio ni al ZIP. Persistencia de identidad,
biblioteca de perfiles y pruebas físicas con varios genéricos siguen pendientes.
Compilación correcta; no se realizaron pruebas nuevas de ejecución en esta entrega.

Referencias oficiales: SDL_PumpEvents (solo hilo principal), SDL_SetJoystickEventsEnabled
(sondeo manual si se desactivan eventos) y SDL_UpdateJoysticks:
https://wiki.libsdl.org/SDL3/SDL_PumpEvents
https://wiki.libsdl.org/SDL3/SDL_SetJoystickEventsEnabled
https://wiki.libsdl.org/SDL3/SDL_UpdateJoysticks
