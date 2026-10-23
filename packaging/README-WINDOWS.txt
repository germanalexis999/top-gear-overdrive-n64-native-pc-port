Top Gear Overdrive - Native PC Port (experimental)

Windows 10/11 de 64 bits.

1. Extrae TODO el ZIP a una carpeta donde puedas guardar archivos.
2. Abre Jugar.cmd o tgod_native.exe.
3. Selecciona tu propia ROM de Top Gear Overdrive USA v0.

Se aceptan .z64, .v64 y .n64. El programa comprueba la revision y convierte
el orden de bytes en memoria, sin modificar tu ROM. Tambien puedes arrastrar
la ROM sobre el EXE. No necesitas instalar compiladores ni descargar DLL.

La ROM se lee localmente: no se envia ni se sube a ningun sitio.
Este paquete no incluye ROM, musica, texturas, coches ni circuitos extraidos.
El ejecutable contiene la logica recompilada; los datos se cargan de tu ROM.

No borres las DLL que acompanian al EXE. Los guardados se crean en saves/.
Los programas y DLL del paquete no tienen firma Authenticode.

Controles:
  Enter = Start; X/Espacio = A; Z/Retroceso = B; C = gatillo Z
  A/S = L/R; flechas = direccion; Escape = salir
  F1-F4 = ventana; F5 = filtro; F6 = resolucion interna
  F11 o Alt+Enter = pantalla completa
  Tambien se detectan mandos XInput.

F6 alterna nativo/720p/1080p/4K y guarda RenderHeight=0/720/1080/2160 en la seccion
[Graphics] de tgod.ini junto al EXE. El formato sigue siendo 4:3: 1080p dibuja
1440x1080, con barras laterales en un monitor 1920x1080.
HD usa la GPU mediante Direct3D 11 (hardware con feature level 11.0).
Se dibuja en un hilo separado; el juego conserva el avance nativo. Si la GPU
no esta disponible se informa en consola y se usa CPU, que es lenta en HD.
No hace falta instalar un compilador de shaders. Nativo sigue como valor inicial.
HD presenta directamente con DXGI y reutiliza recursos de GPU. Si falla la
presentacion, vuelve a la ruta de lectura/GDI. Se mantienen los efectos que
necesitan leer el framebuffer y la sincronizacion nativa del juego.
HD sirve para comparar contornos y detalle. Las texturas son las
originales. F1-F4/F5 solo cambian la ventana y el filtro de presentacion.

Expansion Pak: el juego ya recibe 8 MiB. En tgod.ini, seccion [Graphics],
OriginalResolution=game deja la seleccion original (valor inicial).
Puedes elegir off, half o full, o iniciar con --original-resolution full.
La preferencia se aplica una vez al arrancar; luego Setup sigue disponible.
F6 elige HD por separado. Half conserva su area reducida con bandas.
Preview.10: buen funcionamiento general confirmado por el usuario;
comparacion exhaustiva de modos y multijugador pendiente.

Esta version recupera numeros del HUD y el titulo del showroom, y corrige
mezcla, profundidad y filtrado de particulas. HUD y humo confirmados como
corregidos en las pruebas del usuario. Sigue la revision de fidelidad RDP/VI
y de otros circuitos y modos. No es una version final.
Incluye dibujo de lineas L3DEX2 y cambios de microcodigo. El antialias y los
extremos de esas lineas todavia son aproximados.

Codigo, instrucciones y versiones:
https://github.com/germanalexis999/top-gear-overdrive-n64-native-pc-port

Consulta LICENSE, LICENSES/, THIRD_PARTY_NOTICES.md y SOURCE.md.
PACKAGE-MANIFEST.json enumera los archivos, dependencias y sus hashes SHA-256.

4K conserva el formato 4:3: renderiza 2880×2160 y muestra barras laterales en una pantalla 3840×2160. Se selecciona con F6, RenderHeight=2160 o --render-height 4k. Requiere más trabajo de GPU que 1080p.

Mandos: hasta cuatro gamepads/joysticks mediante SDL3, incluidos Xbox y genéricos
que SDL pueda abrir. El teclado comparte jugador 1 por defecto, sin consumir un
puerto extra. F7 permite reasignar dispositivos por su nombre. Un joystick sin
mapping estándar aparece como «configurar»: asigna manualmente botones, ejes y
crucetas. Se conservan los puertos de los otros jugadores al desconectar un mando.
F11 o Alt+Enter alterna pantalla completa sin bordes; mantener la tecla pulsada
no vuelve a alternar. La resolución interna se cambia por separado con F6.


Mapper de mandos: F7 abre Controles. Elige jugador 1–4 y mando 1–4;
si el mando ya estaba asignado, se intercambia con el del otro jugador. El teclado
puede compartirse con un solo jugador. Selecciona Mando o Teclado, una acción y
una de sus tres asignaciones; suelta los controles y pulsa el botón/tecla o mueve
el eje. Escape cancela la captura. Hay zona muerta (0–50%) e inversión de ejes.
Guardar y cerrar aplica y escribe controls.json junto al EXE (con respaldo .bak);
Cancelar descarta los cambios. Mientras el panel está abierto el juego recibe
controles neutros; al cerrar, suelta los controles para volver a jugar.

Esta revisión local integra SDL3 3.4.18 y requiere SDL3.dll junto al EXE.
Los perfiles anteriores de controls.json se cargan; al guardar se usa versión 2
(con respaldo). Revisa las asignaciones en F7 después del cambio desde XInput.
Para joysticks sin mapping estándar, deja los ejes en reposo antes de capturar;
se recuerda ese centro para poder asignar también gatillos con reposo desplazado.
Se admiten hasta 16 ejes, 64 botones y 4 crucetas por joystick. Opcionalmente se
puede aportar un gamecontrollerdb.txt local; el ZIP no incorpora una base externa.
La identidad entre sesiones y mandos idénticos sigue pendiente de mejorar.
