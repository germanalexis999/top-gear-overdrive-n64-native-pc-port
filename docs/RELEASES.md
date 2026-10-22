# Fuentes y descargas separadas

`main` conserva las fuentes y herramientas. Los paquetes Windows se publican
como adjuntos de GitHub Releases, sin EXE, DLL ni ZIP en el historial Git.
Localmente se usa una carpeta hermana `../release-packages/`.

## Convención del proyecto

Por indicación del usuario (2026-10-22), cada push solicitado al agente incluye
la publicación de una prerelease con ZIP Windows y SHA-256 correspondientes al
mismo commit. No hace falta pedir la release por separado. Se conserva explícito
qué correcciones están pendientes de validar. Esta convención está en AGENTS.md.
Un git push ejecutado directamente fuera del flujo de release no activa por sí
solo una compilación remota: no se ha instalado un hook ni un workflow de CI.

## Flujo con un solo script

La publicación se inicia manualmente; un `git push` por sí solo no compila ni
publica binarios. `tools/release_windows.ps1` automatiza el proceso local cuando
se ejecuta. Necesitas las herramientas de [BUILDING.md](BUILDING.md), GCC 16.2.0,
una ROM propia USA v0 y los cambios de fuentes ya guardados en un commit.

Desde la raíz del repositorio, para generar y probar el ZIP sin subirlo:

```powershell
.\tools\release_windows.ps1 -Version v0.1.0-preview.3 -RomPath 'D:\ROMs\mi-copia.z64'
```

El script normaliza una copia de la ROM en `build/release-input/`, prepara las
dependencias fijadas, compila N64Recomp, regenera el C local, compila el EXE,
ejecuta las pruebas, empaqueta y comprueba el ZIP extraído. No modifica la ROM
original ni añade archivos automáticamente a Git.

Para hacerlo todo y publicar también en GitHub:

```powershell
.\tools\release_windows.ps1 -Version v0.1.0-preview.3 -RomPath 'D:\ROMs\mi-copia.z64' -Publish
```

`-Publish` sube la rama actual con `git push origin HEAD`, crea una release en
borrador, adjunta ZIP y SHA-256, comprueba tamaños/hashes en GitHub y publica una
**prerelease**. Usa las credenciales Git existentes; si todavía no las tienes,
autentica primero un `git push` normal con tu cuenta y acceso al repositorio.

Si el ZIP ya se creó, no se sobrescribe. Para verificarlo y continuar la subida:

```powershell
.\tools\release_windows.ps1 -Version v0.1.0-preview.3 -RomPath 'D:\ROMs\mi-copia.z64' -Resume -Publish
```

`-Resume` exige que el ZIP corresponda al commit actual limpio. Si cambiaste las
fuentes, crea una versión nueva. Las versiones ya publicadas no se reemplazan.
Para otros lanzamientos cambia `-Version`, por ejemplo a `v0.1.0-preview.4`.

Opciones adicionales: `-PythonExecutable 'D:\Python\python.exe'` si Python no
está en PATH, `-DependencyRoot 'D:\dependencias-del-port'` para reutilizar un
checkout local compatible, y `-Jobs 4` para el paralelismo de compilación.
El proceso se detiene ante cualquier error; las pruebas son obligatorias.

## De dónde sale el ZIP

Se construye localmente desde las fuentes del commit indicado en
`SOURCE.md` y `PACKAGE-MANIFEST.json`. `build/tgod_native.exe` es el binario
compilado; las DLL se obtienen del mismo toolchain MinGW y se seleccionan
siguiendo los imports PE. El ZIP no se descarga de otro port ni se forma
comprimiendo toda la carpeta de trabajo. Quien descarga el ZIP no necesita
compiladores: solo extraerlo y aportar su ROM compatible.

## Herramientas individuales

Después de compilar, probar y hacer commit de las fuentes, también puedes
ejecutar cada paso por separado:

```powershell
python tools/package_windows.py --version v0.1.0-preview.3 --output-dir ../release-packages
python tools/package_windows.py --audit ../release-packages/top-gear-overdrive-v0.1.0-preview.3-windows-x64.zip
python tools/test_windows_package.py ../release-packages/top-gear-overdrive-v0.1.0-preview.3-windows-x64.zip --rom 'D:\ROMs\mi-copia.z64'
python tools/publish_release.py ../release-packages/top-gear-overdrive-v0.1.0-preview.3-windows-x64.zip
```

La última orden solo prepara y valida. Añadir `--publish` publica la release en
el repositorio configurado. Usa la autenticación existente del gestor de
credenciales de Git, sin guardar ni imprimir tokens. Primero sube los adjuntos
a un borrador y verifica sus hashes; después publica una versión preliminar.

Los paquetes se construyen desde una lista permitida, no copiando carpetas
completas de trabajo. El empaquetador analiza los imports PE del EXE y de cada
DLL, incluye las tres dependencias MinGW de la compilación x64 y excluye las DLL
del sistema Windows. Incluye las licencias upstream, las fuentes correspondientes
por revisión y los parches del port. No se empaquetan ROMs ni archivos generados
durante las pruebas del juego. Una carpeta de paquete existente no se sobrescribe.

Las pruebas de la distribución se realizan en una extracción temporal, con el
PATH reducido al sistema Windows, para comprobar que no depende del compilador
instalado. Esa extracción puede crear capturas/guardados; nunca se vuelve a
empaquetar. El ZIP publicado permanece intacto.
