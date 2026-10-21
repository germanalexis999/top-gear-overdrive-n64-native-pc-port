# Fuentes y descargas separadas

`main` conserva las fuentes y herramientas. Los paquetes Windows se publican
como adjuntos de GitHub Releases, sin EXE, DLL ni ZIP en el historial Git.
Localmente se usa una carpeta hermana `../release-packages/`.

Después de compilar, probar y hacer commit de las fuentes:

```powershell
python tools/package_windows.py --version v0.1.0-preview.1 --output-dir ../release-packages
python tools/package_windows.py --audit ../release-packages/top-gear-overdrive-v0.1.0-preview.1-windows-x64.zip
python tools/test_windows_package.py ../release-packages/top-gear-overdrive-v0.1.0-preview.1-windows-x64.zip --rom 'C:\ruta\mi-ROM.z64'
python tools/publish_release.py ../release-packages/top-gear-overdrive-v0.1.0-preview.1-windows-x64.zip
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
