[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $InputPath).Path
$destination = [System.IO.Path]::GetFullPath($OutputPath)
if ([string]::Equals($source, $destination, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'La salida debe ser un archivo distinto; el ROM fuente nunca se sobrescribe.'
}
if ([System.IO.Path]::GetExtension($destination) -ne '.z64') {
    throw 'La salida normalizada debe usar la extensión .z64.'
}
if ((Test-Path -LiteralPath $destination) -and -not $Force) {
    throw "El destino ya existe: $destination. Usa -Force para regenerarlo."
}

$bytes = [System.IO.File]::ReadAllBytes($source)
if ($bytes.Length -lt 0x1000) {
    throw "El archivo es demasiado pequeño para ser un ROM N64: $($bytes.Length) bytes."
}

$magic = ($bytes[0..3] | ForEach-Object { $_.ToString('X2') }) -join ''
$byteOrder = switch ($magic) {
    '80371240' { 'z64 (big-endian)' }
    '37804012' { 'v64 (bytes intercambiados por pares)' }
    '40123780' { 'n64 (bytes intercambiados por palabras)' }
    default { throw "Magic N64 no reconocido: $magic" }
}

$normalized = [byte[]]::new($bytes.Length)
switch ($magic) {
    '80371240' { [Array]::Copy($bytes, $normalized, $bytes.Length) }
    '37804012' {
        if (($bytes.Length % 2) -ne 0) { throw 'El tamaño no es múltiplo de 2; no se puede normalizar como v64.' }
        for ($i = 0; $i -lt $bytes.Length; $i += 2) {
            $normalized[$i] = $bytes[$i + 1]
            $normalized[$i + 1] = $bytes[$i]
        }
    }
    '40123780' {
        if (($bytes.Length % 4) -ne 0) { throw 'El tamaño no es múltiplo de 4; no se puede normalizar como n64.' }
        for ($i = 0; $i -lt $bytes.Length; $i += 4) {
            $normalized[$i] = $bytes[$i + 3]
            $normalized[$i + 1] = $bytes[$i + 2]
            $normalized[$i + 2] = $bytes[$i + 1]
            $normalized[$i + 3] = $bytes[$i]
        }
    }
}

$normalizedMagic = ($normalized[0..3] | ForEach-Object { $_.ToString('X2') }) -join ''
if ($normalizedMagic -ne '80371240') { throw "La normalización no produjo el magic esperado: $normalizedMagic" }

$destinationDirectory = [System.IO.Path]::GetDirectoryName($destination)
if (-not (Test-Path -LiteralPath $destinationDirectory)) {
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
}
[System.IO.File]::WriteAllBytes($destination, $normalized)

$title = [System.Text.Encoding]::ASCII.GetString($normalized, 0x20, 20).Trim([char]0, [char]32)
$entryBytes = [byte[]]@($normalized[0x0B], $normalized[0x0A], $normalized[0x09], $normalized[0x08])
$entry = [System.BitConverter]::ToUInt32($entryBytes, 0)
$crc1 = ($normalized[0x10..0x13] | ForEach-Object { $_.ToString('X2') }) -join ''
$crc2 = ($normalized[0x14..0x17] | ForEach-Object { $_.ToString('X2') }) -join ''
$gameId = [System.Text.Encoding]::ASCII.GetString($normalized, 0x3C, 2)
$region = [System.Text.Encoding]::ASCII.GetString($normalized, 0x3E, 1)
$revision = $normalized[0x3F].ToString('X2')

[PSCustomObject]@{
    InputPath = $source
    DetectedByteOrder = $byteOrder
    OutputPath = $destination
    SizeBytes = $normalized.Length
    HeaderMagic = $normalizedMagic
    InternalTitle = $title
    EntryPoint = ('0x{0:X8}' -f $entry)
    HeaderCRC1 = $crc1
    HeaderCRC2 = $crc2
    GameId = $gameId
    RegionCode = $region
    RevisionByte = $revision
    InputSHA256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    NormalizedSHA1 = (Get-FileHash -LiteralPath $destination -Algorithm SHA1).Hash
    NormalizedSHA256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
} | Format-List
