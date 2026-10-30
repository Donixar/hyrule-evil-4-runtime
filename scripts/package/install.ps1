<#
.SYNOPSIS
    Instala Link in RE4 en Resident Evil 4 (2005, UHD) de Steam, respaldando cada original.
    Installs Link in RE4 into Steam's Resident Evil 4 (2005, UHD), backing up every original.

.DESCRIPTION
    Windows PowerShell 5.1, no external modules. Run it through Install-LinkInRE4.bat.

    1. Checks: game folder (BIO4\ and Bin32\bio4.exe), the bio4.exe sha256 pin (an exe patched for 4GB/LAA
       is refused), no foreign Bin32\dinput8.dll, no links in the game folder, game closed, free space.
    2. Backup in <game>\LinkInRE4_Backup\<timestamp>\: every game file the package overwrites is copied
       there and every compressed .lfs twin of a package file is moved (renamed, never deleted) there, so
       the game reads the mod's loose files. Each original's sha256 goes to journal.tsv before it is
       touched and to backup.json at the end.
    3. Copies the files of the chosen parts (default: every part found), each through a temporary file
       whose sha256 is checked against package-<part>.json before it replaces the target.
    4. Checks every file and writes <game>\LinkInRE4_Backup\installed.json (files placed, parts, .lfs moved).

    Running it again is safe: an interrupted install is completed, an installed part is only verified, and a
    new part reuses the same backup. Uninstall-LinkInRE4.bat (also copied into LinkInRE4_Backup) restores
    everything.

    Patch mode (3.0.1): when the package folder holds patch-<version>.json instead of package-<part>.json, the
    script updates an install of the patch's from_version in place. It requires LinkInRE4_Backup\installed.json
    with that version, complete, and a record equal to the patch's from_files for every installed part; it
    hashes every file the patch replaces (each must still hold the from_version bytes) and checks the size of
    every other file. Only the installed parts are patched. New originals are backed up in the same backup
    folder and journal, so the uninstaller restores everything as before. A link_runtime.ini edited by the
    player is copied to LinkInRE4_Backup\link_runtime.ini.<from_version>-user before it is replaced. Running
    the patch again completes an interrupted patch, or only verifies a finished one.

    Exit codes: 0 ok, 1 unexpected error, 2 package incomplete, 3 game folder not found, 4 bio4.exe is not
    the expected one, 5 foreign dinput8.dll, 6 not enough free space, 7 game running, 8 inconsistent state or
    links in the game folder, 9 verification failed, 10 cancelled, 12 patch: the game does not hold the
    version the patch updates (or its files changed).

.PARAMETER GamePath
    Carpeta del juego (la que contiene BIO4 y Bin32). Si falta, se busca la instalación de Steam.
    Game folder (the one holding BIO4 and Bin32). If omitted, the Steam install is looked up.
.PARAMETER Parts
    core, sd, hd (separadas por coma / comma separated). Default: every part found.
.PARAMETER PackagePath
    Carpetas LinkInRE4 con package-<parte>.json, separadas por | (default: this script's folder).
.PARAMETER Yes
    No preguntar / do not ask for confirmation.
.PARAMETER DryRun
    Solo comprueba y muestra el plan / only check and show the plan.
.PARAMETER IgnoreRunningGame
    Only for test copies of the game folder: skip the check that RE4 is closed.
.PARAMETER VerifyAll
    Patch mode: also hash the files the patch does not replace (slower; by default their size is checked).
.PARAMETER MergeUserIni
    Patch mode: add missing INI defaults while preserving existing values, comments and unknown keys.
    The package hashes stay in installed.json; the actual merged hash is recorded separately and journaled.
.PARAMETER LibraryOnly
    Define the shared functions and constants, then return without running the installer.
#>
[CmdletBinding()]
param(
    [string]$GamePath = '',
    [string[]]$Parts = @(),
    [string[]]$PackagePath = @(),
    [switch]$Yes,
    [switch]$DryRun,
    [switch]$IgnoreRunningGame,
    [switch]$VerifyAll,
    [switch]$MergeUserIni,
    [switch]$LibraryOnly
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$PackageSchema = 'link-in-re4-package-1'
$PatchSchema = 'link-in-re4-patch-1'
$StateSchema = 'link-in-re4-installed-1'
$BackupSchema = 'link-in-re4-backup-1'
$BackupFolderName = 'LinkInRE4_Backup'
$PartialSuffix = '.lir4-partial'
$PartOrder = @('core', 'sd', 'hd')
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Script:JournalPath = $null
$Script:GameRoot = $null
$Script:AbortCounts = @{}

$EXIT_PACKAGE = 2; $EXIT_GAME = 3; $EXIT_EXE = 4; $EXIT_DINPUT8 = 5; $EXIT_SPACE = 6
$EXIT_RUNNING = 7; $EXIT_STATE = 8; $EXIT_VERIFY = 9; $EXIT_CANCEL = 10; $EXIT_PATCH = 12
$Script:Patch = $null

# ------------------------------------------------------------------ output

function Say([string]$Es, [string]$En, [string]$Color = 'Gray') {
    Write-Host $Es -ForegroundColor $Color
    if ($En) { Write-Host ('  ' + $En) -ForegroundColor DarkGray }
}

function Fail([int]$Code, [string]$Es, [string]$En) {
    Write-Host ''
    Write-Host ('ERROR: ' + $Es) -ForegroundColor Red
    Write-Host ('ERROR: ' + $En) -ForegroundColor Red
    exit $Code
}

function Confirm-Step([string]$Es, [string]$En) {
    if ($Yes) { return $true }
    Write-Host ''
    Write-Host ($Es + ' [S/N]') -ForegroundColor Yellow
    Write-Host ($En + ' [Y/N]') -ForegroundColor Yellow
    try { $answer = Read-Host '>' } catch { return $false }
    return ($answer -match '^\s*(s|si|sí|y|yes)\s*$')
}

function Format-GB([double]$Bytes) { return ('{0:N2} GB' -f ($Bytes / 1e9)) }

# Test hook (mirror tests only): LINKINRE4_TEST_ABORT=<phase>:<n> ends the process abruptly after n steps.
function Test-Abort([string]$Phase) {
    $spec = $env:LINKINRE4_TEST_ABORT
    if (-not $spec) { return }
    $bits = $spec.Split(':')
    if ($bits.Count -ne 2 -or $bits[0] -ne $Phase) { return }
    if (-not $Script:AbortCounts.ContainsKey($Phase)) { $Script:AbortCounts[$Phase] = 0 }
    $Script:AbortCounts[$Phase] += 1
    if ($Script:AbortCounts[$Phase] -ge [int]$bits[1]) {
        Write-Host ('TEST: simulated interruption after ' + $Script:AbortCounts[$Phase] + ' ' + $Phase + ' steps')
        [Environment]::Exit(99)
    }
}

# ------------------------------------------------------------------ files

function Join-Rel([string]$Root, [string]$Rel) {
    return [System.IO.Path]::Combine($Root, ($Rel -replace '/', '\'))
}

function Get-Rel([string]$Full) {
    return $Full.Substring($Script:GameRoot.Length).TrimStart('\') -replace '\\', '/'
}

function Test-File([string]$Path) { return [System.IO.File]::Exists($Path) }

function Get-Length([string]$Path) { return (New-Object System.IO.FileInfo($Path)).Length }

function Test-Reparse([string]$Path) {
    if (-not ([System.IO.File]::Exists($Path) -or [System.IO.Directory]::Exists($Path))) { return $false }
    $attributes = [System.IO.File]::GetAttributes($Path)
    return (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
}

# Relative names and every ancestor must stay local, including missing leaf files,
# temporary files and the backup tree. Also used by the updater through LibraryOnly.
function Get-SafePath([string]$Root, [string]$Rel) {
    if (-not $Rel -or $Rel -match '[\\:<>"|?*\x00-\x1f]' -or $Rel.StartsWith('/') -or
        @($Rel.Split('/') | Where-Object { $_ -eq '.' -or $_ -eq '..' -or -not $_ -or
            $_ -match '[. ]$' -or $_ -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)' }).Count) {
        throw "Ruta insegura / unsafe relative path: $Rel"
    }
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $full = [IO.Path]::GetFullPath((Join-Rel $rootFull $Rel))
    if (-not $full.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Ruta fuera de destino / path outside destination.' }
    $cursor = $full
    while ($cursor) {
        if (Test-Reparse $cursor) { throw "La ruta contiene un enlace o junction / redirected path: $cursor" }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}

function Assert-SafeBackupTree([string]$Root) {
    # Do not use recursive enumeration: reject a directory link before descending into it.
    $pending = New-Object Collections.Stack
    $pending.Push($Root)
    while ($pending.Count) {
        $dir = $pending.Pop()
        if (-not [IO.Directory]::Exists($dir)) { continue }
        foreach ($entry in [IO.Directory]::GetFileSystemEntries($dir)) {
            [void](Get-SafePath $dir ([IO.Path]::GetFileName($entry)))
            if ([IO.Directory]::Exists($entry)) { $pending.Push($entry) }
        }
    }
}

function Get-Sha256([string]$Path) {
    $stream = New-Object System.IO.FileStream($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                                              [System.IO.FileShare]::Read, 1048576)
    try {
        $algorithm = New-Object System.Security.Cryptography.SHA256CryptoServiceProvider
        try { $bytes = $algorithm.ComputeHash($stream) } finally { $algorithm.Dispose() }
    } finally { $stream.Dispose() }
    return ([System.BitConverter]::ToString($bytes) -replace '-', '').ToLowerInvariant()
}

function Clear-ReadOnly([string]$Path) {
    $attributes = [System.IO.File]::GetAttributes($Path)
    if (($attributes -band [System.IO.FileAttributes]::ReadOnly) -ne 0) {
        [System.IO.File]::SetAttributes($Path, ($attributes -bxor [System.IO.FileAttributes]::ReadOnly))
    }
}

function Remove-FileIfPresent([string]$Path) {
    if (Test-File $Path) { Clear-ReadOnly $Path; [System.IO.File]::Delete($Path) }
}

function Read-Json([string]$Path) {
    return ([System.IO.File]::ReadAllText($Path, $Utf8) | ConvertFrom-Json)
}

function Write-JsonFile([string]$Path, $Object) {
    $text = $Object | ConvertTo-Json -Depth 8
    $temporary = $Path + '.writing'
    [System.IO.File]::WriteAllText($temporary, $text, $Utf8)
    if (Test-File $Path) { [System.IO.File]::Replace($temporary, $Path, [NullString]::Value) }
    else { [System.IO.File]::Move($temporary, $Path) }
}

function Add-Journal([string]$Line) {
    [System.IO.File]::AppendAllText($Script:JournalPath, $Line + "`r`n", $Utf8)
}

# Only insert absent keys. Existing text (including comments, ordering and unknown sections) is
# preserved verbatim. INI section/key lookup is case insensitive, as in the runtime's Windows API.
function Merge-IniText([string]$Original, [string]$Defaults) {
    $keys = @{}
    $ends = @{}
    $section = ''
    $linePattern = '[^\r\n]*(?:\r\n|\n|\r|$)'
    foreach ($line in [regex]::Matches($Original, $linePattern)) {
        $text = $line.Value.TrimEnd("`r", "`n")
        if ($text -match '^\s*\[([^\]]+)\]\s*(?:[;#].*)?$') {
            $ends[$section] = $line.Index
            $section = $Matches[1].Trim()
        } elseif ($text -match '^\s*([^;#=\[\]\s][^=]*?)\s*=') {
            $keys[$section + "`0" + $Matches[1].Trim()] = $true
        }
    }
    $ends[$section] = $Original.Length
    $missing = @{}
    $order = New-Object System.Collections.ArrayList
    $section = ''
    foreach ($line in [regex]::Matches($Defaults, $linePattern)) {
        $text = $line.Value.TrimEnd("`r", "`n")
        if ($text -match '^\s*\[([^\]]+)\]\s*(?:[;#].*)?$') {
            $section = $Matches[1].Trim()
        } elseif ($text -match '^\s*([^;#=\[\]\s][^=]*?)\s*=') {
            $key = $section + "`0" + $Matches[1].Trim()
            if (-not $keys.ContainsKey($key)) {
                if (-not $missing.ContainsKey($section)) {
                    $missing[$section] = New-Object System.Collections.ArrayList
                    [void]$order.Add($section)
                }
                [void]$missing[$section].Add($text)
                $keys[$key] = $true
            }
        }
    }
    if ($order.Count -eq 0) { return $Original }
    $newline = "`r`n"
    $firstNewline = [regex]::Match($Original, '\r\n|\n|\r')
    if ($firstNewline.Success) { $newline = $firstNewline.Value }
    $inserts = New-Object System.Collections.ArrayList
    $append = ''
    foreach ($name in $order) {
        $text = (@($missing[$name]) -join $newline) + $newline
        if ($ends.ContainsKey($name)) {
            [void]$inserts.Add(@{ Offset = $ends[$name]; Text = $text })
        } else {
            $append += ('[' + $name + ']' + $newline + $text)
        }
    }
    $result = $Original
    foreach ($insert in @($inserts | Sort-Object { $_.Offset } -Descending)) {
        $prefix = ''
        if ($insert.Offset -gt 0 -and $Original[$insert.Offset - 1] -ne "`r" -and $Original[$insert.Offset - 1] -ne "`n") { $prefix = $newline }
        $result = $result.Insert($insert.Offset, $prefix + $insert.Text)
    }
    if ($append) {
        if ($result.Length -gt 0 -and -not ($result.EndsWith("`r") -or $result.EndsWith("`n"))) { $result += $newline }
        $result += $append
    }
    return $result
}

function Get-MergedIni([string]$CurrentPath, [string]$DefaultsPath) {
    # Detect BOMs and keep the user's encoding. Reject invalid UTF-8 before trying the Windows
    # legacy code page; silently replacing an accented byte would alter the player's comments.
    $strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
    $reader = New-Object System.IO.StreamReader($CurrentPath, $strictUtf8, $true)
    try {
        try { $original = $reader.ReadToEnd(); $encoding = $reader.CurrentEncoding }
        catch [System.Text.DecoderFallbackException] {
            $reader.Dispose()
            $reader = New-Object System.IO.StreamReader($CurrentPath, ([System.Text.Encoding]::Default), $true)
            $original = $reader.ReadToEnd()
            $encoding = $reader.CurrentEncoding
        }
    } finally { $reader.Dispose() }
    $defaults = [System.IO.File]::ReadAllText($DefaultsPath, $Utf8)
    $merged = Merge-IniText $original $defaults
    [byte[]]$bytes = @($encoding.GetPreamble()) + @($encoding.GetBytes($merged))
    $algorithm = New-Object System.Security.Cryptography.SHA256CryptoServiceProvider
    try { $hash = $algorithm.ComputeHash($bytes) } finally { $algorithm.Dispose() }
    return @{ Content = $bytes; Bytes = [long]$bytes.Length
              Sha = ([System.BitConverter]::ToString($hash) -replace '-', '').ToLowerInvariant() }
}

# Journal lines: loose|lfs <path> <sha256> <bytes> (original backed up), placed <path> <sha256> <bytes>,
# dir <path> (folder created in the game). A torn last line is ignored.
function Read-Journal([string]$Path) {
    $result = @{ Backup = @{}; Placed = @{}; Dirs = (New-Object System.Collections.ArrayList) }
    if (-not (Test-File $Path)) { return $result }
    foreach ($line in [System.IO.File]::ReadAllLines($Path, $Utf8)) {
        $f = $line.Split("`t")
        if ($f.Count -eq 4 -and ($f[0] -eq 'loose' -or $f[0] -eq 'lfs') -and $f[2] -match '^[0-9a-f]{64}$') {
            $result.Backup[$f[1]] = @{ kind = $f[0]; path = $f[1]; sha256 = $f[2]; bytes = [long]$f[3] }
        } elseif ($f.Count -eq 4 -and $f[0] -eq 'placed' -and $f[2] -match '^[0-9a-f]{64}$') {
            if (-not $result.Placed.ContainsKey($f[1])) { $result.Placed[$f[1]] = @{} }
            $result.Placed[$f[1]][$f[2]] = $true
        } elseif ($f.Count -eq 2 -and $f[0] -eq 'dir') {
            [void]$result.Dirs.Add($f[1])
        }
    }
    return $result
}

function New-GameDirectory([string]$Dir) {
    if ([System.IO.Directory]::Exists($Dir)) { return }
    New-GameDirectory ([System.IO.Path]::GetDirectoryName($Dir))
    [void][System.IO.Directory]::CreateDirectory($Dir)
    Add-Journal ('dir' + "`t" + (Get-Rel $Dir))
}

# ------------------------------------------------------------------ game

function Test-GameFolder([string]$Path) {
    if (-not $Path) { return $false }
    return ([System.IO.Directory]::Exists((Join-Path $Path 'BIO4')) -and (Test-File (Join-Path $Path 'Bin32\bio4.exe')))
}

function Find-Game {
    $candidates = New-Object System.Collections.ArrayList
    foreach ($key in @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 254700',
                       'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 254700')) {
        try {
            $value = (Get-ItemProperty -LiteralPath $key -Name InstallLocation -ErrorAction Stop).InstallLocation
            if ($value) { [void]$candidates.Add($value) }
        } catch { }
    }
    try {
        $steam = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction Stop).SteamPath
        if ($steam) {
            $steam = $steam -replace '/', '\'
            [void]$candidates.Add((Join-Path $steam 'steamapps\common\Resident Evil 4'))
            $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
            if (Test-File $vdf) {
                foreach ($match in [regex]::Matches([System.IO.File]::ReadAllText($vdf), '"path"\s+"([^"]+)"')) {
                    $library = $match.Groups[1].Value -replace '\\\\', '\'
                    [void]$candidates.Add((Join-Path $library 'steamapps\common\Resident Evil 4'))
                }
            }
        }
    } catch { }
    foreach ($candidate in $candidates) {
        if (Test-GameFolder $candidate) { return $candidate }
    }
    return $null
}

function Resolve-Game {
    if ($GamePath) {
        if (-not (Test-GameFolder $GamePath)) {
            Fail $EXIT_GAME ("No encontré BIO4\ y Bin32\bio4.exe en: " + $GamePath + '. Indica la carpeta de Resident Evil 4 (la que contiene BIO4 y Bin32).') `
                ("BIO4\ and Bin32\bio4.exe not found in: " + $GamePath + '. Give the Resident Evil 4 folder (the one holding BIO4 and Bin32).')
        }
        return (Resolve-Path -LiteralPath $GamePath).ProviderPath.TrimEnd('\')
    }
    $found = Find-Game
    if ($found) {
        Say ('Juego encontrado: ' + $found) ('Game found: ' + $found)
        if (-not (Confirm-Step 'Usar esta carpeta?' 'Use this folder?')) {
            $found = $null
        }
    }
    if (-not $found) {
        if ($Yes) {
            Fail $EXIT_GAME 'No encontré Resident Evil 4. Usa -GamePath "<carpeta del juego>".' 'Resident Evil 4 not found. Use -GamePath "<game folder>".'
        }
        Say 'Pega la ruta de la carpeta de Resident Evil 4 (la que contiene BIO4 y Bin32):' 'Paste the Resident Evil 4 folder path (the one holding BIO4 and Bin32):' 'Yellow'
        try { $typed = (Read-Host '>').Trim().Trim('"') } catch { $typed = '' }
        if (-not (Test-GameFolder $typed)) {
            Fail $EXIT_GAME ('No encontré BIO4\ y Bin32\bio4.exe en: ' + $typed) ('BIO4\ and Bin32\bio4.exe not found in: ' + $typed)
        }
        $found = $typed
    }
    return (Resolve-Path -LiteralPath $found).ProviderPath.TrimEnd('\')
}

# Only process names are read (no handles or memory of the game).
function Test-GameRunning {
    if ($IgnoreRunningGame) { return $false }
    return (@(Get-Process -Name 'bio4' -ErrorAction SilentlyContinue).Count -gt 0)
}

# ------------------------------------------------------------------ package

function Split-List([string[]]$Values, [string]$Separator) {
    $out = New-Object System.Collections.ArrayList
    foreach ($value in $Values) {
        foreach ($piece in ($value -split [regex]::Escape($Separator))) {
            if ($piece.Trim()) { [void]$out.Add($piece.Trim()) }
        }
    }
    return ,$out
}

# A patch folder holds patch-<version>.json and files/ with only the files that changed. Its parts mirror
# package-<part>.json (every file of the new version, lfs_to_move) plus from_files (the record of the version
# it updates) and changed (the paths shipped in files/).
function Get-Patch([string[]]$Dirs) {
    $found = New-Object System.Collections.ArrayList
    foreach ($dir in $Dirs) {
        if (-not [System.IO.Directory]::Exists($dir)) { continue }
        $dir = (Resolve-Path -LiteralPath $dir).ProviderPath
        foreach ($file in [System.IO.Directory]::GetFiles($dir, 'patch-*.json')) { [void]$found.Add(@{ File = $file; Dir = $dir }) }
    }
    if ($found.Count -eq 0) { return $null }
    if ($found.Count -gt 1) {
        Fail $EXIT_PACKAGE 'Hay más de un parche en la carpeta. Extrae cada parche en una carpeta nueva.' 'More than one patch in the folder. Extract each patch into a new folder.'
    }
    $dir = $found[0].Dir
    if (@([System.IO.Directory]::GetFiles($dir, 'package-*.json')).Count -gt 0) {
        Fail $EXIT_PACKAGE 'La carpeta mezcla un parche con un paquete completo. Extrae el parche en una carpeta NUEVA y ejecútalo desde ahí.' `
            'The folder mixes a patch with a full package. Extract the patch into a NEW folder and run it from there.'
    }
    try { $patch = Read-Json $found[0].File } catch {
        Fail $EXIT_PACKAGE ('No pude leer ' + $found[0].File + '. Vuelve a extraer el parche.') ('Cannot read ' + $found[0].File + '. Extract the patch again.')
    }
    if ($patch.schema -ne $PatchSchema -or -not $patch.from_version -or -not $patch.version) {
        Fail $EXIT_PACKAGE ('Archivo de parche desconocido: ' + $found[0].File) ('Unknown patch file: ' + $found[0].File)
    }
    return @{ Manifest = $patch; Dir = $dir }
}

function Get-Packages {
    $dirs = Split-List $PackagePath '|'
    if ($dirs.Count -eq 0) { [void]$dirs.Add($PSScriptRoot) }
    $Script:Patch = Get-Patch $dirs
    if ($null -ne $Script:Patch) {
        $found = @{}
        foreach ($part in $PartOrder) {
            $p = $Script:Patch.Manifest.parts.PSObject.Properties[$part]
            if ($null -eq $p) { continue }
            $m = [pscustomobject]@{ schema = $PackageSchema; part = $part; version = $Script:Patch.Manifest.version
                                    bio4_exe_sha256 = $Script:Patch.Manifest.bio4_exe_sha256
                                    files = @($p.Value.files); lfs_to_move = @($p.Value.lfs_to_move) }
            $found[$part] = @{ Manifest = $m; Dir = $Script:Patch.Dir; From = $p.Value.from_files; Changed = @($p.Value.changed) }
        }
        return $found
    }
    $found = @{}
    foreach ($dir in $dirs) {
        if (-not [System.IO.Directory]::Exists($dir)) {
            Fail $EXIT_PACKAGE ('No existe la carpeta del paquete: ' + $dir) ('Package folder not found: ' + $dir)
        }
        $dir = (Resolve-Path -LiteralPath $dir).ProviderPath
        foreach ($file in [System.IO.Directory]::GetFiles($dir, 'package-*.json')) {
            try { $manifest = Read-Json $file } catch {
                Fail $EXIT_PACKAGE ('No pude leer ' + $file + '. Vuelve a extraer el paquete.') ('Cannot read ' + $file + '. Extract the package again.')
            }
            if ($manifest.schema -ne $PackageSchema -or $PartOrder -notcontains $manifest.part) {
                Fail $EXIT_PACKAGE ('Archivo de paquete desconocido: ' + $file) ('Unknown package file: ' + $file)
            }
            if ($found.ContainsKey($manifest.part)) {
                Fail $EXIT_PACKAGE ('La parte ' + $manifest.part + ' aparece dos veces.') ('Part ' + $manifest.part + ' found twice.')
            }
            $found[$manifest.part] = @{ Manifest = $manifest; Dir = $dir }
        }
    }
    if ($found.Count -eq 0) {
        Fail $EXIT_PACKAGE 'No encontré ningún package-*.json. Extrae los .7z en la misma carpeta LinkInRE4 y ejecuta el instalador desde ahí.' `
            'No package-*.json found. Extract the .7z archives into the same LinkInRE4 folder and run the installer from there.'
    }
    $versions = @($found.Values | ForEach-Object { $_.Manifest.version } | Sort-Object -Unique)
    if ($versions.Count -ne 1) {
        Fail $EXIT_PACKAGE ('Las partes son de versiones distintas: ' + ($versions -join ', ')) ('The parts come from different versions: ' + ($versions -join ', '))
    }
    return $found
}

# ------------------------------------------------------------------ main

function Invoke-Install {
    Say 'Link in RE4 - instalador' 'Link in RE4 - installer' 'Cyan'
    $packages = Get-Packages
    $version = @($packages.Values)[0].Manifest.version
    $patchMode = ($null -ne $Script:Patch)
    if ($patchMode) {
        Say ('Parche ' + $Script:Patch.Manifest.from_version + ' -> ' + $version + ': actualiza una instalación de la ' + $Script:Patch.Manifest.from_version + '.') `
            ('Patch ' + $Script:Patch.Manifest.from_version + ' -> ' + $version + ': updates a ' + $Script:Patch.Manifest.from_version + ' install.') 'Cyan'
    }

    $requested = Split-List $Parts ','
    if ($patchMode) { $requested = New-Object System.Collections.ArrayList }  # the installed parts decide
    $selected = New-Object System.Collections.ArrayList
    foreach ($part in $PartOrder) {
        if ($requested.Count -eq 0) {
            if ($packages.ContainsKey($part)) { [void]$selected.Add($part) }
        } elseif ($requested -contains $part) {
            if (-not $packages.ContainsKey($part)) {
                Fail $EXIT_PACKAGE ('Falta la parte ' + $part + ': extrae su .7z en esta misma carpeta.') ('Part ' + $part + ' is missing: extract its .7z into this same folder.')
            }
            [void]$selected.Add($part)
        }
    }
    foreach ($part in $requested) {
        if ($PartOrder -notcontains $part) { Fail $EXIT_PACKAGE ('Parte desconocida: ' + $part) ('Unknown part: ' + $part) }
    }
    if ($selected.Count -eq 0) { Fail $EXIT_PACKAGE 'No hay partes que instalar.' 'No parts to install.' }

    # Every file of the chosen parts must be present with its size (a partial extraction stops here).
    $planFiles = New-Object System.Collections.ArrayList
    $packageSha = @{}
    foreach ($entry in $packages.GetEnumerator()) {
        foreach ($f in @($entry.Value.Manifest.files)) { $packageSha[$f.path] = $f.sha256 }
    }
    if ($patchMode) { $selected.Clear() }  # filled from installed.json once the game folder is known
    if (-not $patchMode) { foreach ($part in $selected) {
        $package = $packages[$part]
        $missing = New-Object System.Collections.ArrayList
        foreach ($f in @($package.Manifest.files)) {
            $source = Join-Rel (Join-Path $package.Dir 'files') $f.path
            if (-not (Test-File $source) -or (Get-Length $source) -ne [long]$f.bytes) { [void]$missing.Add($f.path); continue }
            [void]$planFiles.Add(@{ Path = $f.path; Sha = $f.sha256; Bytes = [long]$f.bytes; Part = $part; Source = $source;
                                    Target = $null; Current = $null; Done = $false })
        }
        if ($missing.Count -gt 0) {
            Fail $EXIT_PACKAGE ('Faltan ' + $missing.Count + ' archivos de la parte ' + $part + ' (p. ej. ' + $missing[0] + '). Vuelve a extraer su .7z completo.') `
                ('' + $missing.Count + ' files of part ' + $part + ' are missing (e.g. ' + $missing[0] + '). Extract its .7z again.')
        }
    } }

    $game = Resolve-Game
    $Script:GameRoot = $game
    try { [void](Get-SafePath $game 'Bin32/bio4.exe') }
    catch { Fail $EXIT_STATE $_.Exception.Message 'Unsafe game path. Nothing was changed.' }
    Say ('Carpeta del juego: ' + $game) ('Game folder: ' + $game)

    if (Test-GameRunning) {
        Fail $EXIT_RUNNING 'Resident Evil 4 está abierto. Ciérralo y vuelve a ejecutar el instalador.' 'Resident Evil 4 is running. Close it and run the installer again.'
    }

    $pin = @($packages.Values)[0].Manifest.bio4_exe_sha256
    $exeHash = Get-Sha256 (Join-Path $game 'Bin32\bio4.exe')
    if ($exeHash -ne $pin) {
        Fail $EXIT_EXE ('Tu Bin32\bio4.exe no es el original de Steam (1.1.0) que el mod necesita. Si un mod (re4_tweaks, re4_fps, 4GB Patch) te ofreció el parche de 4GB/LAA y respondiste Sí, en Steam haz clic derecho en Resident Evil 4 > Propiedades > Archivos instalados > Verificar integridad, y cuando vuelvan a preguntarte por el parche de 4GB responde No. sha256 esperado ' + $pin + ', encontrado ' + $exeHash + '.') `
            ('Your Bin32\bio4.exe is not the original Steam 1.1.0 exe the mod needs. If a mod (re4_tweaks, re4_fps, 4GB Patch) offered the 4GB/LAA patch and you answered Yes, right-click Resident Evil 4 in Steam > Properties > Installed Files > Verify integrity, and answer No when asked about the 4GB patch again. Expected sha256 ' + $pin + ', found ' + $exeHash + '.')
    }

    $backupRoot = Join-Path $game $BackupFolderName
    $statePath = Join-Path $backupRoot 'installed.json'
    try {
        [void](Get-SafePath $game ($BackupFolderName + '/installed.json'))
        Assert-SafeBackupTree $backupRoot
    } catch { Fail $EXIT_STATE $_.Exception.Message 'Unsafe backup path. Nothing was changed.' }
    $state = $null
    if (Test-File $statePath) {
        try { $state = Read-Json $statePath } catch { $state = $null }
        if ($null -eq $state -or $state.schema -ne $StateSchema) {
            Fail $EXIT_STATE ('No pude leer ' + $statePath + '. No se cambió nada.') ('Cannot read ' + $statePath + '. Nothing was changed.')
        }
    }
    $backupName = $null
    if ($null -ne $state) {
        $backupName = $state.backup_dir
        try { [void](Get-SafePath $backupRoot $backupName) }
        catch { Fail $EXIT_STATE $_.Exception.Message 'Unsafe backup directory. Nothing was changed.' }
        if (-not [System.IO.Directory]::Exists((Join-Path $backupRoot $backupName))) {
            Fail $EXIT_STATE ('Falta la carpeta de respaldo ' + $backupName + ' indicada en installed.json. No se cambió nada.') ('Backup folder ' + $backupName + ' named in installed.json is missing. Nothing was changed.')
        }
        if ($state.status -ne 'complete') {
            Say 'Hay una instalación anterior sin terminar: se va a completar.' 'A previous install did not finish: it will be completed.' 'Yellow'
        }
    } else {
        $backupName = Get-Date -Format 'yyyyMMdd-HHmmss'
    }
    $backupDir = Join-Path $backupRoot $backupName
    $filesDir = Join-Path $backupDir 'files'
    $journal = Read-Journal (Join-Path $backupDir 'journal.tsv')
    $backup = @{}
    $backupJson = Join-Path $backupDir 'backup.json'
    if (Test-File $backupJson) {
        foreach ($e in @((Read-Json $backupJson).entries)) {
            $backup[$e.path] = @{ kind = $e.kind; path = $e.path; sha256 = $e.sha256; bytes = [long]$e.bytes }
        }
    }
    foreach ($e in $journal.Backup.Values) { $backup[$e.path] = $e }
    $ours = @{}
    foreach ($p in $journal.Placed.Keys) { $ours[$p] = $journal.Placed[$p] }
    $previous = @{}
    if ($null -ne $state) {
        foreach ($f in @($state.files)) {
            $previous[$f.path] = $f
            if ($state.status -eq 'complete') {
                if (-not $ours.ContainsKey($f.path)) { $ours[$f.path] = @{} }
                $ours[$f.path][$f.sha256] = $true
            }
        }
    }
    $installedParts = @()
    if ($null -ne $state) {
        # parts/files describe the persisted plan even while status is installing.
        $installedParts = @(@($state.parts) + @($state.files | ForEach-Object { $_.part }) | Sort-Object -Unique)
        if (-not $patchMode) {
            if ($state.version -ne $version) {
                Fail $EXIT_STATE 'El paquete completo es de otra versión. Usa el parche correspondiente o desinstala primero.' `
                    'The full package is a different version. Use the matching patch or uninstall first.'
            }
            # Keep every recorded file in the plan, including parts omitted on this invocation.
            # A verified file needs no payload (e.g. Core already installed, resuming an SD download).
            $planned = @{}
            foreach ($f in $planFiles) { $planned[$f.Path] = $true }
            foreach ($f in @($state.files)) {
                if ($planned.ContainsKey($f.path)) { continue }
                $source = $null
                if ($packages.ContainsKey($f.part)) {
                    $match = @($packages[$f.part].Manifest.files | Where-Object { $_.path -eq $f.path -and $_.sha256 -eq $f.sha256 -and $_.bytes -eq $f.bytes })
                    if ($match.Count -eq 1) { $source = Join-Rel (Join-Path $packages[$f.part].Dir 'files') $f.path }
                }
                [void]$planFiles.Add(@{ Path = $f.path; Sha = $f.sha256; Bytes = [long]$f.bytes; Part = $f.part;
                    Source = $source; Target = $null; Current = $null; Done = $false; Retained = $true })
            }
        }
    }
    $fromSha = @{}
    $userIni = $null
    $mergedIni = $null
    $retainedIni = $null
    if ($patchMode) {
        $from = $Script:Patch.Manifest.from_version
        if ($null -eq $state) {
            Fail $EXIT_PATCH ('No encontré Link in RE4 ' + $from + ' instalado en esta carpeta (falta LinkInRE4_Backup\installed.json). El parche solo actualiza una instalación hecha con el instalador de la ' + $from + '; si no la tienes, usa el paquete completo. No se cambió nada.') `
                ('Link in RE4 ' + $from + ' is not installed in this folder (LinkInRE4_Backup\installed.json is missing). The patch only updates an install made with the ' + $from + ' installer; otherwise use the full package. Nothing was changed.')
        }
        # Same version as the patch: an interrupted patch to finish, or a finished one to verify.
        $resume = ($state.version -eq $version)
        if (-not $resume -and $state.version -ne $from) {
            Fail $EXIT_PATCH ('Tienes instalada la versión ' + $state.version + ' y este parche es para la ' + $from + '. Usa el paquete completo: desinstala y vuelve a instalar. No se cambió nada.') `
                ('Version ' + $state.version + ' is installed and this patch is for ' + $from + '. Use the full package: uninstall and install again. Nothing was changed.')
        }
        if (-not $resume -and $state.status -ne 'complete') {
            Fail $EXIT_PATCH ('La instalación de la ' + $from + ' no terminó. Ejecuta primero el instalador de la ' + $from + ' para completarla (o desinstala y usa el paquete completo). No se cambió nada.') `
                ('The ' + $from + ' install did not finish. Run the ' + $from + ' installer first to complete it (or uninstall and use the full package). Nothing was changed.')
        }
        foreach ($part in $PartOrder) {
            if (@($state.parts) -contains $part) {
                if (-not $packages.ContainsKey($part)) { Fail $EXIT_PACKAGE ('El parche no trae la parte ' + $part + '.') ('The patch has no part ' + $part + '.') }
                [void]$selected.Add($part)
            }
        }
        if ($selected.Count -eq 0) { Fail $EXIT_PATCH 'installed.json no nombra ninguna parte. No se cambió nada.' 'installed.json names no part. Nothing was changed.' }
        # The install record must be the one this patch starts from (or, resuming, the one it ends with).
        $want = @{}
        foreach ($part in $selected) {
            if ($resume) { foreach ($f in @($packages[$part].Manifest.files)) { $want[$f.path] = $f.sha256 } }
            else { foreach ($p in $packages[$part].From.PSObject.Properties) { $want[$p.Name] = $p.Value } }
            foreach ($p in $packages[$part].From.PSObject.Properties) { $fromSha[$p.Name] = $p.Value }
        }
        # The installed version's files are the mod's own, even if the old journal is gone.
        foreach ($k in $fromSha.Keys) {
            if (-not $ours.ContainsKey($k)) { $ours[$k] = @{} }
            $ours[$k][$fromSha[$k]] = $true
        }
        $recorded = @{}
        foreach ($f in @($state.files)) { $recorded[$f.path] = $f.sha256 }
        $mismatch = New-Object System.Collections.ArrayList
        foreach ($k in $want.Keys) { if (-not $recorded.ContainsKey($k) -or $recorded[$k] -ne $want[$k]) { [void]$mismatch.Add($k) } }
        foreach ($k in $recorded.Keys) { if (-not $want.ContainsKey($k)) { [void]$mismatch.Add($k) } }
        if ($mismatch.Count -gt 0) {
            $expected = $from; if ($resume) { $expected = $version }
            Fail $EXIT_PATCH ('El registro de instalación no es el de la ' + $expected + ' (' + $mismatch.Count + ' diferencias, p. ej. ' + $mismatch[0] + '). Usa el paquete completo. No se cambió nada.') `
                ('The install record is not the ' + $expected + ' one (' + $mismatch.Count + ' differences, e.g. ' + $mismatch[0] + '). Use the full package. Nothing was changed.')
        }
        if (@($Script:Patch.Manifest.removed).Count -gt 0) {
            Fail $EXIT_PACKAGE 'Este instalador no sabe quitar archivos de una versión anterior. Usa el paquete completo.' 'This installer cannot remove files of an older version. Use the full package.'
        }
        $missing = New-Object System.Collections.ArrayList
        foreach ($part in $selected) {
            $changed = @{}
            foreach ($c in $packages[$part].Changed) { $changed[$c] = $true }
            foreach ($f in @($packages[$part].Manifest.files)) {
                $item = @{ Path = $f.path; Sha = $f.sha256; Bytes = [long]$f.bytes; Part = $part; Source = $null
                           Target = $null; Current = $null; Done = $false; Unchanged = (-not $changed.ContainsKey($f.path)) }
                if (-not $item.Unchanged) {
                    $item.Source = Join-Rel (Join-Path $Script:Patch.Dir 'files') $f.path
                    if (-not (Test-File $item.Source) -or (Get-Length $item.Source) -ne $item.Bytes) { [void]$missing.Add($f.path); continue }
                }
                [void]$planFiles.Add($item)
            }
        }
        if ($missing.Count -gt 0) {
            Fail $EXIT_PACKAGE ('Faltan ' + $missing.Count + ' archivos del parche (p. ej. ' + $missing[0] + '). Extrae todos los ZIP del parche en la misma carpeta y vuelve a intentar.') `
                ('' + $missing.Count + ' patch files are missing (e.g. ' + $missing[0] + '). Extract all patch ZIPs into the same folder and retry.')
        }
    }
    if (-not ($selected -contains 'core') -and -not ($installedParts -contains 'core')) {
        Fail $EXIT_PACKAGE 'Las texturas SD/HD necesitan la parte core: instálala primero (o junto con ellas).' 'The SD/HD textures need the core part: install it first (or together).'
    }

    # Validate all destinations before touching either the game or its backup.
    try {
        foreach ($f in $planFiles) {
            $f.Target = Get-SafePath $game $f.Path
            foreach ($suffix in @('', '.lfs', $PartialSuffix)) { [void](Get-SafePath $game ($f.Path + $suffix)) }
            foreach ($suffix in @('', '.lfs', $PartialSuffix)) { [void](Get-SafePath $filesDir ($f.Path + $suffix)) }
        }
        foreach ($part in $selected) {
            foreach ($l in @($packages[$part].Manifest.lfs_to_move)) {
                [void](Get-SafePath $game $l)
                [void](Get-SafePath $filesDir $l)
            }
        }
        foreach ($e in $backup.Values) {
            [void](Get-SafePath $game $e.path)
            [void](Get-SafePath $filesDir $e.path)
        }
        foreach ($d in $journal.Dirs) { [void](Get-SafePath $game $d) }
    } catch { Fail $EXIT_STATE $_.Exception.Message 'Unsafe game or backup path. Nothing was changed.' }

    $dll = Join-Path $game 'Bin32\dinput8.dll'
    if (Test-File $dll) {
        $dllHash = Get-Sha256 $dll
        $isOurs = ($packageSha['Bin32/dinput8.dll'] -eq $dllHash) -or ($ours.ContainsKey('Bin32/dinput8.dll') -and $ours['Bin32/dinput8.dll'].ContainsKey($dllHash))
        if (-not $isOurs) {
            Fail $EXIT_DINPUT8 'Ya hay un Bin32\dinput8.dll que no es de Link in RE4 (suele ser re4_tweaks o re4_fps), y Link in RE4 usa ese mismo nombre. Renombra el tuyo a winmm.dll (y su dinput8.ini, si existe, a winmm.ini) y vuelve a ejecutar el instalador. Ojo: el mod "FIX Steam must be running" también usa winmm.dll. No se cambió nada.' `
                'Bin32\dinput8.dll already exists and is not Link in RE4 (usually re4_tweaks or re4_fps); Link in RE4 uses that same name. Rename yours to winmm.dll (and its dinput8.ini, if any, to winmm.ini) and run the installer again. Note: the "FIX Steam must be running" mod also uses winmm.dll. Nothing was changed.'
        }
    }

    # Current state of every target: ours already, an original to back up, or a conflict.
    Say 'Comprobando los archivos del juego...' 'Checking the game files...'
    $needBackup = New-Object System.Collections.ArrayList
    $conflicts = New-Object System.Collections.ArrayList
    $checked = 0
    foreach ($f in $planFiles) {
        $f.OutputSha = $f.Sha
        $f.OutputBytes = $f.Bytes
        $checked += 1
        if ($checked % 200 -eq 0) { Write-Host ('  ' + $checked + '/' + $planFiles.Count) }
        if ($patchMode -and $f.Unchanged) {
            # Kept from the installed version: it must be there, with its size (and its hash with -VerifyAll).
            # An unchanged link_runtime.ini only has to exist: the player may have edited it, and it stays.
            $isIni = ($f.Path -eq 'Bin32/link_runtime.ini')
            if (-not (Test-File $f.Target) -or (-not $isIni -and ((Get-Length $f.Target) -ne $f.Bytes -or ($VerifyAll -and (Get-Sha256 $f.Target) -ne $f.Sha)))) {
                [void]$conflicts.Add($f.Path)
            } else {
                $f.Done = $true
                if ($isIni) { $f.OutputBytes = Get-Length $f.Target }
            }
            continue
        }
        if (-not (Test-File $f.Target)) {
            if ($patchMode -and $fromSha.ContainsKey($f.Path)) { [void]$conflicts.Add($f.Path) }
            continue
        }
        $f.Current = Get-Sha256 $f.Target
        if ($f.Path -eq 'Bin32/link_runtime.ini' -and -not $patchMode -and ($f.ContainsKey('Retained') -or $ours.ContainsKey($f.Path)) -and
            -not ($backup.ContainsKey($f.Path) -and $f.Current -eq $backup[$f.Path].sha256 -and -not (Test-File (Join-Rel $filesDir $f.Path)))) {
            # Re-running the installer (textures added, a resume, Core again) must not reset the player's settings. With
            # Core selected only an INI this backup set placed counts, so a leftover from before is still backed up, as is a
            # backed-up original the backup no longer holds (an uninstall that ended with warnings put it back). Verify and
            # record their actual bytes without claiming ownership of a user edit in the journal.
            $f.OutputSha = $f.Current
            $f.OutputBytes = Get-Length $f.Target
            $f.Done = $true
            $retainedIni = $f
            continue
        }
        if ($patchMode -and $MergeUserIni -and $f.Path -eq 'Bin32/link_runtime.ini') {
            if ((Get-Sha256 $f.Source) -ne $f.Sha) {
                Fail $EXIT_VERIFY ('El archivo del paquete ' + $f.Path + ' está dañado. Vuelve a extraer el parche.') `
                    ('Package file ' + $f.Path + ' is damaged. Extract the patch again.')
            }
            $merged = Get-MergedIni $f.Target $f.Source
            $f.MergeContent = $merged.Content
            $f.OutputSha = $merged.Sha
            $f.OutputBytes = $merged.Bytes
            $f.Done = ($f.Current -eq $f.OutputSha)
            $mergedIni = $f
            # Preserve a recoverable user copy before managing its merged bytes. A repeat on our
            # previously merged file needs no new snapshot; edits since that merge do.
            if ($f.Current -ne $f.Sha -and -not ($ours.ContainsKey($f.Path) -and $ours[$f.Path].ContainsKey($f.Current))) {
                $userIni = $f
            }
            continue
        }
        if ($f.Current -eq $f.OutputSha) { $f.Done = $true; continue }
        if ($patchMode -and $fromSha.ContainsKey($f.Path) -and $f.Current -ne $fromSha[$f.Path] -and
            -not ($backup.ContainsKey($f.Path) -and $f.Current -eq $backup[$f.Path].sha256)) {
            if ($f.Path -eq 'Bin32/link_runtime.ini') {
                # The player's own settings: kept beside the backup, then replaced by the new INI.
                $userIni = $f
                if (-not $ours.ContainsKey($f.Path)) { $ours[$f.Path] = @{} }
                $ours[$f.Path][$f.Current] = $true
                continue
            }
            [void]$conflicts.Add($f.Path)
            continue
        }
        $isOurs = $ours.ContainsKey($f.Path) -and $ours[$f.Path].ContainsKey($f.Current)
        if ($backup.ContainsKey($f.Path)) {
            if ($f.Current -eq $backup[$f.Path].sha256) {
                # The original is in the game again (Steam verify, or an uninstall that stopped with warnings):
                # back it up once more if the backup no longer holds it.
                if (-not (Test-File (Join-Rel $filesDir $f.Path))) { [void]$needBackup.Add($f) }
            } elseif (-not $isOurs) {
                [void]$conflicts.Add($f.Path)
            }
        } elseif (-not $isOurs) {
            [void]$needBackup.Add($f)
        }
    }

    $lfsSeen = @{}
    $lfsList = New-Object System.Collections.ArrayList
    foreach ($part in $selected) {
        foreach ($l in @($packages[$part].Manifest.lfs_to_move)) { if (-not $lfsSeen.ContainsKey($l)) { $lfsSeen[$l] = $true; [void]$lfsList.Add($l) } }
    }
    foreach ($f in $planFiles) {
        $l = $f.Path + '.lfs'
        if (-not $lfsSeen.ContainsKey($l)) { $lfsSeen[$l] = $true; [void]$lfsList.Add($l) }
    }
    $lfsMove = New-Object System.Collections.ArrayList
    $lfsDuplicates = New-Object System.Collections.ArrayList
    foreach ($l in $lfsList) {
        $path = Join-Rel $game $l
        if (-not (Test-File $path)) { continue }
        if (Test-Reparse $path) { continue }
        if ($backup.ContainsKey($l)) {
            $current = Get-Sha256 $path
            if ($current -ne $backup[$l].sha256) { [void]$conflicts.Add($l) }
            elseif (Test-File (Join-Rel $filesDir $l)) { [void]$lfsDuplicates.Add($l) }
            else { [void]$lfsMove.Add(@{ Path = $l; Recorded = $true }) }
        } else {
            [void]$lfsMove.Add(@{ Path = $l; Recorded = $false })
        }
    }
    if ($patchMode -and $conflicts.Count -gt 0) {
        Fail $EXIT_PATCH ('Estos archivos del juego no son los de la ' + $Script:Patch.Manifest.from_version + ' instalada: ' + (($conflicts | Select-Object -First 5) -join ', ') + ' (' + $conflicts.Count + ' en total). Usa el paquete completo: desinstala y vuelve a instalar. No se cambió nada.') `
            ('These game files are not the installed ' + $Script:Patch.Manifest.from_version + ' ones: ' + (($conflicts | Select-Object -First 5) -join ', ') + ' (' + $conflicts.Count + ' in all). Use the full package: uninstall and install again. Nothing was changed.')
    }
    if ($conflicts.Count -gt 0) {
        Fail $EXIT_STATE ('Estos archivos cambiaron desde la instalación anterior y no son ni del mod ni los originales respaldados: ' + (($conflicts | Select-Object -First 5) -join ', ') + '. Desinstala primero. No se cambió nada.') `
            ('These files changed since the previous install and are neither the mod''s nor the backed-up originals: ' + (($conflicts | Select-Object -First 5) -join ', ') + '. Uninstall first. Nothing was changed.')
    }

    $toCopy = @($planFiles | Where-Object { -not $_.Done })
    foreach ($f in $toCopy) {
        if (-not $f.Source -or -not (Test-File $f.Source) -or (Get-Length $f.Source) -ne $f.Bytes) {
            Fail $EXIT_PACKAGE ('Falta el archivo pendiente ' + $f.Path + '. Extrae la parte ' + $f.Part + ' de la misma versión y repite. No se cambió nada.') `
                ('Pending file missing: ' + $f.Path + '. Extract part ' + $f.Part + ' of the same version and retry. Nothing was changed.')
        }
    }
    $needBytes = [double]0
    $largest = [double]0
    foreach ($f in $toCopy) { $needBytes += $f.OutputBytes; if ($f.OutputBytes -gt $largest) { $largest = $f.OutputBytes } }
    foreach ($f in $needBackup) { $needBytes += (Get-Length $f.Target) }
    $needBytes += $largest + 512MB
    $free = $null
    try { $free = (New-Object System.IO.DriveInfo([System.IO.Path]::GetPathRoot($game))).AvailableFreeSpace } catch { $free = $null }
    if ($null -ne $free -and $toCopy.Count -gt 0 -and $free -lt $needBytes) {
        Fail $EXIT_SPACE ('Falta espacio en ' + [System.IO.Path]::GetPathRoot($game) + ': se necesitan ' + (Format-GB $needBytes) + ' (archivos y respaldo) y hay ' + (Format-GB $free) + ' libres. No se cambió nada.') `
            ('Not enough space on ' + [System.IO.Path]::GetPathRoot($game) + ': ' + (Format-GB $needBytes) + ' needed (files and backup), ' + (Format-GB $free) + ' free. Nothing was changed.')
    }

    $copyBytes = [double]0
    foreach ($f in $toCopy) { $copyBytes += $f.OutputBytes }
    Write-Host ''
    Say ('Hyrule Evil 4 ' + $version + ' - partes: ' + ($selected -join ', ')) ('Hyrule Evil 4 ' + $version + ' - parts: ' + ($selected -join ', ')) 'Cyan'
    Say ('  Archivos a copiar: ' + $toCopy.Count + ' de ' + $planFiles.Count + ' (' + (Format-GB $copyBytes) + ')') ('Files to copy: ' + $toCopy.Count + ' of ' + $planFiles.Count)
    Say ('  Originales a respaldar: ' + $needBackup.Count + ' copiados y ' + $lfsMove.Count + ' .lfs movidos a ' + $backupDir) ('Originals to back up: ' + $needBackup.Count + ' copied and ' + $lfsMove.Count + ' .lfs moved')
    $userIniCopy = $null
    if ($null -ne $userIni) {
        $userIniCopy = Join-Path $backupRoot ('link_runtime.ini.' + $Script:Patch.Manifest.from_version + '-user')
        if ($MergeUserIni) {
            if ((Test-File $userIniCopy) -and (Get-Sha256 $userIniCopy) -ne $userIni.Current) {
                # A short hash keeps the name well under MAX_PATH (Windows PowerShell 5.1 has no long paths).
                $userIniCopy += '.' + $userIni.Current.Substring(0, 12)
            }
            Say ('  Se conservan tus valores y comentarios del INI y se agregan las claves nuevas. Tu copia previa queda en ' + $userIniCopy + '.') `
                ('Your INI values and comments are preserved and missing keys are added. Your previous copy is kept as ' + $userIniCopy + '.') 'Yellow'
        } else {
            Say ('  Tu Bin32\link_runtime.ini tiene cambios tuyos: se guarda en ' + $userIniCopy + ' y se reemplaza por el de la ' + $version + ' (trae claves nuevas; vuelve a aplicar tus cambios).') `
                ('Your Bin32\link_runtime.ini has your own changes: it is kept as ' + $userIniCopy + ' and replaced by the ' + $version + ' one (it has new keys; apply your changes again).') 'Yellow'
        }
    }
    if ($DryRun) {
        Say 'Simulación: no se cambió nada.' 'Dry run: nothing was changed.' 'Green'
        exit 0
    }
    if (-not (Confirm-Step 'Instalar ahora?' 'Install now?')) {
        Fail $EXIT_CANCEL 'Cancelado. No se cambió nada.' 'Cancelled. Nothing was changed.'
    }

    # From here on every change is journaled before or right after it happens.
    [void][System.IO.Directory]::CreateDirectory($filesDir)
    $Script:JournalPath = Join-Path $backupDir 'journal.tsv'
    if ($null -ne $userIniCopy -and -not (Test-File $userIniCopy)) {
        [System.IO.File]::Copy($userIni.Target, $userIniCopy, $false)
        if ((Get-Sha256 $userIniCopy) -ne $userIni.Current) {
            Remove-FileIfPresent $userIniCopy
            Fail $EXIT_VERIFY 'No pude copiar tu link_runtime.ini. No se cambió nada más; vuelve a ejecutar el parche.' 'Could not copy your link_runtime.ini. Nothing else changed; run the patch again.'
        }
    }
    # The journal must also know the user's INI hash, or an interrupted patch would see it as foreign.
    if ($null -ne $userIni) { Add-Journal ('placed' + "`t" + $userIni.Path + "`t" + $userIni.Current + "`t" + (Get-Length $userIni.Target)) }
    $managed = @{}
    foreach ($p in $previous.Keys) { $managed[$p] = @{ path = $p; sha256 = $previous[$p].sha256; bytes = [long]$previous[$p].bytes; part = $previous[$p].part } }
    foreach ($f in $planFiles) { $managed[$f.Path] = @{ path = $f.Path; sha256 = $f.Sha; bytes = $f.Bytes; part = $f.Part } }
    $allParts = @(@($installedParts) + @($selected) | Sort-Object -Unique | Sort-Object { [array]::IndexOf($PartOrder, $_) })
    $started = Get-Date -Format 's'
    $stateObject = [ordered]@{
        schema = $StateSchema; name = 'Link in RE4'; version = $version; status = 'installing'
        backup_dir = $backupName; parts = $allParts; parts_this_run = @($selected); started = $started; finished = $null
        files = @($managed.Keys | Sort-Object | ForEach-Object { [ordered]@{ path = $_; sha256 = $managed[$_].sha256; bytes = $managed[$_].bytes; part = $managed[$_].part } })
        lfs_moved = @(); created_dirs = @()
    }
    if ($patchMode) { $stateObject.patched_from = $Script:Patch.Manifest.from_version }
    elseif ($null -ne $state -and $state.PSObject.Properties['patched_from'] -and $state.version -eq $version) { $stateObject.patched_from = $state.patched_from }
    if ($null -ne $mergedIni) {
        $stateObject.user_ini = [ordered]@{ path = $mergedIni.Path; sha256 = $mergedIni.OutputSha; bytes = $mergedIni.OutputBytes }
    } elseif ($null -ne $retainedIni) {
        $stateObject.user_ini = [ordered]@{ path = $retainedIni.Path; sha256 = $retainedIni.OutputSha; bytes = $retainedIni.OutputBytes }
    } elseif ($null -ne $state -and $state.PSObject.Properties['user_ini'] -and
              @($planFiles | Where-Object { $_.Path -eq 'Bin32/link_runtime.ini' -and -not $_.Done }).Count -eq 0) {
        $stateObject.user_ini = $state.user_ini
    }
    Write-JsonFile $statePath $stateObject

    Say 'Respaldando originales...' 'Backing up originals...'
    foreach ($f in $needBackup) {
        $saved = Join-Rel $filesDir $f.Path
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($saved))
        $temporary = $saved + $PartialSuffix
        [System.IO.File]::Copy($f.Target, $temporary, $true)
        $copyHash = Get-Sha256 $temporary
        if ($copyHash -ne $f.Current) {
            Remove-FileIfPresent $temporary
            Fail $EXIT_VERIFY ('La copia de respaldo de ' + $f.Path + ' no coincide con el original. El juego sigue sin cambios en ese archivo; vuelve a ejecutar el instalador.') `
                ('The backup copy of ' + $f.Path + ' does not match the original. That game file is unchanged; run the installer again.')
        }
        Remove-FileIfPresent $saved
        [System.IO.File]::Move($temporary, $saved)
        $length = Get-Length $saved
        Add-Journal ('loose' + "`t" + $f.Path + "`t" + $copyHash + "`t" + $length)
        $backup[$f.Path] = @{ kind = 'loose'; path = $f.Path; sha256 = $copyHash; bytes = $length }
        Test-Abort 'backup'
    }
    $moved = 0
    foreach ($l in $lfsMove) {
        $source = Join-Rel $game $l.Path
        $saved = Join-Rel $filesDir $l.Path
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($saved))
        if (-not $l.Recorded) {
            $hash = Get-Sha256 $source
            $length = Get-Length $source
            Add-Journal ('lfs' + "`t" + $l.Path + "`t" + $hash + "`t" + $length)
            $backup[$l.Path] = @{ kind = 'lfs'; path = $l.Path; sha256 = $hash; bytes = $length }
        }
        if (Test-File $saved) {
            if ((Get-Sha256 $saved) -ne $backup[$l.Path].sha256) {
                Fail $EXIT_STATE ('Ya hay un respaldo distinto de ' + $l.Path + '. Desinstala primero.') ('A different backup of ' + $l.Path + ' already exists. Uninstall first.')
            }
            Remove-FileIfPresent $source
        } else {
            [System.IO.File]::Move($source, $saved)
        }
        $moved += 1
        if ($moved % 200 -eq 0) { Write-Host ('  .lfs ' + $moved + '/' + $lfsMove.Count) }
        Test-Abort 'lfs'
    }
    foreach ($l in $lfsDuplicates) {
        # Steam restored a .lfs that the backup already holds byte for byte.
        Remove-FileIfPresent (Join-Rel $game $l)
    }

    Say 'Copiando archivos del mod...' 'Copying the mod files...'
    $copied = 0
    foreach ($f in $toCopy) {
        New-GameDirectory ([System.IO.Path]::GetDirectoryName($f.Target))
        $temporary = $f.Target + $PartialSuffix
        if ($f.ContainsKey('MergeContent')) { [System.IO.File]::WriteAllBytes($temporary, $f.MergeContent) }
        else { [System.IO.File]::Copy($f.Source, $temporary, $true) }
        $hash = Get-Sha256 $temporary
        $attempt = 1
        while ($hash -ne $f.OutputSha) {
            Remove-FileIfPresent $temporary
            $sourceHash = Get-Sha256 $f.Source
            if ($sourceHash -ne $f.Sha) {
                Fail $EXIT_VERIFY ('El archivo del paquete ' + $f.Path + ' está dañado. Vuelve a extraer el .zip y ejecuta otra vez el instalador.') `
                    ('Package file ' + $f.Path + ' is damaged. Extract the .zip again and run the installer again.')
            }
            # The package file is fine: the copy itself came out wrong (disk or memory). Try again twice.
            Say ('La copia de ' + $f.Path + ' salió distinta (intento ' + $attempt + ', sha256 ' + $hash + '); se repite.') `
                ('The copy of ' + $f.Path + ' came out different (attempt ' + $attempt + ', sha256 ' + $hash + '); retrying.') 'Yellow'
            if ($attempt -ge 3) {
                Fail $EXIT_VERIFY ('La copia de ' + $f.Path + ' salió distinta 3 veces aunque el archivo del paquete está bien: revisa el disco o la memoria y vuelve a ejecutar el instalador.') `
                    ('The copy of ' + $f.Path + ' came out different 3 times although the package file is fine: check the disk or memory and run the installer again.')
            }
            $attempt += 1
            if ($f.ContainsKey('MergeContent')) { [System.IO.File]::WriteAllBytes($temporary, $f.MergeContent) }
            else { [System.IO.File]::Copy($f.Source, $temporary, $true) }
            $hash = Get-Sha256 $temporary
        }
        Remove-FileIfPresent $f.Target
        [System.IO.File]::Move($temporary, $f.Target)
        Add-Journal ('placed' + "`t" + $f.Path + "`t" + $hash + "`t" + $f.OutputBytes)
        $copied += 1
        if ($copied % 100 -eq 0) { Write-Host ('  ' + $copied + '/' + $toCopy.Count) }
        Test-Abort 'copy'
    }

    Say 'Verificando...' 'Verifying...'
    $bad = New-Object System.Collections.ArrayList
    # Each copied file was hashed before it replaced its target and each file already in place was hashed above;
    # here every target must exist with its size, no compressed twin may remain and every original is saved.
    foreach ($f in $planFiles) {
        if (-not (Test-File $f.Target) -or (Get-Length $f.Target) -ne $f.OutputBytes) { [void]$bad.Add($f.Path) }
    }
    foreach ($l in $lfsList) { if (Test-File (Join-Rel $game $l)) { [void]$bad.Add($l) } }
    foreach ($e in $backup.Values) {
        if (-not (Test-File (Join-Rel $filesDir $e.path))) { [void]$bad.Add('backup:' + $e.path) }
    }
    if ($bad.Count -gt 0) {
        Fail $EXIT_VERIFY ('La verificación falló en ' + $bad.Count + ' archivos (p. ej. ' + $bad[0] + '). Vuelve a ejecutar el instalador o desinstala.') `
            ('Verification failed on ' + $bad.Count + ' files (e.g. ' + $bad[0] + '). Run the installer again or uninstall.')
    }

    $journal = Read-Journal $Script:JournalPath
    $entries = @($backup.Keys | Sort-Object | ForEach-Object { [ordered]@{ kind = $backup[$_].kind; path = $_; sha256 = $backup[$_].sha256; bytes = $backup[$_].bytes } })
    Write-JsonFile $backupJson ([ordered]@{ schema = $BackupSchema; version = $version; created = $started; entries = $entries })
    $stateObject.status = 'complete'
    $stateObject.finished = Get-Date -Format 's'
    $stateObject.lfs_moved = @($backup.Values | Where-Object { $_.kind -eq 'lfs' } | ForEach-Object { $_.path } | Sort-Object)
    $stateObject.created_dirs = @($journal.Dirs | Sort-Object -Unique)
    Write-JsonFile $statePath $stateObject

    foreach ($name in @('uninstall.ps1', 'Uninstall-LinkInRE4.bat')) {
        $source = Join-Path $PSScriptRoot $name
        if (Test-File $source) { [System.IO.File]::Copy($source, (Join-Path $backupRoot $name), $true) }
    }

    Write-Host ''
    Say ('Listo: Link in RE4 ' + $version + ' quedó instalado (partes: ' + ($allParts -join ', ') + ').') ('Done: Link in RE4 ' + $version + ' is installed (parts: ' + ($allParts -join ', ') + ').') 'Green'
    Say ('Los originales quedaron en ' + $backupRoot + '. Para desinstalar ejecuta Uninstall-LinkInRE4.bat (hay una copia en esa carpeta).') `
        ('The originals are in ' + $backupRoot + '. To uninstall run Uninstall-LinkInRE4.bat (a copy is in that folder).')
    exit 0
}

if ($LibraryOnly) { return }

try {
    Invoke-Install
} catch {
    Write-Host ''
    Write-Host ('ERROR inesperado: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host ('Unexpected ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host ('  ' + $_.InvocationInfo.PositionMessage) -ForegroundColor DarkGray
    Write-Host 'Vuelve a ejecutar el instalador para completar o el desinstalador para deshacer.' -ForegroundColor Yellow
    Write-Host 'Run the installer again to complete, or the uninstaller to undo.' -ForegroundColor Yellow
    exit 1
}
