<#
One-click local patch orchestration. Windows PowerShell 5.1, no network.
The installer owns install validation and writes; this wrapper owns a rollback
snapshot covering the entire selected chain. -Headless requires an explicit game.
#>
[CmdletBinding()]
param(
    [string]$BundlePath = $PSScriptRoot,
    [string]$GamePath = '',
    [string]$ResultPath = '',
    [switch]$Headless,
    [switch]$LibraryOnly
)
$entryOptions = @{ BundlePath=$BundlePath; GamePath=$GamePath; ResultPath=$ResultPath; Headless=[bool]$Headless }
$onlyLibrary = [bool]$LibraryOnly
$updaterHome = $PSScriptRoot
. (Join-Path $PSScriptRoot 'install.ps1') -LibraryOnly
$Script:UpdaterHome = $updaterHome
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-UpdateRelative([string]$Rel) {
    if (-not $Rel -or $Rel -match '[\\:<>"|?*\x00-\x1f]' -or $Rel.StartsWith('/') -or
        @($Rel.Split('/') | Where-Object { $_ -eq '.' -or $_ -eq '..' -or -not $_ -or
            $_ -match '[. ]$' -or $_ -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)' }).Count) {
        throw "Ruta insegura en el parche: $Rel. Pide otra copia del actualizador."
    }
}

function Get-UpdatePath([string]$Root, [string]$Rel) {
    Assert-UpdateRelative $Rel
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $full = [IO.Path]::GetFullPath((Join-Rel $rootFull $Rel))
    if (-not $full.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Ruta fuera de destino.' }
    $cursor = $full
    while ($cursor) {
        if (Test-Reparse $cursor) { throw "La ruta contiene un enlace o junction: $cursor. Usa una instalación sin enlaces." }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
        if (-not $cursor) { break }
    }
    return $full
}

function Get-UpdateFreeSpace([string]$Path) {
    return (New-Object IO.DriveInfo([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Path)))).AvailableFreeSpace
}

function Expand-UpdateArchive([string]$Archive, [string]$Destination) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $seen = @{}; $total = [long]0
        if ($zip.Entries.Count -gt 20000) { throw 'El parche tiene demasiados archivos.' }
        foreach ($entry in $zip.Entries) {
            $rel = $entry.FullName.TrimEnd('/')
            Assert-UpdateRelative $rel
            if (-not $rel.StartsWith('LinkInRE4/', [StringComparison]::OrdinalIgnoreCase) -and $rel -ne 'LinkInRE4') {
                throw "Contenido fuera de LinkInRE4: $rel"
            }
            if ($seen.ContainsKey($rel)) { throw "Archivo duplicado en el ZIP: $rel" }
            $seen[$rel] = $true
            if ($entry.PSObject.Properties['ExternalAttributes'] -and ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000)) { throw "Enlace dentro del ZIP: $rel" }
            $total += $entry.Length
            if ($total -gt 4GB) { throw 'El parche supera el límite de extracción de 4 GB.' }
        }
        if ((Get-UpdateFreeSpace $Destination) -lt ($total + 32MB)) { throw 'Falta espacio para extraer el parche. Libera espacio y vuelve a intentarlo.' }
        foreach ($entry in $zip.Entries) {
            $path = Get-UpdatePath $Destination $entry.FullName.TrimEnd('/')
            if ($entry.FullName.EndsWith('/')) { [void][IO.Directory]::CreateDirectory($path); continue }
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
            $inputStream = $entry.Open()
            try {
                $outputStream = [IO.File]::Open($path, [IO.FileMode]::CreateNew)
                try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
            } finally { $inputStream.Dispose() }
        }
    } finally { $zip.Dispose() }
}

function Read-UpdateChain([string]$Bundle, [string]$Work) {
    $config = Read-Json (Get-UpdatePath $Bundle 'updater.json')
    if ($config.schema -ne 'link-in-re4-updater-1') { throw 'Actualizador desconocido. Pide otra copia.' }
    if ($config.PSObject.Properties['remote_enabled'] -and $config.remote_enabled) {
        throw 'La descarga remota aún no está habilitada. Usa el archivo que te compartieron.'
    }
    $patches = @($config.patches)
    if ($patches.Count -lt 1 -or $patches.Count -gt 16) { throw 'La cadena debe contener entre 1 y 16 parches.' }
    $chain = New-Object Collections.ArrayList
    $prior = $null; $seenVersions = @{}; $index = 0
    foreach ($patch in $patches) {
        if ($patch.from_version -notmatch '^\d+\.\d+\.\d+$' -or $patch.version -notmatch '^\d+\.\d+\.\d+$' -or
            [version]$patch.version -le [version]$patch.from_version) { throw 'Versiones inválidas en la cadena.' }
        if ($prior -and $patch.from_version -ne $prior.Manifest.version) { throw 'Falta un parche intermedio en la cadena. Pide una cadena completa.' }
        if ($seenVersions.ContainsKey($patch.version)) { throw 'Versión repetida en la cadena.' }
        $seenVersions[$patch.version] = $true
        $archive = Get-UpdatePath $Bundle $patch.path
        if (-not (Test-File $archive) -or (Get-Length $archive) -ne [long]$patch.bytes -or
            (Get-Sha256 $archive) -ne $patch.sha256) { throw 'El ZIP del parche está incompleto o dañado. Pide otra copia del actualizador.' }
        $dir = Join-Path $Work ('patch-' + $index)
        [void][IO.Directory]::CreateDirectory($dir)
        Expand-UpdateArchive $archive $dir
        $root = Join-Path $dir 'LinkInRE4'
        $manifests = @([IO.Directory]::GetFiles($root, 'patch-*.json'))
        if ($manifests.Count -ne 1) { throw 'El ZIP debe contener exactamente un manifiesto de parche.' }
        $manifest = Read-Json $manifests[0]
        if ($manifest.schema -ne 'link-in-re4-patch-1' -or $manifest.from_version -ne $patch.from_version -or
            $manifest.version -ne $patch.version -or @($manifest.removed).Count) { throw 'El manifiesto no coincide con la cadena o elimina archivos no admitidos.' }
        $paths = @{}
        foreach ($part in $manifest.parts.PSObject.Properties) {
            if ($part.Name -notin @('core','sd','hd')) { throw 'Parte de paquete desconocida.' }
            $rows = @{}
            foreach ($f in @($part.Value.files)) {
                Assert-UpdateRelative $f.path
                if ($f.path -notmatch '^(Bin32|BIO4)/' -or $f.path -ieq 'Bin32/bio4.exe' -or
                    $paths.ContainsKey($f.path) -or $f.sha256 -notmatch '^[a-f0-9]{64}$' -or [long]$f.bytes -lt 0) {
                    throw "Archivo inválido o duplicado en el manifiesto: $($f.path)"
                }
                $paths[$f.path] = $true; $rows[$f.path] = $f
            }
            foreach ($lfs in @($part.Value.lfs_to_move)) {
                Assert-UpdateRelative $lfs
                if ($lfs -notmatch '^BIO4/.+\.lfs$') { throw 'Ruta comprimida inválida.' }
            }
            foreach ($c in @($part.Value.changed)) {
                if (-not $rows.ContainsKey($c)) { throw "Archivo cambiado sin registro: $c" }
                $src = Get-UpdatePath $root ('files/' + $c)
                if (-not (Test-File $src) -or (Get-Length $src) -ne [long]$rows[$c].bytes -or
                    (Get-Sha256 $src) -ne $rows[$c].sha256) { throw "Archivo dañado dentro del parche: $c. Pide otra copia." }
            }
            if ($prior) {
                $oldPart = $prior.Manifest.parts.PSObject.Properties[$part.Name]
                if (-not $oldPart) { throw 'Las partes de la cadena no coinciden.' }
                $oldRows = @{}
                foreach ($f in @($oldPart.Value.files)) { $oldRows[$f.path] = $f.sha256 }
                $fromRows = @($part.Value.from_files.PSObject.Properties)
                if ($oldRows.Count -ne $fromRows.Count) { throw 'Las bases de la cadena no coinciden.' }
                foreach ($f in $fromRows) {
                    if (-not $oldRows.ContainsKey($f.Name) -or $oldRows[$f.Name] -ne $f.Value) { throw 'Las bases de la cadena no coinciden.' }
                }
                if ($manifest.bio4_exe_sha256 -ne $prior.Manifest.bio4_exe_sha256) { throw 'Los parches requieren ejecutables distintos.' }
            }
        }
        $item = @{ Manifest=$manifest; Root=$root }
        [void]$chain.Add($item); $prior = $item; $index++
    }
    if ($config.from_version -ne $chain[0].Manifest.from_version -or $config.target_version -ne $chain[-1].Manifest.version) {
        throw 'La versión anunciada no coincide con los parches.'
    }
    return ,$chain
}

$Script:UpdateStatus = @{ RunDir=''; Key=''; Es=''; En=''; Told=$false }
$Script:UpdateEnglish = @{}

function ConvertTo-UpdateStatusLine([string]$Text) {
    # One field per line of the status file: a backslash and a line break travel escaped.
    return $Text.Replace('\', '\\').Replace("`r`n", '\n').Replace("`n", '\n').Replace("`r", '')
}

function Set-UpdateStatus([string]$Es, [string]$En, [int]$Percent = -1, [string]$State = 'work', [string]$Detail = '') {
    # The update runs hidden and can take many minutes. Each long phase is written for the launcher's window
    # (LINKINRE4_UPDATER_STATUS: marker, state, percent or -1, Spanish, English, detail, end) and, with its time, to
    # progress.log. States: work (a phase), hide (a dialog of this script is up) and ok, info or error (the final
    # message, which the window then shows by itself). $Script:UpdateStatus.Told says whether the window was told.
    # A failure here never stops the update. Nothing is returned: callers return their own report.
    $Script:UpdateStatus.Told = $false
    $esLine = ConvertTo-UpdateStatusLine $Es; $enLine = ConvertTo-UpdateStatusLine $En
    try {
        $key = $State + '|' + $esLine
        if ($Script:UpdateStatus.RunDir -and $key -ne $Script:UpdateStatus.Key) {
            [IO.File]::AppendAllText((Join-Path $Script:UpdateStatus.RunDir 'progress.log'),
                ((Get-Date).ToString('s') + "`t" + $State + "`t" + $esLine + "`t" + $enLine + "`r`n"), $Utf8)
        }
        $Script:UpdateStatus.Key = $key; $Script:UpdateStatus.Es = $Es; $Script:UpdateStatus.En = $En
    } catch { }
    try {
        if ($env:LINKINRE4_UPDATER_STATUS) {
            [IO.File]::WriteAllText($env:LINKINRE4_UPDATER_STATUS,
                ("LIR4STATUS1`n" + $State + "`n" + $Percent + "`n" + $esLine + "`n" + $enLine + "`n" +
                 (ConvertTo-UpdateStatusLine $Detail) + "`nend`n"), $Utf8)
            $Script:UpdateStatus.Told = $true
        }
    } catch { }
}

function Get-UpdateInstallerPercent([string]$Line) {
    # install.ps1 prints '  200/1600' while it checks and while it copies; '  .lfs 200/800' is not counted.
    if ($Line -match '^\s+(\d+)/(\d+)\s*$' -and [long]$Matches[2] -gt 0) {
        return [int][Math]::Min(100, [Math]::Floor(100 * [long]$Matches[1] / [long]$Matches[2]))
    }
    return -1
}

function Get-UpdateWriterMutexName([string]$Game) {
    $canonical = [IO.Path]::GetFullPath($Game).TrimEnd('\').ToLowerInvariant()
    $hashTool = [Security.Cryptography.SHA256]::Create()
    try { $key = [BitConverter]::ToString($hashTool.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical))).Replace('-', '') }
    finally { $hashTool.Dispose() }
    return ('Local\LinkInRE4-Installer-' + $key)
}

function Assert-NoUpdateWriter([string]$Game) {
    # The caller already owns the updater mutex. An old child can still be finishing after its
    # wrapper died; do not restore its transaction while that child owns the game writer mutex.
    $mutex = New-Object Threading.Mutex($false, (Get-UpdateWriterMutexName $Game))
    $owned = $false
    try {
        try { $owned = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned = $true }
        if (-not $owned) { throw 'El instalador anterior todavía está trabajando en esta carpeta. Espera a que termine y vuelve a ejecutar el actualizador.' }
    } finally {
        if ($owned) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}

function New-UpdateInstallerCommand([string]$Game, [string]$Package, [bool]$CheckOnly,
                                   [int]$OwnerId, [long]$OwnerStartTicks) {
    # PowerShell also ends a single-quoted string at U+2018, U+2019, U+201A and U+201B: double them too.
    $quote = { param($s) "'" + ($s -replace "['‘’‚‛]", '$0$0') + "'" }
    $checkLiteral = '$false'; if ($CheckOnly) { $checkLiteral = '$true' }
    # Capture parent identity before launch. A delayed child must check it after taking the writer
    # lock, otherwise it could start writing after a replacement wrapper has begun recovery.
    $command = '$installerPath=' + (& $quote (Join-Path $Script:UpdaterHome 'install.ps1')) +
        '; $installerGame=' + (& $quote $Game) + '; $installerPackage=' + (& $quote $Package) +
        '; $installerCheck=' + $checkLiteral + '; $writerName=' + (& $quote (Get-UpdateWriterMutexName $Game)) +
        '; $expectedOwnerId=' + $OwnerId + '; $expectedOwnerTicks=[long]' + $OwnerStartTicks + ";`r`n"
    $command += @'
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$writerMutex = New-Object Threading.Mutex($false, $writerName)
$ownsWriter = $false
try {
    try { $ownsWriter = $writerMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsWriter = $true }
    if (-not $ownsWriter) { throw 'Hay otro instalador trabajando en esta carpeta. Espera a que termine.' }
    $ownerProcess = $null
    try {
        $ownerProcess = [Diagnostics.Process]::GetProcessById($expectedOwnerId)
        if ($ownerProcess.HasExited -or $ownerProcess.StartTime.ToUniversalTime().Ticks -ne $expectedOwnerTicks) {
            throw 'Parent identity changed.'
        }
    } catch {
        throw 'El actualizador que inició esta instalación se cerró. Vuelve a ejecutar el actualizador para recuperar la instalación anterior.'
    } finally {
        if ($null -ne $ownerProcess) { $ownerProcess.Dispose() }
    }
    # Use the current trusted installer, never the script from a historical ZIP.
    # Only the check before the first write hashes the files a patch keeps. Read-UpdateChain proved that each
    # patch starts exactly where the previous one ends and every written file is verified as it is copied, so
    # hashing the whole install again for each link (10 to 32 GB per pass) added minutes and no protection.
    $LASTEXITCODE = 0
    & $installerPath -GamePath $installerGame -PackagePath $installerPackage -Yes -VerifyAll:$installerCheck -MergeUserIni -DryRun:$installerCheck
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine('ERROR: ' + $_.Exception.Message)
    exit 1
} finally {
    if ($ownsWriter) { $writerMutex.ReleaseMutex() }
    $writerMutex.Dispose()
}
'@
    return $command
}

function Invoke-UpdateInstaller([string]$Game, [string]$Package, [string]$Log, [bool]$CheckOnly) {
    $ownerProcess = [Diagnostics.Process]::GetCurrentProcess()
    try { $command = New-UpdateInstallerCommand $Game $Package $CheckOnly $ownerProcess.Id $ownerProcess.StartTime.ToUniversalTime().Ticks }
    finally { $ownerProcess.Dispose() }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $PSHOME 'powershell.exe'
    $info.Arguments = '-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ' +
        [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = New-Object Text.UTF8Encoding($false)
    $info.StandardErrorEncoding = New-Object Text.UTF8Encoding($false)
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        # stderr drains in the background; stdout is read line by line so the installer's counters move the bar.
        $errTask = $process.StandardError.ReadToEndAsync()
        $lines = New-Object 'Collections.Generic.List[string]'
        while ($null -ne ($line = $process.StandardOutput.ReadLine())) {
            $lines.Add($line)
            $percent = Get-UpdateInstallerPercent $line
            if ($percent -ge 0) { Set-UpdateStatus $Script:UpdateStatus.Es $Script:UpdateStatus.En $percent }
        }
        $process.WaitForExit()
        $output = ($lines -join "`r`n") + "`r`n" + $errTask.Result
        [IO.File]::WriteAllText($Log, $output, $Utf8)
        if ($process.ExitCode -ne 0) {
            # install.ps1 prints its refusal twice: 'ERROR: ' in Spanish, then 'ERROR: ' in English.
            $errorLine = @($output -split "`r?`n" | Where-Object { $_ -match '^ERROR:' } | Select-Object -First 2)
            if ($errorLine.Count) {
                if ($errorLine.Count -gt 1) { $Script:UpdateEnglish[$errorLine[0]] = $errorLine[1] }
                throw $errorLine[0]
            }
            throw ('El instalador se interrumpió (código ' + $process.ExitCode + '). Registro: ' + $Log)
        }
    } finally { $process.Dispose() }
}

function Get-UpdateSnapshotPaths($Chain, $State, [string]$Game) {
    Assert-UpdateRelative $State.backup_dir
    if ($State.backup_dir.Contains('/')) { throw 'Nombre de respaldo inválido.' }
    $base = 'LinkInRE4_Backup/' + $State.backup_dir
    $paths = @{}
    $originals=(Read-Journal (Get-UpdatePath $Game "$base/journal.tsv")).Backup
    $backupRecord=Get-UpdatePath $Game "$base/backup.json"
    if (Test-File $backupRecord) {
        foreach ($row in @((Read-Json $backupRecord).entries)) { $originals[$row.path]=$row }
    }
    foreach ($rel in @('LinkInRE4_Backup/installed.json', 'LinkInRE4_Backup/installed.json.writing',
        'LinkInRE4_Backup/uninstall.ps1', 'LinkInRE4_Backup/Uninstall-LinkInRE4.bat',
        "$base/journal.tsv", "$base/backup.json", "$base/backup.json.writing")) { $paths[$rel]=$true }
    foreach ($patch in $Chain) {
        $paths['LinkInRE4_Backup/link_runtime.ini.' + $patch.Manifest.from_version + '-user']=$true
        foreach ($part in @($State.parts)) {
            $info = $patch.Manifest.parts.PSObject.Properties[$part]
            if (-not $info) { throw "El parche no trae la parte instalada $part." }
            foreach ($rel in @($info.Value.changed)) {
                foreach ($p in @($rel, "$rel.lir4-partial", "$base/files/$rel.lir4-partial")) { $paths[$p]=$true }
                $savedRel="$base/files/$rel"
                if (-not (Test-File (Get-UpdatePath $Game $savedRel))) { $paths[$savedRel]=$true }
                elseif (-not $originals.ContainsKey($rel)) {
                    throw "Hay un respaldo sin registro para $rel. Conserva el respaldo y pide ayuda antes de actualizar."
                }
            }
            $twins = @($info.Value.lfs_to_move) + @($info.Value.files | ForEach-Object { $_.path + '.lfs' })
            foreach ($rel in $twins) {
                $paths[$rel]=$true
                # Existing compressed originals are immutable in install.ps1.
                # Record absence for newly moved originals; never duplicate the
                # historical texture backup just to apply a small runtime patch.
                $savedRel="$base/files/$rel"
                if (-not (Test-File (Get-UpdatePath $Game $savedRel))) { $paths[$savedRel]=$true }
            }
        }
    }
    return ,$paths
}

function Assert-UpdateSnapshotSpace([string]$Game, $Paths) {
    $bytes=[long]0
    foreach ($rel in $Paths.Keys) {
        $path=Get-UpdatePath $Game $rel
        if (Test-File $path) { $bytes+=Get-Length $path }
    }
    if ((Get-UpdateFreeSpace $Game) -lt ($bytes * 2 + 512MB)) {
        throw 'Falta espacio para respaldar y actualizar. Libera espacio y vuelve a intentarlo.'
    }
}

function New-UpdateSnapshot([string]$Game, [string]$Transaction, $Paths) {
    $rows = New-Object Collections.ArrayList; $dirs = @{}; $bytes = [long]0
    foreach ($rel in @($Paths.Keys | Sort-Object)) {
        $path = Get-UpdatePath $Game $rel
        if ([IO.Directory]::Exists($path)) { throw "Se esperaba un archivo, pero hay una carpeta: $rel" }
        $exists = Test-File $path
        $row = [ordered]@{ path=$rel; existed=$exists; hash=''; attributes=0; saved=('files/' + $rows.Count) }
        if ($exists) {
            $row.hash=Get-Sha256 $path; $row.attributes=[int][IO.File]::GetAttributes($path)
            $bytes += Get-Length $path
        }
        $parent = [IO.Path]::GetDirectoryName($path)
        while ($parent.Length -gt $Game.Length -and -not [IO.Directory]::Exists($parent)) {
            $dirs[$parent.Substring($Game.Length + 1).Replace('\','/')]=$true
            $parent=[IO.Path]::GetDirectoryName($parent)
        }
        [void]$rows.Add($row)
    }
    if ((Get-UpdateFreeSpace $Game) -lt ($bytes * 2 + 512MB)) { throw 'Falta espacio para respaldar y actualizar. Libera espacio y vuelve a intentarlo.' }
    [void][IO.Directory]::CreateDirectory((Join-Path $Transaction 'files'))
    foreach ($row in $rows) {
        if (-not $row.existed) { continue }
        $source=Get-UpdatePath $Game $row.path; $saved=Get-UpdatePath $Transaction $row.saved
        [IO.File]::Copy($source,$saved,$false)
        if ((Get-Sha256 $saved) -ne $row.hash) { throw 'No se pudo verificar el respaldo. El juego sigue sin cambios.' }
    }
    $iniBackups=@([IO.Directory]::GetFiles((Join-Path $Game 'LinkInRE4_Backup')) |
        ForEach-Object { [IO.Path]::GetFileName($_) } |
        Where-Object { $_ -match '^link_runtime\.ini\.\d+\.\d+\.\d+-user(\.([a-f0-9]{12}|[a-f0-9]{64}))?$' })
    $journal=[ordered]@{ schema='link-in-re4-update-transaction-1'; game=$Game; status='active'; rows=@($rows); new_dirs=@($dirs.Keys); ini_backups=$iniBackups }
    Write-JsonFile (Join-Path $Transaction 'transaction.json') $journal
}

function Restore-UpdateSnapshot([string]$Game, [string]$Transaction) {
    $journalPath=Get-UpdatePath $Transaction 'transaction.json'
    $journal=Read-Json $journalPath
    if ($journal.schema -ne 'link-in-re4-update-transaction-1' -or $journal.game -ne $Game) { throw 'El registro de recuperación no corresponde a esta instalación.' }
    # Validate every recovery copy before changing any game file.
    foreach ($row in @($journal.rows)) {
        [void](Get-UpdatePath $Game $row.path)
        if ($row.existed -and (Get-Sha256 (Get-UpdatePath $Transaction $row.saved)) -ne $row.hash) { throw 'El respaldo de recuperación está dañado. Conserva la carpeta updater-transaction y pide ayuda.' }
    }
    foreach ($row in @($journal.rows)) {
        $target=Get-UpdatePath $Game $row.path
        if ($row.existed) {
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
            if (Test-File $target) { Clear-ReadOnly $target }
            [IO.File]::Copy((Get-UpdatePath $Transaction $row.saved),$target,$true)
            [IO.File]::SetAttributes($target,[IO.FileAttributes]$row.attributes)
            if ((Get-Sha256 $target) -ne $row.hash) { throw "No se pudo restaurar $($row.path)." }
        } else { Remove-FileIfPresent $target }
    }
    # The installer can retain more than one personal INI for the same version;
    # its hash suffix is known only after a preceding link has merged new keys.
    foreach ($file in [IO.Directory]::GetFiles((Join-Path $Game 'LinkInRE4_Backup'))) {
        $name=[IO.Path]::GetFileName($file)
        if ($name -match '^link_runtime\.ini\.\d+\.\d+\.\d+-user(\.([a-f0-9]{12}|[a-f0-9]{64}))?$' -and
            $journal.PSObject.Properties['ini_backups'] -and @($journal.ini_backups) -notcontains $name) {
            Remove-FileIfPresent (Get-UpdatePath $Game ('LinkInRE4_Backup/' + $name))
        }
    }
    foreach ($rel in @($journal.new_dirs | Sort-Object { $_.Length } -Descending)) {
        $dir=Get-UpdatePath $Game $rel
        if ([IO.Directory]::Exists($dir) -and [IO.Directory]::GetFileSystemEntries($dir).Length -eq 0) { [IO.Directory]::Delete($dir) }
    }
    $journal.status='rolled_back'
    Write-JsonFile $journalPath $journal
}

function Remove-UpdateHistory([string]$Game, [string]$Keep) {
    # After a successful update keep only the newest previous snapshot: the one this run archived, or, when it
    # archived none, the most recently written one. Each history can weigh a gigabyte or more.
    $backup=[IO.Path]::GetDirectoryName((Get-UpdatePath $Game 'LinkInRE4_Backup/updater-transaction'))
    $histories=@([IO.Directory]::GetDirectories($backup) | Where-Object { [IO.Path]::GetFileName($_) -match '^updater-history-[0-9a-f]{32}$' })
    if (-not $Keep -and $histories.Count) {
        $Keep=@($histories | Sort-Object { [IO.Directory]::GetLastWriteTimeUtc($_) } -Descending)[0]
    }
    $removed=0
    foreach ($dir in $histories) {
        if ($Keep -and [string]::Equals($dir, $Keep, [StringComparison]::OrdinalIgnoreCase)) { continue }
        $rel='LinkInRE4_Backup/' + [IO.Path]::GetFileName($dir)
        [void](Get-UpdatePath $Game $rel)
        # Refuse to descend into a link: delete only a plain tree.
        $pending=New-Object Collections.Stack; $pending.Push($dir)
        while ($pending.Count) {
            foreach ($entry in [IO.Directory]::GetFileSystemEntries($pending.Pop())) {
                if (Test-Reparse $entry) { throw "El historial del actualizador contiene un enlace: $entry" }
                if ([IO.Directory]::Exists($entry)) { $pending.Push($entry) }
            }
        }
        [IO.Directory]::Delete($dir,$true)
        $removed++
    }
    return $removed
}

function Format-UpdateSteps([string[]]$Versions) {
    # 'se aplicó el parche 3.0.3' / 'se aplicaron los parches 3.0.2 y 3.0.3' / '3.0.2, 3.0.3 y 3.0.4'
    $list=@($Versions)
    if ($list.Count -eq 1) { return 'se aplicó el parche ' + $list[0] }
    return 'se aplicaron los parches ' + (($list[0..($list.Count - 2)]) -join ', ') + ' y ' + $list[-1]
}

function Format-UpdateStepsEnglish([string[]]$Versions) {
    $list=@($Versions)
    if ($list.Count -eq 1) { return 'patch ' + $list[0] + ' was applied' }
    return 'patches ' + (($list[0..($list.Count - 2)]) -join ', ') + ' and ' + $list[-1] + ' were applied'
}

function Get-UpdateEnglishReason([string]$Reason) {
    # The final message goes out in both languages: most players on Nexus do not read Spanish. The installer gives
    # its own English line, the usual refusals of this script are translated here, and anything else is rare.
    if ($Script:UpdateEnglish.ContainsKey($Reason)) { return $Script:UpdateEnglish[$Reason] }
    foreach ($pair in @(
        @('^Resident Evil 4 está abierto', 'Resident Evil 4 is running. Close it and run the updater again.'),
        @('^Ya hay otro actualizador', 'Another updater is already working on this folder, and it is still working even if you see no window. Wait for its final message (it can take several minutes, longer with the HD textures) and do not open it again.'),
        @('^El instalador anterior todavía', 'The previous installer is still working on this folder. Wait for it to finish and run the updater again.'),
        @('^Hay otro instalador trabajando', 'Another installer is working on this folder. Wait for it to finish.'),
        @('^No encontré el mod instalado', 'The mod is not installed here. Install the full Link in RE4 package first, then apply this patch.'),
        @('^No encontré el juego', 'The game was not found. Pick the Resident Evil 4 folder that contains BIO4 and Bin32.'),
        @('^La instalación anterior no está completa', 'The previous install is not complete. Finish its installer before updating.'),
        @('^Falta espacio', 'Not enough free space to back up and update. Free some space and try again.'),
        @('^La ruta contiene un enlace', 'The path contains a link or junction. Use an install without links.'),
        @('^(El ZIP del parche está incompleto|Archivo dañado dentro del parche)', 'The updater file is incomplete or damaged. Download it again.')
    )) { if ($Reason -match $pair[0]) { return $pair[1] } }
    return 'The update could not be completed. The reason above is in Spanish; send the log file if you need help.'
}

function Invoke-LinkUpdate($Options, [string]$RunDir) {
    $game=$Options.GamePath
    if ($Options.Headless -and -not $game) { throw 'El modo sin interfaz exige una ruta explícita del juego.' }
    Set-UpdateStatus 'Buscando el juego…' 'Looking for the game…'
    if (-not $game) {
        $game=Find-Game
        if (-not $game) {
            Set-UpdateStatus '' '' -1 'hide'
            Add-Type -AssemblyName System.Windows.Forms
            $picker=New-Object Windows.Forms.FolderBrowserDialog
            $picker.Description='No encontré Resident Evil 4. Elige la carpeta que contiene BIO4 y Bin32. / Resident Evil 4 was not found. Pick the folder that contains BIO4 and Bin32.'
            $picker.ShowNewFolderButton=$false
            try { if ($picker.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) { $game=$picker.SelectedPath } }
            finally { $picker.Dispose() }
            Set-UpdateStatus 'Buscando el juego…' 'Looking for the game…'
        }
    }
    if (-not (Test-GameFolder $game)) { throw 'No encontré el juego. Selecciona la carpeta Resident Evil 4 que contiene BIO4 y Bin32.' }
    $game=Get-CanonicalDirectory $game
    [void](Get-UpdatePath $game 'Bin32/bio4.exe')
    if (Test-GameRunning) { throw 'Resident Evil 4 está abierto. Ciérralo y vuelve a ejecutar el actualizador.' }
    $hashTool=[Security.Cryptography.SHA256]::Create()
    try { $key=[BitConverter]::ToString($hashTool.ComputeHash([Text.Encoding]::UTF8.GetBytes($game.ToLowerInvariant()))).Replace('-','') }
    finally { $hashTool.Dispose() }
    $mutex=New-Object Threading.Mutex($false,('Local\LinkInRE4-Update-'+$key))
    $owned=$false; $transaction=Join-Path $game 'LinkInRE4_Backup/updater-transaction'
    $active=$false; $detected=''; $archive=''
    try {
        try { $owned=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $owned=$true }
        if (-not $owned) { throw 'Ya hay otro actualizador trabajando en esta carpeta, y sigue trabajando aunque no veas su ventana. Espera su mensaje final (puede tardar varios minutos, más con las texturas HD) y no lo abras otra vez.' }
        Set-UpdateStatus 'Revisando la instalación…' 'Checking the install…'
        Assert-NoUpdateWriter $game
        [void](Get-UpdatePath $game 'LinkInRE4_Backup/updater-transaction/transaction.json')
        $journal=Join-Path $transaction 'transaction.json'
        if (Test-File $journal) {
            $old=Read-Json $journal
            if ($old.status -eq 'active') {
                # A snapshot fits only the install it was taken from. After an uninstall its backup folder is gone; after a
                # fresh full install installed.json names another one. Restoring it then would break the current install.
                # An unreadable installed.json (the same crash) still recovers: the snapshot is the only way back.
                $snapDir=@($old.rows | ForEach-Object { if ($_.path -match '^LinkInRE4_Backup/([^/]+)/journal\.tsv$') { $Matches[1] } })
                $liveDir=$null
                try { $liveDir=(Read-Json (Get-UpdatePath $game 'LinkInRE4_Backup/installed.json')).backup_dir } catch { }
                if ($snapDir.Count -eq 1 -and [IO.Directory]::Exists((Get-UpdatePath $game ('LinkInRE4_Backup/' + $snapDir[0]))) -and
                    ($null -eq $liveDir -or $liveDir -eq $snapDir[0])) {
                    Set-UpdateStatus 'Recuperando la instalación anterior…' 'Recovering the previous install…'
                    Restore-UpdateSnapshot $game $transaction
                    return @{ status='recovered'; version=''; detected_version=''; applied=@(); message='Se recuperó la instalación anterior después de una interrupción. Ejecuta el actualizador otra vez para aplicar el parche.'; message_en='The previous install was recovered after an interruption. Run the updater again to apply the patch.'; backup_path=$transaction }
                }
                $old.status='stale'; Write-JsonFile $journal $old
            }
        }
        $stateFile=Get-UpdatePath $game 'LinkInRE4_Backup/installed.json'
        if (-not (Test-File $stateFile)) { throw 'No encontré el mod instalado. Instala primero el paquete completo de Link in RE4 y después aplica este parche.' }
        $state=Read-Json $stateFile
        if ($state.schema -ne 'link-in-re4-installed-1' -or $state.status -ne 'complete') { throw 'La instalación anterior no está completa. Completa su instalador antes de actualizar.' }
        $detected=''
        if ($state.PSObject.Properties['version'] -and $null -ne $state.version) { $detected=[string]$state.version }
        $Script:LinkUpdateDetected=$detected
        Set-UpdateStatus 'Leyendo los parches…' 'Reading the patches…'
        $chain=Read-UpdateChain $Options.BundlePath $RunDir
        $first=$chain[0].Manifest.from_version
        $target=$chain[-1].Manifest.version
        $selected=New-Object Collections.ArrayList
        $current=$detected
        foreach ($patch in $chain) {
            if ($patch.Manifest.from_version -eq $current) { [void]$selected.Add($patch); $current=$patch.Manifest.version }
        }
        if ($detected -eq $target) {
            Set-UpdateStatus 'Comprobando los archivos…' 'Checking the files…'
            Invoke-UpdateInstaller $game $chain[-1].Root (Join-Path $RunDir 'verify-current.log') $true
            return @{ status='up_to_date'; version=$target; detected_version=$detected; applied=@();
                message="Ya tienes Link in RE4 $target. Los archivos se comprobaron."; message_en="You already have Link in RE4 $target. The files were checked."; backup_path='' }
        }
        if ($selected.Count -eq 0 -or $current -ne $target) {
            # Nothing has been written yet: every refusal below leaves the game untouched.
            $known=$detected -match '^\d+\.\d+\.\d+$'
            if ($known -and [version]$detected -gt [version]$target) {
                return @{ status='newer'; version=$detected; detected_version=$detected; applied=@();
                    message="Ya tienes Link in RE4 $detected, que es más nueva que la $target de este actualizador. No se hizo ningún cambio; no necesitas este archivo."; message_en="You already have Link in RE4 $detected, which is newer than the $target of this updater. Nothing was changed; you do not need this file."; backup_path='' }
            }
            if ($known -and [version]$detected -lt [version]$first) {
                return @{ status='too_old'; version=''; detected_version=$detected; applied=@();
                    message=("Tienes Link in RE4 $detected. Este actualizador sirve desde la $first`: primero aplica el parche $first o instala el paquete completo $first, y después vuelve a ejecutar este archivo. No se hizo ningún cambio."); message_en=("You have Link in RE4 $detected. This updater works from $first on: first apply the $first patch or install the full $first package, then run this file again. Nothing was changed."); backup_path='' }
            }
            $shown=$detected; if (-not $shown) { $shown='sin número' }
            $shownEn=$detected; if (-not $shownEn) { $shownEn='with no version number' }
            $accepted=(@($chain | ForEach-Object { $_.Manifest.from_version }) -join ', ')
            return @{ status='unknown_version'; version=''; detected_version=$detected; applied=@();
                message=("Tienes Link in RE4 $shown, una versión que este actualizador no reconoce (sirve para $accepted). No se hizo ningún cambio. Pide el parche desde tu versión o instala el paquete completo $target."); message_en=("You have Link in RE4 $shownEn, a version this updater does not recognise (it works for $accepted). Nothing was changed. Get the patch for your version or install the full $target package."); backup_path='' }
        }
        $applied=[string[]]@($selected | ForEach-Object { $_.Manifest.version })
        $steps=$selected.Count + 2
        Set-UpdateStatus "Comprobando la instalación actual (paso 1 de $steps)…" "Checking the current install (step 1 of $steps)…"
        Invoke-UpdateInstaller $game $selected[0].Root (Join-Path $RunDir 'preflight.log') $true
        Set-UpdateStatus "Guardando una copia para deshacer (paso 2 de $steps)…" "Saving a rollback copy (step 2 of $steps)…"
        $paths=Get-UpdateSnapshotPaths $selected $state $game
        # All destination paths and existing backups checked before creating a transaction.
        foreach ($rel in $paths.Keys) { [void](Get-UpdatePath $game $rel) }
        Assert-UpdateSnapshotSpace $game $paths
        if ([IO.Directory]::Exists($transaction)) {
            $archive=Join-Path ([IO.Path]::GetDirectoryName($transaction)) ('updater-history-' + [Guid]::NewGuid().ToString('N'))
            [IO.Directory]::Move($transaction,$archive)
        }
        New-UpdateSnapshot $game $transaction $paths
        $active=$true
        $step=0
        foreach ($patch in $selected) {
            if (Test-GameRunning) { throw 'Resident Evil 4 está abierto. Ciérralo antes de actualizar.' }
            Set-UpdateStatus ('Aplicando el parche ' + $patch.Manifest.version + ' (paso ' + ($step + 3) + " de $steps)…") ('Applying patch ' + $patch.Manifest.version + ' (step ' + ($step + 3) + " of $steps)…")
            Invoke-UpdateInstaller $game $patch.Root (Join-Path $RunDir ('install-'+$step+'.log')) $false
            $step++
        }
        $done=Read-Json $journal
        $done.status='complete'
        Write-JsonFile $journal $done
        $active=$false
        # The update is complete; a cleanup failure must not undo it. Logged, retried by the next update.
        try { [void](Remove-UpdateHistory $game $archive) }
        catch { [IO.File]::WriteAllText((Join-Path $RunDir 'history-cleanup.log'), $_.Exception.Message, $Utf8) }
        return @{ status='updated'; version=$target; detected_version=$detected; applied=$applied;
            message=("Listo: tenías la $detected y " + (Format-UpdateSteps $applied) + ". Ahora tienes Link in RE4 $target. Se conservaron tus valores del INI.");
            message_en=("Done: you had $detected and " + (Format-UpdateStepsEnglish $applied) + ". You now have Link in RE4 $target. Your INI values were kept.");
            backup_path=$transaction }
    } catch {
        $reason=$_.Exception.Message
        if ($active) {
            Set-UpdateStatus 'Deshaciendo la actualización…' 'Undoing the update…'
            try {
                Restore-UpdateSnapshot $game $transaction
                return @{ status='rolled_back'; version=''; detected_version=$detected; applied=@(); message=($reason + "`r`nSe deshizo la actualización y se recuperó la instalación anterior ($detected)."); message_en=((Get-UpdateEnglishReason $reason) + "`r`nThe update was undone and the previous install ($detected) is back."); backup_path=$transaction }
            } catch {
                return @{ status='rollback_failed'; version=''; detected_version=$detected; applied=@(); message=($reason + "`r`nNo se pudo completar la recuperación: " + $_.Exception.Message + "`r`nNo abras el juego. Conserva el respaldo: " + $transaction); message_en=((Get-UpdateEnglishReason $reason) + "`r`nThe recovery could not be completed. Do not open the game. Keep the backup: " + $transaction); backup_path=$transaction }
            }
        }
        throw
    } finally {
        if ($owned) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}

function Start-LinkUpdate($Options) {
    $result=$Options.ResultPath
    if (-not $result) {
        $result=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) ('LinkInRE4/Updater/'+[Guid]::NewGuid().ToString('N')+'/result.json')
    }
    $result=[IO.Path]::GetFullPath($result)
    $runDir=Join-Path ([IO.Path]::GetDirectoryName($result)) ('updater-run-'+[Guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($runDir)
    $Script:LinkUpdateDetected=''
    $Script:UpdateStatus.RunDir=$runDir
    try { $report=Invoke-LinkUpdate $Options $runDir }
    catch { $report=@{ status='error'; version=''; detected_version=$Script:LinkUpdateDetected; applied=@(); message=$_.Exception.Message; backup_path='' } }
    if (-not $report.ContainsKey('detected_version')) { $report.detected_version='' }
    if (-not $report.ContainsKey('applied')) { $report.applied=@() }
    if (-not $report.ContainsKey('message_en')) { $report.message_en=Get-UpdateEnglishReason $report.message }
    $report.log_dir=$runDir
    $report.finished=(Get-Date).ToString('s')
    Write-JsonFile $result $report
    if ($Options.Headless) { Set-UpdateStatus '' '' -1 'hide' }
    else {
        # The launcher's window shows the final message by itself, in both languages. Without the launcher, or if its
        # status file cannot be written, a plain message box does.
        $tone='error'
        if ($report.status -in @('updated','up_to_date')) { $tone='ok' } elseif ($report.status -in @('newer','recovered')) { $tone='info' }
        Set-UpdateStatus $report.message $report.message_en -1 $tone $result
        if (-not $Script:UpdateStatus.Told) {
            Add-Type -AssemblyName System.Windows.Forms
            [void][Windows.Forms.MessageBox]::Show(($report.message + "`r`n`r`n" + $report.message_en + "`r`n`r`nRegistro / Log: " + $result), 'Link in RE4 — Actualizador / Updater')
        }
    }
    if ($report.status -in @('updated','up_to_date','newer','recovered')) { return 0 }
    return 1
}

if (-not $onlyLibrary) { exit (Start-LinkUpdate $entryOptions) }
