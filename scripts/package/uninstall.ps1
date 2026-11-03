<#
.SYNOPSIS
    Desinstala Link in RE4 y devuelve cada original respaldado.
    Uninstalls Link in RE4 and puts every backed-up original back.

.DESCRIPTION
    Windows PowerShell 5.1, no external modules. Run it through Uninstall-LinkInRE4.bat, from the package folder
    or from the copy the installer leaves in <game>\LinkInRE4_Backup.

    Reads <game>\LinkInRE4_Backup\installed.json and the backup's backup.json and journal.tsv (so an interrupted
    install or uninstall can be undone too). Before changing anything it checks every backed-up original against
    its sha256. Then it deletes each mod file whose hash is still the mod's (a file changed since the install is
    left in place with a warning; an edited Bin32\link_runtime.ini moves to LinkInRE4_Backup as
    link_runtime.ini.<hash>-user), moves every original back (.lfs files and loose originals), checks each one
    against backup.json, removes the folders the installer created and, when everything is back, the backup and
    the updater's snapshots (updater-transaction, updater-history-*). With warnings or a failed restore they stay.

    Exit codes: 0 ok (or nothing installed), 1 unexpected error, 3 game folder not found, 7 game running,
    8 inconsistent state, 9 a backup is missing or damaged (nothing changed) or a restored file does not match,
    10 cancelled, 11 finished with warnings (files left in place; the backup folder is kept).

.PARAMETER GamePath
    Carpeta del juego. Si falta: la carpeta padre cuando el script está en LinkInRE4_Backup, o la de Steam.
    Game folder. If omitted: the parent folder when the script sits in LinkInRE4_Backup, or Steam's.
.PARAMETER Yes
    No preguntar / do not ask for confirmation.
.PARAMETER IgnoreRunningGame
    Only for test copies of the game folder: skip the check that RE4 is closed.
#>
[CmdletBinding()]
param(
    [string]$GamePath = '',
    [switch]$Yes,
    [switch]$IgnoreRunningGame
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$StateSchema = 'link-in-re4-installed-1'
$BackupFolderName = 'LinkInRE4_Backup'
$PartialSuffix = '.lir4-partial'
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Script:AbortCounts = @{}

$EXIT_GAME = 3; $EXIT_RUNNING = 7; $EXIT_STATE = 8; $EXIT_VERIFY = 9; $EXIT_CANCEL = 10; $EXIT_WARNINGS = 11

# ------------------------------------------------------------------ output

function Say([string]$Es, [string]$En, [string]$Color = 'Gray') {
    Write-Host $Es -ForegroundColor $Color
    if ($En) { Write-Host ('  ' + $En) -ForegroundColor DarkGray }
}

function Warn([string]$Es, [string]$En) { Say ('AVISO: ' + $Es) ('WARNING: ' + $En) 'Yellow' }

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

function Test-File([string]$Path) { return [System.IO.File]::Exists($Path) }

function Test-Reparse([string]$Path) {
    if (-not ([System.IO.File]::Exists($Path) -or [System.IO.Directory]::Exists($Path))) { return $false }
    $attributes = [System.IO.File]::GetAttributes($Path)
    return (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
}

# A Steam folder moved with a junction is common; follow such links once, up front,
# so the game root is the real folder and Get-SafePath can keep rejecting every link.
function Get-CanonicalDirectory([string]$Path, [int]$Depth = 0) {
    if ($Depth -gt 20) { throw 'Demasiados enlaces en la ruta del juego / too many directory links in the game path.' }
    $full = [IO.Path]::GetFullPath($Path)
    $current = [IO.Path]::GetPathRoot($full)
    foreach ($piece in @($full.Substring($current.Length) -split '[\\/]' | Where-Object { $_ })) {
        $current = Join-Path $current $piece
        if (-not (Test-Reparse $current)) { continue }
        $item = Get-Item -LiteralPath $current -Force
        $targets = @()
        if ($item.PSObject.Properties['Target']) { $targets = @($item.Target) }
        if (-not $targets.Count -or -not $targets[0]) { throw "No pude seguir el enlace de la ruta del juego / could not resolve a link in the game path: $current" }
        $target = [string]$targets[0]
        if (-not [IO.Path]::IsPathRooted($target)) { $target = Join-Path ([IO.Path]::GetDirectoryName($current)) $target }
        $Script:FollowedGameLink = $true
        $current = Get-CanonicalDirectory $target ($Depth + 1)
    }
    return $current.TrimEnd('\', '/')
}

function Get-GameRoot([string]$Path) {
    $alias = (Resolve-Path -LiteralPath $Path).ProviderPath.TrimEnd('\')
    $Script:FollowedGameLink = $false
    $real = Get-CanonicalDirectory $alias
    if ($Script:FollowedGameLink) { Say ('Carpeta real (por un enlace): ' + $real) ('Real folder (through a link): ' + $real) }
    return $real
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

# Same format as install.ps1: loose|lfs <path> <sha256> <bytes>, placed <path> <sha256> <bytes>, dir <path>.
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

# The updater's rollback snapshot (updater-transaction, about 1.3 GB on a full chain) and the snapshots it archived
# (updater-history-<guid>) only fit the install that was just removed. Called only after every original is back.
function Remove-UpdaterLeftovers([string]$BackupRoot) {
    if (-not [System.IO.Directory]::Exists($BackupRoot)) { return }
    foreach ($dir in @([System.IO.Directory]::GetDirectories($BackupRoot))) {
        $name = [System.IO.Path]::GetFileName($dir)
        if ($name -ne 'updater-transaction' -and $name -notmatch '^updater-history-[0-9a-f]{32}$') { continue }
        try {
            [void](Get-SafePath $BackupRoot $name)
            Assert-SafeBackupTree $dir
            [System.IO.Directory]::Delete($dir, $true)
        } catch {
            Warn ('No pude borrar ' + $dir + '; puedes borrarla a mano.') ('Could not delete ' + $dir + '; you can delete it by hand.')
        }
    }
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
            Fail $EXIT_GAME ('No encontré BIO4\ y Bin32\bio4.exe en: ' + $GamePath) ('BIO4\ and Bin32\bio4.exe not found in: ' + $GamePath)
        }
        return (Get-GameRoot $GamePath)
    }
    $parent = Split-Path -Parent $PSScriptRoot
    if ((Split-Path -Leaf $PSScriptRoot) -eq $BackupFolderName -and (Test-GameFolder $parent)) {
        return (Get-GameRoot $parent)
    }
    $found = Find-Game
    if ($found) {
        Say ('Juego encontrado: ' + $found) ('Game found: ' + $found)
        if (-not (Confirm-Step 'Usar esta carpeta?' 'Use this folder?')) { $found = $null }
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
    return (Get-GameRoot $found)
}

# Only process names are read (no handles or memory of the game).
function Test-GameRunning {
    if ($IgnoreRunningGame) { return $false }
    return (@(Get-Process -Name 'bio4' -ErrorAction SilentlyContinue).Count -gt 0)
}

# ------------------------------------------------------------------ main

function Invoke-Uninstall {
    Say 'Link in RE4 - desinstalador' 'Link in RE4 - uninstaller' 'Cyan'
    $game = Resolve-Game
    Say ('Carpeta del juego: ' + $game) ('Game folder: ' + $game)
    $backupRoot = Join-Path $game $BackupFolderName
    $statePath = Join-Path $backupRoot 'installed.json'
    try {
        [void](Get-SafePath $game 'Bin32/bio4.exe')
        [void](Get-SafePath $game ($BackupFolderName + '/installed.json'))
        Assert-SafeBackupTree $backupRoot
    } catch { Fail $EXIT_STATE $_.Exception.Message 'Unsafe game or backup path. Nothing was changed.' }
    if (-not (Test-File $statePath)) {
        $sets = @()
        if ([System.IO.Directory]::Exists($backupRoot)) { $sets = @([System.IO.Directory]::GetDirectories($backupRoot) | Where-Object { [IO.Path]::GetFileName($_) -notlike 'updater-*' }) }
        if ($sets.Count -gt 0) {
            Fail $EXIT_STATE ('Falta installed.json pero hay respaldos en ' + $backupRoot + '. No se cambió nada; revisa esa carpeta a mano.') `
                ('installed.json is missing but ' + $backupRoot + ' holds backups. Nothing was changed; check that folder by hand.')
        }
        Say 'Link in RE4 no está instalado en esta carpeta. No hay nada que hacer.' 'Link in RE4 is not installed in this folder. Nothing to do.' 'Green'
        exit 0
    }
    try { $state = Read-Json $statePath } catch { $state = $null }
    if ($null -eq $state -or $state.schema -ne $StateSchema) {
        Fail $EXIT_STATE ('No pude leer ' + $statePath + '. No se cambió nada.') ('Cannot read ' + $statePath + '. Nothing was changed.')
    }
    if (Test-GameRunning) {
        Fail $EXIT_RUNNING 'Resident Evil 4 está abierto. Ciérralo y vuelve a ejecutar el desinstalador.' 'Resident Evil 4 is running. Close it and run the uninstaller again.'
    }
    try { $backupDir = Get-SafePath $backupRoot $state.backup_dir }
    catch { Fail $EXIT_STATE $_.Exception.Message 'Unsafe backup directory. Nothing was changed.' }
    $filesDir = Join-Path $backupDir 'files'
    $journalPath = Join-Path $backupDir 'journal.tsv'
    $backupJson = Join-Path $backupDir 'backup.json'
    if (-not [System.IO.Directory]::Exists($backupDir)) {
        Fail $EXIT_STATE ('Falta la carpeta de respaldo ' + $backupDir + '. No se cambió nada.') ('Backup folder ' + $backupDir + ' is missing. Nothing was changed.')
    }

    $journal = Read-Journal $journalPath
    $entries = @{}
    if (Test-File $backupJson) {
        foreach ($e in @((Read-Json $backupJson).entries)) {
            $entries[$e.path] = @{ kind = $e.kind; path = $e.path; sha256 = $e.sha256; bytes = [long]$e.bytes }
        }
    }
    foreach ($e in $journal.Backup.Values) { $entries[$e.path] = $e }
    # Paths the installer manages, with the hashes of its versions. A file the installer had not placed yet
    # (interrupted install) is an untouched original unless it already holds the mod's bytes.
    $ours = @{}
    $placed = @{}
    $complete = ($state.status -eq 'complete')
    foreach ($f in @($state.files)) {
        if (-not $ours.ContainsKey($f.path)) { $ours[$f.path] = @{} }
        $ours[$f.path][$f.sha256] = $true
        if ($complete) { $placed[$f.path] = $true }
    }
    foreach ($p in $journal.Placed.Keys) {
        if (-not $ours.ContainsKey($p)) { $ours[$p] = @{} }
        foreach ($h in $journal.Placed[$p].Keys) { $ours[$p][$h] = $true }
        $placed[$p] = $true
    }

    try {
        foreach ($p in $ours.Keys) {
            [void](Get-SafePath $game $p)
            [void](Get-SafePath $game ($p + $PartialSuffix))
        }
        foreach ($e in $entries.Values) {
            [void](Get-SafePath $game $e.path)
            [void](Get-SafePath $filesDir $e.path)
        }
        foreach ($d in @($journal.Dirs) + @($state.created_dirs)) {
            if ($d) { [void](Get-SafePath $game $d) }
        }
    } catch { Fail $EXIT_STATE $_.Exception.Message 'Unsafe restore path. Nothing was changed.' }

    Say ('Versión instalada: ' + $state.version + ' (' + $state.status + '), ' + $ours.Count + ' archivos del mod, ' + $entries.Count + ' originales respaldados.') `
        ('Installed version: ' + $state.version + ' (' + $state.status + '), ' + $ours.Count + ' mod files, ' + $entries.Count + ' backed-up originals.')
    if (-not (Confirm-Step 'Desinstalar Link in RE4 y devolver los originales?' 'Uninstall Link in RE4 and put the originals back?')) {
        Fail $EXIT_CANCEL 'Cancelado. No se cambió nada.' 'Cancelled. Nothing was changed.'
    }

    # Every original must be restorable before any file is touched.
    Say 'Comprobando el respaldo...' 'Checking the backup...'
    $restore = New-Object System.Collections.ArrayList
    $broken = New-Object System.Collections.ArrayList
    foreach ($e in $entries.Values) {
        $saved = Join-Rel $filesDir $e.path
        if (Test-File $saved) {
            if ((Get-Sha256 $saved) -ne $e.sha256) { [void]$broken.Add($e.path) } else { [void]$restore.Add($e) }
        } else {
            $target = Join-Rel $game $e.path
            if (-not ((Test-File $target) -and (Get-Sha256 $target) -eq $e.sha256)) { [void]$broken.Add($e.path) }
        }
    }
    if ($broken.Count -gt 0) {
        Fail $EXIT_VERIFY ('Faltan o están dañados ' + $broken.Count + ' originales del respaldo (p. ej. ' + $broken[0] + '). No se cambió nada. Alternativa: borra Bin32\dinput8.dll y Bin32\link_* y usa Verificar integridad en Steam.') `
            ('' + $broken.Count + ' backed-up originals are missing or damaged (e.g. ' + $broken[0] + '). Nothing was changed. Fallback: delete Bin32\dinput8.dll and Bin32\link_* and run Verify integrity in Steam.')
    }

    # An interrupted updater snapshot fits only this install: a later updater must never restore it.
    # An unreadable record cannot be restored either, so it never blocks the uninstall.
    $transactionJson = Join-Path $backupRoot 'updater-transaction\transaction.json'
    try { $transaction = Read-Json $transactionJson } catch { $transaction = $null }
    if ($null -ne $transaction -and $transaction.PSObject.Properties['status'] -and $transaction.status -eq 'active') {
        $transaction.status = 'uninstalled'
        try { Write-JsonFile $transactionJson $transaction } catch { Warn 'No pude cerrar el registro del actualizador.' 'Could not retire the updater record.' }
    }

    Say 'Quitando los archivos del mod...' 'Removing the mod files...'
    $kept = @{}
    $removed = 0
    foreach ($p in @($ours.Keys | Sort-Object)) {
        $target = Join-Rel $game $p
        Remove-FileIfPresent ($target + $PartialSuffix)
        if (-not (Test-File $target)) { continue }
        if (Test-Reparse $target) {
            Warn ('Es un enlace, no lo toco: ' + $p) ('It is a link, left alone: ' + $p)
            $kept[$p] = $true
            continue
        }
        $current = Get-Sha256 $target
        if ($ours[$p].ContainsKey($current)) {
            Remove-FileIfPresent $target
            $removed += 1
            if ($removed % 200 -eq 0) { Write-Host ('  ' + $removed + '/' + $ours.Count) }
            Test-Abort 'remove'
        } elseif ($entries.ContainsKey($p) -and $current -eq $entries[$p].sha256) {
            continue
        } elseif (-not $placed.ContainsKey($p)) {
            continue
        } elseif ($p -eq 'Bin32/link_runtime.ini') {
            # The player's settings (README section 2) leave the game folder so the uninstall can finish.
            $copy = Join-Path $backupRoot ('link_runtime.ini.' + $current.Substring(0, 12) + '-user')
            for ($n = 2; (Test-File $copy) -and (Get-Sha256 $copy) -ne $current; $n++) { $copy = Join-Path $backupRoot ('link_runtime.ini.' + $current.Substring(0, 12) + '.' + $n + '-user') }
            if (Test-File $copy) { Remove-FileIfPresent $target } else { [System.IO.File]::Move($target, $copy) }
            Say ('Tus ajustes de ' + $p + ' quedan en ' + $copy + '.') ('Your ' + $p + ' settings are kept as ' + $copy + '.') 'Yellow'
            continue
        } else {
            Warn ('Cambió desde la instalación; lo dejo como está: ' + $p) ('Changed since the install; left as it is: ' + $p)
            $kept[$p] = $true
        }
    }

    Say 'Devolviendo los originales...' 'Putting the originals back...'
    $keepBackup = $false
    $restored = 0
    foreach ($e in $restore) {
        $target = Join-Rel $game $e.path
        $saved = Join-Rel $filesDir $e.path
        if ($kept.ContainsKey($e.path)) {
            Warn ('No devuelvo el original de ' + $e.path + ' (queda en el respaldo).') ('Original of ' + $e.path + ' not restored (kept in the backup).')
            $keepBackup = $true
            continue
        }
        if (Test-File $target) {
            if ((Get-Sha256 $target) -eq $e.sha256) { Remove-FileIfPresent $saved; continue }
            Warn ('Hay otro archivo en ' + $e.path + '; el original queda en el respaldo.') ('Another file sits at ' + $e.path + '; the original stays in the backup.')
            $keepBackup = $true
            continue
        }
        [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($target))
        [System.IO.File]::Move($saved, $target)
        $restored += 1
        if ($restored % 200 -eq 0) { Write-Host ('  ' + $restored + '/' + $restore.Count) }
        Test-Abort 'restore'
    }

    Say 'Verificando...' 'Verifying...'
    $bad = New-Object System.Collections.ArrayList
    foreach ($e in $entries.Values) {
        if ($kept.ContainsKey($e.path)) { continue }
        $target = Join-Rel $game $e.path
        if (-not (Test-File $target) -or (Get-Sha256 $target) -ne $e.sha256) {
            if (-not (Test-File (Join-Rel $filesDir $e.path))) { [void]$bad.Add($e.path) }
        }
    }
    if ($bad.Count -gt 0) {
        Fail $EXIT_VERIFY ('' + $bad.Count + ' originales no coinciden después de devolverlos (p. ej. ' + $bad[0] + '). Usa Verificar integridad en Steam.') `
            ('' + $bad.Count + ' originals do not match after restoring them (e.g. ' + $bad[0] + '). Run Verify integrity in Steam.')
    }

    $dirs = @(@($journal.Dirs) + @($state.created_dirs) | Where-Object { $_ } | Sort-Object -Unique | Sort-Object { $_.Length } -Descending)
    foreach ($d in $dirs) {
        $full = Join-Rel $game $d
        if ([System.IO.Directory]::Exists($full) -and @([System.IO.Directory]::GetFileSystemEntries($full)).Count -eq 0) {
            [System.IO.Directory]::Delete($full)
        }
    }

    # A *.lir4-partial in the backup is an unfinished copy of an original that never left the game.
    $leftovers = @()
    if ([System.IO.Directory]::Exists($filesDir)) {
        foreach ($partial in [System.IO.Directory]::GetFiles($filesDir, '*' + $PartialSuffix, [System.IO.SearchOption]::AllDirectories)) {
            Remove-FileIfPresent $partial
        }
        $leftovers = @([System.IO.Directory]::GetFiles($filesDir, '*', [System.IO.SearchOption]::AllDirectories))
    }
    if ($keepBackup -or $kept.Count -gt 0 -or $leftovers.Count -gt 0) {
        $state | Add-Member -NotePropertyName 'uninstall_kept' -NotePropertyValue @($kept.Keys | Sort-Object) -Force
        Write-JsonFile $statePath $state
        Write-Host ''
        Warn ('Desinstalación terminada con avisos. Quedan ' + $leftovers.Count + ' archivos en ' + $backupDir + '; no los borres hasta revisarlos.') `
            ('Uninstall finished with warnings. ' + $leftovers.Count + ' files remain in ' + $backupDir + '; keep them until you have checked them.')
        exit $EXIT_WARNINGS
    }

    foreach ($name in @('journal.tsv', 'backup.json', 'backup.json.writing')) { Remove-FileIfPresent (Join-Path $backupDir $name) }
    if ([System.IO.Directory]::Exists($filesDir)) { [System.IO.Directory]::Delete($filesDir, $true) }
    if (@([System.IO.Directory]::GetFileSystemEntries($backupDir)).Count -eq 0) { [System.IO.Directory]::Delete($backupDir) }
    Remove-FileIfPresent $statePath
    foreach ($name in @('installed.json.writing', 'uninstall.ps1', 'Uninstall-LinkInRE4.bat')) { Remove-FileIfPresent (Join-Path $backupRoot $name) }
    # Every original is back and verified: the updater's snapshots can no longer be restored over anything.
    Remove-UpdaterLeftovers $backupRoot
    if ([System.IO.Directory]::Exists($backupRoot) -and @([System.IO.Directory]::GetFileSystemEntries($backupRoot)).Count -eq 0) {
        try { [System.IO.Directory]::Delete($backupRoot) } catch { Warn ('Puedes borrar la carpeta vacía ' + $backupRoot + '.') ('You can delete the empty folder ' + $backupRoot + '.') }
    }
    Write-Host ''
    Say ('Listo: se quitaron ' + $removed + ' archivos del mod y se devolvieron ' + $entries.Count + ' originales. El juego quedó como antes de instalar Link in RE4.') `
        ('Done: ' + $removed + ' mod files removed and ' + $entries.Count + ' originals put back. The game is as it was before Link in RE4.') 'Green'
    exit 0
}

try {
    Invoke-Uninstall
} catch {
    Write-Host ''
    Write-Host ('ERROR inesperado: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host ('Unexpected ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host ('  ' + $_.InvocationInfo.PositionMessage) -ForegroundColor DarkGray
    Write-Host 'Puedes volver a ejecutar el desinstalador: retoma donde quedó.' -ForegroundColor Yellow
    Write-Host 'You can run the uninstaller again: it resumes where it stopped.' -ForegroundColor Yellow
    exit 1
}
