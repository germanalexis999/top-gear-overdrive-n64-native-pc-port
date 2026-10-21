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

F6 alterna nativo/720p/1080p y guarda RenderHeight=0/720/1080 en la seccion
[Graphics] de tgod.ini junto al EXE. El formato sigue siendo 4:3: 1080p dibuja
1440x1080, con barras laterales en un monitor 1920x1080.
HD es experimental y lento con el renderer por CPU actual. Se dibuja en un
hilo separado; el juego conserva el avance nativo. La imagen HD puede tener
pocos FPS y retraso. Usa nativo para jugar con la menor latencia.
HD sirve para comparar contornos y detalle. Las texturas son las
originales. F1-F4/F5 solo cambian la ventana y el filtro de presentacion.

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
