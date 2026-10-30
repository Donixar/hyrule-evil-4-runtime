<#
.SYNOPSIS
    Read-only Link in RE4 diagnostics. Windows PowerShell 5.1; no administrator or extra modules.
.DESCRIPTION
    Double-click Diagnostico-LinkInRE4.bat. Writes one ZIP to Desktop and removes its temporary
    staging folder. Never installs, launches, repairs, or writes files inside the game.
    Text copies redact game/Steam/user-profile roots. Minidumps retain original bytes because
    rewriting memory would break debugging; they can contain private paths and process memory.
    Checks every SHA-256 in LinkInRE4_Backup/installed.json. That is a local installation record,
    not independent proof of an official release. No saves, Steam account data or product keys.
.PARAMETER GamePath
    Optional game root containing Bin32/bio4.exe and BIO4. Otherwise use Steam registry/libraries.
.PARAMETER SteamPath
    Optional Steam root(s); when supplied, discovery uses only these roots (offline fixtures).
.PARAMETER OutputDirectory
    Destination for the ZIP, default Windows Desktop. Must be outside the game and have no links.
.PARAMETER SkipSystemInfo
    Offline fixture switch: skip Windows/GPU/locale/VC++ queries.
.PARAMETER MaxDumpCount
    Newest dumps to collect, default 3 (last modification time, then filename).
.PARAMETER MaxDumpBytes
    Maximum bytes per dump, default 64 MiB. Larger dumps are listed, not copied.
.PARAMETER MaxTextBytes
    Maximum bytes per text file, default 16 MiB; large logs retain their final bytes.
#>
[CmdletBinding()]
param(
    [string]$GamePath = '',
    [string[]]$SteamPath = @(),
    [string]$OutputDirectory = '',
    [switch]$SkipSystemInfo,
    [ValidateRange(0, 10)][int]$MaxDumpCount = 3,
    [ValidateRange(1, 268435456)][long]$MaxDumpBytes = 67108864,
    [ValidateRange(1024, 33554432)][int]$MaxTextBytes = 16777216
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$Utf8 = New-Object System.Text.UTF8Encoding($false)
$Script:GameRoot = ''
$Script:GameAlias = ''
$Script:SteamRoots = @()
$Script:Stage = $null
$Script:Warnings = New-Object System.Collections.ArrayList
$Script:Collection = New-Object System.Collections.ArrayList

function Get-Property($Object, [string]$Name, $Default = $null) {
    if ($Object -is [Collections.IDictionary] -and $Object.Contains($Name)) { return $Object[$Name] }
    if ($null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]) { return $Object.$Name }
    return $Default
}

function Protect-Text([string]$Value) {
    foreach ($root in @($Script:GameRoot, $Script:GameAlias) + @($Script:SteamRoots) + @($env:USERPROFILE)) {
        if ($root) {
            $label = '[USER]'
            if ($root -eq $Script:GameRoot -or $root -eq $Script:GameAlias) { $label = '[GAME]' }
            elseif (@($Script:SteamRoots) -contains $root) { $label = '[STEAM]' }
            $normalized = $root.TrimEnd('\', '/')
            foreach ($form in @($normalized, $normalized.Replace('\', '/'), $normalized.Replace('\', '\\'))) {
                $Value = [regex]::Replace($Value, [regex]::Escape($form), $label, 'IgnoreCase')
            }
        }
    }
    # Also cover other profile names embedded in historical logs or JSON-escaped paths.
    $Value = [regex]::Replace($Value, '(?i)[a-z]:[\\/]+Users[\\/]+[^\\/\r\n"<>]+', '[USER]')
    return $Value
}

function Add-Warning([string]$Value) { [void]$Script:Warnings.Add((Protect-Text $Value)) }
function Write-Text([string]$Path, [string]$Value) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, (Protect-Text $Value), $Utf8)
}
function Get-Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function Test-NoLinks([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                # OneDrive placeholders are reparse points, not links. PS 5.1 LinkType is also empty when a
                # link denies read or listing (C:\Documents and Settings), so the tag is read from the parent
                # listing: name surrogates (junction, symlink, mount point) and any unreadable reparse point fail.
                if (@('Junction', 'SymbolicLink') -contains $item.LinkType) { return $false }
                try {
                    if (-not ('LinkInRE4Diag.ReparseTag' -as [type])) {
                        Add-Type -Namespace LinkInRE4Diag -Name ReparseTag -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr FindFirstFileW(string name, IntPtr data);
[DllImport("kernel32.dll")] static extern bool FindClose(IntPtr handle);
// WIN32_FIND_DATAW: dwFileAttributes at 0, dwReserved0 (the reparse tag) at 36, 592 bytes in all.
public static long Get(string path) {
    IntPtr data = Marshal.AllocHGlobal(592);
    try {
        IntPtr handle = FindFirstFileW(path, data); if (handle == new IntPtr(-1)) return -1; FindClose(handle);
        return (Marshal.ReadInt32(data, 0) & 0x400) != 0 ? (long)(uint)Marshal.ReadInt32(data, 36) : -1;
    } finally { Marshal.FreeHGlobal(data); }
}
'@
                    }
                    $tag = [LinkInRE4Diag.ReparseTag]::Get($current.TrimEnd('\', '/'))
                } catch { return $false }
                if ($tag -lt 0 -or ($tag -band 0x20000000)) { return $false }
                try { [void][IO.Directory]::EnumerateFileSystemEntries($current).GetEnumerator().MoveNext() } catch { return $false }
            }
        }
        $parent = [IO.Path]::GetDirectoryName($current.TrimEnd('\', '/'))
        if ($parent -eq $current) { break }
        $current = $parent
    }
    return $true
}

function Test-InDirectory([string]$Path, [string]$Directory) {
    if (-not $Directory) { return $false }
    $left = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $right = [IO.Path]::GetFullPath($Directory).TrimEnd('\', '/')
    return ($left.Equals($right, [StringComparison]::OrdinalIgnoreCase) -or
        $left.StartsWith($right + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))
}

function Get-CanonicalDirectory([string]$Path, [int]$Depth = 0) {
    if ($Depth -gt 20) { throw 'Too many directory links while locating the game.' }
    $full = [IO.Path]::GetFullPath($Path)
    $current = [IO.Path]::GetPathRoot($full)
    foreach ($piece in @($full.Substring($current.Length) -split '[\\/]' | Where-Object { $_ })) {
        $current = Join-Path $current $piece
        if (-not (Test-Path -LiteralPath $current)) { continue }
        $item = Get-Item -LiteralPath $current -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            $targets = @(Get-Property $item 'Target' @())
            if (-not $targets.Count -or -not $targets[0]) { throw 'Could not resolve a game directory link safely.' }
            $target = [string]$targets[0]
            if (-not [IO.Path]::IsPathRooted($target)) { $target = Join-Path ([IO.Path]::GetDirectoryName($current)) $target }
            $current = Get-CanonicalDirectory $target ($Depth + 1)
        }
    }
    return $current.TrimEnd('\', '/')
}

function Get-PathLengths([string]$Root) {
    # PowerShell strings use UTF-16 code units, matching Win32 wchar_t buffer counts.
    # Longest current runtime asset: dll/shield_guard_assets.h kBreakMetal (33 units).
    $directory = (Join-Path $Root 'Bin32').TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    return [ordered]@{
        root_chars = $Root.Length; bin32_directory_chars = $directory.Length
        dinput8_path_chars = ($directory + 'dinput8.dll').Length
        longest_runtime_asset_path_chars = ($directory + 'link_shield_guard_break_metal.wav').Length
    }
}

function Add-PathWarnings($Lengths, [string]$Basis) {
    if ($Lengths.longest_runtime_asset_path_chars -ge 260) {
        Add-Warning ($Basis + ' runtime asset path length ' + $Lengths.longest_runtime_asset_path_chars +
            ' reaches MAX_PATH 260. This is a compatibility risk, not proof of the crash or the loaded DLL path.')
    }
    if ($Lengths.dinput8_path_chars -ge 260) {
        Add-Warning ($Basis + ' dinput8.dll path length ' + $Lengths.dinput8_path_chars +
            ' reaches MAX_PATH 260; the recorded path may differ from the actual loaded module path.')
    }
}

function Get-GameFile([string]$Relative) {
    if (-not $Script:GameRoot -or -not $Relative -or $Relative -match '[:\x00-\x1f]' -or
        [IO.Path]::IsPathRooted($Relative)) { return $null }
    $pieces = @($Relative -split '[\\/]')
    if (@($pieces | Where-Object { $_ -eq '..' -or $_ -eq '.' -or $_ -eq '' -or $_ -match '[. ]$' }).Count) { return $null }
    $target = $Script:GameRoot
    foreach ($piece in $pieces) {
        $target = Join-Path $target $piece
        if (Test-Path -LiteralPath $target) {
            if ((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { return $null }
        }
    }
    return $target
}

function Test-Game([string]$Path) {
    if (-not $Path) { return $false }
    return ([IO.Directory]::Exists((Join-Path $Path 'BIO4')) -and [IO.File]::Exists((Join-Path $Path 'Bin32\bio4.exe')))
}

function Find-Game {
    if ($GamePath) {
        if (Test-Game $GamePath) { return @{ path = $GamePath; discovery = 'explicit' } }
        Add-Warning 'GamePath does not contain BIO4 and Bin32/bio4.exe; game files were not collected.'
        return $null
    }
    $roots = New-Object System.Collections.ArrayList
    if ($SteamPath.Count) { foreach ($root in $SteamPath) { [void]$roots.Add($root) } }
    else {
        foreach ($key in @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 254700',
                           'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 254700')) {
            try {
                $root = (Get-ItemProperty -LiteralPath $key -Name InstallLocation).InstallLocation
                if (Test-Game $root) { return @{ path = $root; discovery = 'steam_registry' } }
            } catch { }
        }
        foreach ($entry in @(@('HKCU:\Software\Valve\Steam', 'SteamPath'),
                              @('HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath'),
                              @('HKLM:\SOFTWARE\Valve\Steam', 'InstallPath'))) {
            try {
                $root = (Get-ItemProperty -LiteralPath $entry[0] -Name $entry[1]).($entry[1])
                if ($root) { [void]$roots.Add($root) }
            } catch { }
        }
    }
    $libraries = New-Object System.Collections.ArrayList
    foreach ($root in @($roots | Sort-Object -Unique)) {
        [void]$libraries.Add($root)
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not [IO.File]::Exists($vdf)) { continue }
        try {
            $content = [IO.File]::ReadAllText($vdf)
            # New format has "path" fields; old Steam used "1" "D:\\SteamLibrary".
            foreach ($match in [regex]::Matches($content, '"(?:path|[0-9]+)"\s+"([^"\r\n]+)"')) {
                $library = $match.Groups[1].Value.Replace('\\', '\')
                if ([IO.Path]::IsPathRooted($library)) { [void]$libraries.Add($library) }
            }
        } catch { Add-Warning 'Could not read a Steam libraryfolders.vdf.' }
    }
    $Script:SteamRoots = @($libraries | Sort-Object -Unique)
    foreach ($library in $Script:SteamRoots) {
        $acf = Join-Path $library 'steamapps\appmanifest_254700.acf'
        if ([IO.File]::Exists($acf)) {
            try {
                $match = [regex]::Match([IO.File]::ReadAllText($acf), '"installdir"\s+"([^"\r\n]+)"')
                if ($match.Success -and $match.Groups[1].Value -notmatch '[\\/:]' -and $match.Groups[1].Value -ne '..') {
                    $candidate = Join-Path $library ('steamapps\common\' + $match.Groups[1].Value)
                    if (Test-Game $candidate) { return @{ path = $candidate; discovery = 'steam_library_manifest' } }
                }
            } catch { Add-Warning 'Could not read a Steam appmanifest_254700.acf.' }
        }
        $candidate = Join-Path $library 'steamapps\common\Resident Evil 4'
        if (Test-Game $candidate) { return @{ path = $candidate; discovery = 'steam_library_default' } }
    }
    Add-Warning 'Steam RE4 (app 254700) was not found. Run again with -GamePath pointing to its game root.'
    return $null
}

function Read-TextTail([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        # Read the BOM from the original start, not from the truncated tail. Otherwise
        # UTF-16 ASCII bytes look like valid UTF-8 with NULs and bypass path redaction.
        $fileLength = $stream.Length
        $prefix = New-Object byte[] 3
        $prefixCount = $stream.Read($prefix, 0, 3)
        $encoding = $Utf8
        $bomBytes = 0
        $unitBytes = 1
        $knownEncoding = $false
        if ($prefixCount -ge 2 -and $prefix[0] -eq 255 -and $prefix[1] -eq 254) {
            $encoding = [Text.Encoding]::Unicode; $bomBytes = 2; $unitBytes = 2; $knownEncoding = $true
        } elseif ($prefixCount -ge 2 -and $prefix[0] -eq 254 -and $prefix[1] -eq 255) {
            $encoding = [Text.Encoding]::BigEndianUnicode; $bomBytes = 2; $unitBytes = 2; $knownEncoding = $true
        } elseif ($prefixCount -ge 3 -and $prefix[0] -eq 239 -and $prefix[1] -eq 187 -and $prefix[2] -eq 191) {
            $bomBytes = 3; $knownEncoding = $true
        }
        $truncated = ($fileLength -gt $MaxTextBytes)
        $start = [long][Math]::Max($bomBytes, $fileLength - $MaxTextBytes)
        # Align to complete UTF-16 code units, even with an odd byte limit.
        $remainder = ($start - $bomBytes) % $unitBytes
        if ($remainder) { $start += $unitBytes - $remainder }
        $length = [int]($fileLength - $start)
        $length -= $length % $unitBytes
        [void]$stream.Seek($start, [IO.SeekOrigin]::Begin)
        $buffer = New-Object byte[] $length
        $count = 0
        while ($count -lt $length) {
            $read = $stream.Read($buffer, $count, $length - $count)
            if ($read -eq 0) { break }
            $count += $read
        }
        $bufferStart = 0
        if ($truncated -and $unitBytes -eq 1) {
            # A bounded UTF-8 tail may start halfway through a multibyte character.
            while ($bufferStart -lt $count -and ($buffer[$bufferStart] -band 0xC0) -eq 0x80) { $bufferStart++ }
        }
        if (-not $knownEncoding) {
            try { [void](New-Object Text.UTF8Encoding($false, $true)).GetString($buffer, $bufferStart, $count - $bufferStart) }
            catch { $encoding = [Text.Encoding]::Default }
        }
        $value = $encoding.GetString($buffer, $bufferStart, $count - $bufferStart).TrimStart([char]0xFEFF)
        if ($truncated) {
            # Discard a partial first line: its path prefix could have been cut away,
            # making the remaining personal name impossible to identify for redaction.
            $newline = $value.IndexOf("`n")
            if ($newline -ge 0) { $value = $value.Substring($newline + 1) } else { $value = '' }
            $value = "[Only complete lines from the last $MaxTextBytes bytes were collected]`r`n" + $value
        }
        return $value
    } finally { $stream.Dispose() }
}

function Collect-Text([string]$Relative) {
    $source = Get-GameFile $Relative
    if (-not $source -or -not [IO.File]::Exists($source)) { return }
    try {
        Write-Text (Join-Path $Script:Stage ('game/' + $Relative)) (Read-TextTail $source)
        $file = Get-Item -LiteralPath $source
        [void]$Script:Collection.Add([ordered]@{ path = $Relative; status = 'collected_redacted'; bytes = $file.Length; modified_utc = $file.LastWriteTimeUtc.ToString('o') })
    } catch { Add-Warning ('Could not collect text: ' + $Relative) }
}

function Get-FileDetails([string]$Relative) {
    $source = Get-GameFile $Relative
    if (-not $source) { return [ordered]@{ path = $Relative; status = 'unsafe_path' } }
    if (-not [IO.File]::Exists($source)) { return [ordered]@{ path = $Relative; status = 'missing' } }
    try {
        $file = Get-Item -LiteralPath $source -Force
        return [ordered]@{ path = $Relative; status = 'ok'; bytes = $file.Length; sha256 = (Get-Hash $source)
            modified_utc = $file.LastWriteTimeUtc.ToString('o'); file_version = $file.VersionInfo.FileVersion }
    } catch { return [ordered]@{ path = $Relative; status = 'unreadable' } }
}

function Add-ExeHeader($Details) {
    $source = Get-GameFile 'Bin32/bio4.exe'
    if (-not $source -or -not [IO.File]::Exists($source)) { return }
    $stream = $null
    try {
        $stream = [IO.File]::Open($source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { return }
        [void]$stream.Seek(60, [IO.SeekOrigin]::Begin)
        $offset = $reader.ReadUInt32()
        if ($offset -gt $stream.Length - 24) { return }
        [void]$stream.Seek($offset, [IO.SeekOrigin]::Begin)
        if ($reader.ReadUInt32() -ne 0x4550) { return }
        $Details['pe_machine'] = ('0x{0:x4}' -f $reader.ReadUInt16())
        [void]$stream.Seek($offset + 22, [IO.SeekOrigin]::Begin)
        $Details['large_address_aware'] = (($reader.ReadUInt16() -band 0x20) -ne 0)
    } catch { Add-Warning 'Could not read bio4.exe PE header.' }
    finally { if ($stream) { $stream.Dispose() } }
}

function Get-SystemSummary {
    if ($SkipSystemInfo) { return [ordered]@{ status = 'skipped_for_fixture' } }
    $result = [ordered]@{ status = 'collected'; windows = $null; gpu = @(); locale = $null; vcredist = @(); system_x86_dlls = @() }
    try {
        $os = Get-CimInstance Win32_OperatingSystem
        $result.windows = [ordered]@{ caption = $os.Caption; version = $os.Version; build = $os.BuildNumber; architecture = $os.OSArchitecture }
    } catch { Add-Warning 'Windows version query failed.' }
    try {
        $result.gpu = @(Get-CimInstance Win32_VideoController | ForEach-Object {
            [ordered]@{ name = $_.Name; driver_version = $_.DriverVersion; video_processor = $_.VideoProcessor }
        })
    } catch { Add-Warning 'GPU query failed.' }
    try {
        $culture = [Globalization.CultureInfo]::CurrentCulture
        $result.locale = [ordered]@{ culture = $culture.Name; ui_culture = [Globalization.CultureInfo]::CurrentUICulture.Name
            decimal_separator = $culture.NumberFormat.NumberDecimalSeparator; system_locale = (Get-WinSystemLocale).Name }
    } catch { Add-Warning 'Locale query failed.' }
    $vc = New-Object System.Collections.ArrayList
    foreach ($prefix in @('HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes',
                          'HKLM:\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC\Runtimes')) {
        foreach ($arch in @('x86', 'x64')) {
            try {
                $item = Get-ItemProperty -LiteralPath (Join-Path $prefix $arch)
                [void]$vc.Add([ordered]@{ source = 'VC14_runtime'; architecture = $arch; installed = (Get-Property $item 'Installed'); version = (Get-Property $item 'Version') })
            } catch { }
        }
    }
    foreach ($prefix in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
                          'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')) {
        try {
            foreach ($key in Get-ChildItem -LiteralPath $prefix) {
                try {
                    $item = Get-ItemProperty -LiteralPath $key.PSPath
                    $name = Get-Property $item 'DisplayName' ''
                    if ($name -match '^Microsoft Visual C\+\+.*(?:Redistributable|Runtime)') {
                        [void]$vc.Add([ordered]@{ source = 'installed_program'; name = $name; version = (Get-Property $item 'DisplayVersion') })
                    }
                } catch { }
            }
        } catch { Add-Warning 'A VC++ installed-program registry view was unavailable.' }
    }
    $result.vcredist = @($vc)
    $systemFolder = [Environment]::GetFolderPath('System')
    if ([Environment]::Is64BitOperatingSystem -and [Environment]::Is64BitProcess) { $systemFolder = Join-Path $env:WINDIR 'SysWOW64' }
    $result.system_x86_dlls = @(foreach ($name in @('msvcp140.dll', 'vcruntime140.dll', 'msvcr120.dll', 'd3d9.dll', 'dinput8.dll')) {
        $path = Join-Path $systemFolder $name
        $item = [ordered]@{ name = $name; present = [IO.File]::Exists($path) }
        if ($item.present) {
            try { $item['version'] = (Get-Item -LiteralPath $path).VersionInfo.FileVersion; $item['sha256'] = Get-Hash $path } catch { }
        }
        $item
    })
    return $result
}

function Get-Installation {
    $summary = [ordered]@{ status = 'not_found'; verification_basis = 'local installed.json; not an independent official release manifest' }
    $verification = [ordered]@{ matched = 0; changed = 0; missing = 0; invalid = 0; unreadable = 0; files = @() }
    $source = Get-GameFile 'LinkInRE4_Backup/installed.json'
    if (-not $source -or -not [IO.File]::Exists($source)) { return @{ summary = $summary; verification = $verification } }
    Collect-Text 'LinkInRE4_Backup/installed.json'
    try {
        if ((Get-Item -LiteralPath $source).Length -gt $MaxTextBytes) { throw 'Installation record is too large.' }
        $state = [IO.File]::ReadAllText($source) | ConvertFrom-Json
        if ((Get-Property $state 'schema') -ne 'link-in-re4-installed-1') { throw 'Unrecognized installation schema.' }
        if ($null -eq $state.PSObject.Properties['files']) { throw 'Missing files in installation record.' }
    } catch {
        $summary.status = 'unreadable'
        Add-Warning 'LinkInRE4_Backup/installed.json is unreadable or has an unrecognized schema; file verification was not possible.'
        return @{ summary = $summary; verification = $verification }
    }
    foreach ($name in @('status', 'version', 'parts', 'patched_from', 'started', 'finished')) { $summary[$name] = Get-Property $state $name }
    $summary['record_sha256'] = Get-Hash $source
    $rows = New-Object System.Collections.ArrayList
    foreach ($file in @($state.files)) {
        $relative = [string](Get-Property $file 'path' '')
        $expected = [string](Get-Property $file 'sha256' '')
        $target = Get-GameFile $relative
        $row = [ordered]@{ path = (Protect-Text $relative); expected_sha256 = $expected; actual_sha256 = $null; status = 'invalid_path' }
        if (-not $target) { $verification.invalid++ }
        elseif ($expected -notmatch '^[a-fA-F0-9]{64}$') { $row.status = 'invalid_hash'; $verification.invalid++ }
        elseif (-not [IO.File]::Exists($target)) { $row.status = 'missing'; $verification.missing++ }
        else {
            try {
                $row.actual_sha256 = Get-Hash $target
                $row['actual_bytes'] = (Get-Item -LiteralPath $target).Length
                if ($row.actual_sha256 -eq $expected) { $row.status = 'matched'; $verification.matched++ }
                else { $row.status = 'changed'; $verification.changed++ }
            } catch { $row.status = 'unreadable'; $verification.unreadable++ }
        }
        [void]$rows.Add($row)
    }
    $verification.files = @($rows)
    $backup = [string](Get-Property $state 'backup_dir' '')
    if ($backup -match '^[a-zA-Z0-9_-]+$') {
        Collect-Text ('LinkInRE4_Backup/' + $backup + '/journal.tsv')
        Collect-Text ('LinkInRE4_Backup/' + $backup + '/backup.json')
    }
    if ($summary.status -ne 'complete') { Add-Warning 'Installation record is not complete; interrupted installation is possible.' }
    return @{ summary = $summary; verification = $verification }
}

function Get-CompressedTwins($Verification) {
    $files = New-Object System.Collections.ArrayList
    foreach ($row in @($Verification.files)) {
        if ($row.status -eq 'invalid_path' -or $row.path -notmatch '^BIO4/' -or $row.path -match '\.lfs$') { continue }
        $relative = $row.path + '.lfs'
        $source = Get-GameFile $relative
        if ($source -and [IO.File]::Exists($source)) {
            [void]$files.Add([ordered]@{ path = $relative; loose_path = $row.path; status = 'compressed_twin_present'; bytes = (Get-Item -LiteralPath $source).Length })
        }
    }
    return [ordered]@{ present = $files.Count; files = @($files) }
}

function Get-TitlePacks($Installation) {
    # Actual package title IDs; do not infer which game language the player selected.
    $parts = @(Get-Property $Installation 'parts' @())
    foreach ($part in @('sd', 'hd')) {
        $folder = 'ImagePack'
        if ($part -eq 'hd') { $folder = 'ImagePackHD' }
        foreach ($id in @('0', '1', '2', '3', '4', '5', '6', '9')) {
            $relative = 'BIO4/' + $folder + '/3e00000' + $id + '.pack'
            $loose = Get-GameFile $relative
            $compressed = Get-GameFile ($relative + '.lfs')
            $exists = ($loose -and [IO.File]::Exists($loose))
            $hasLfs = ($compressed -and [IO.File]::Exists($compressed))
            $installed = ($parts -contains $part)
            $status = 'part_not_installed'
            if ($installed) {
                if ($hasLfs) { $status = 'compressed_twin_present' }
                elseif (-not $exists) { $status = 'missing_for_installed_part' }
                else { $status = 'loose_present' }
            }
            [ordered]@{ path = $relative; part = $part; title_id = $id; part_installed = $installed
                loose_present = [bool]$exists; lfs_present = [bool]$hasLfs; status = $status }
        }
    }
}

try {
    $found = Find-Game
    if ($found) {
        $Script:GameAlias = [IO.Path]::GetFullPath($found.path).TrimEnd('\', '/')
        $Script:GameRoot = Get-CanonicalDirectory $Script:GameAlias
    }
    if (-not $OutputDirectory) { $OutputDirectory = [Environment]::GetFolderPath('Desktop') }
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ((Test-InDirectory $OutputDirectory $Script:GameRoot) -or (Test-InDirectory $tempRoot $Script:GameRoot)) {
        throw 'OUTPUT_INSIDE_GAME: output and temporary folders must be outside the game.'
    }
    if (-not (Test-NoLinks $OutputDirectory) -or -not (Test-NoLinks $tempRoot)) { throw 'Output/temporary folders must not contain junctions or symbolic links.' }
    $Script:Stage = Join-Path $tempRoot ('LinkInRE4-Diagnostico-' + [guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($Script:Stage)
    $report = [ordered]@{
        schema = 'link-in-re4-diagnostics-1'; collected_utc = [DateTime]::UtcNow.ToString('o')
        game = [ordered]@{ found = [bool]$found; path = $(if ($found) { '[GAME]' } else { $null }); discovery = $(if ($found) { $found.discovery } else { 'not_found' })
            path_lengths = $null; canonical_path_lengths = $null
            path_length_units = 'UTF-16 code units, excluding null terminator; Bin32 directory includes trailing separator'
            path_length_note = 'path_lengths uses discovered alias; canonical_path_lengths resolves junctions. Neither proves the actual loaded module path.' }
        system = (Get-SystemSummary); exe = $null; dlls = @(); installation = $null; verification = $null
        compressed_twins = $null; title_packs = @(); collection = @(); warnings = @()
    }
    if ($found) {
        $report.game.path_lengths = Get-PathLengths $Script:GameAlias
        $report.game.canonical_path_lengths = Get-PathLengths $Script:GameRoot
        Add-PathWarnings $report.game.path_lengths 'Discovered alias'
        if ($Script:GameRoot -ne $Script:GameAlias) { Add-PathWarnings $report.game.canonical_path_lengths 'Canonical directory' }
        Write-Host 'Comprobando archivos del mod; puede tardar unos minutos...'
        $report.exe = Get-FileDetails 'Bin32/bio4.exe'
        Add-ExeHeader $report.exe
        $bin = Get-GameFile 'Bin32'
        if ($bin -and [IO.Directory]::Exists($bin)) {
            $report.dlls = @(Get-ChildItem -LiteralPath $bin -Filter '*.dll' -File -Force | Sort-Object Name | ForEach-Object { Get-FileDetails ('Bin32/' + $_.Name) })
            foreach ($name in @('link_runtime.log', 'link_runtime.old.log', 'link_runtime.ini', 'dinput8.ini', 're4_tweaks.ini', 're4_tweaks.log', 'winmm.ini', 'winmm.log')) { Collect-Text ('Bin32/' + $name) }
            $dumps = @(Get-ChildItem -LiteralPath $bin -Filter 'link_crash_*.dmp' -File -Force | Sort-Object LastWriteTimeUtc, Name -Descending | Select-Object -First $MaxDumpCount)
            foreach ($dump in $dumps) {
                $relative = 'Bin32/' + $dump.Name
                $safe = Get-GameFile $relative
                $item = [ordered]@{ path = $relative; bytes = $dump.Length; modified_utc = $dump.LastWriteTimeUtc.ToString('o'); status = 'skipped_unsafe_path' }
                if ($safe -and $dump.Length -gt $MaxDumpBytes) { $item.status = 'skipped_too_large' }
                elseif ($safe) {
                    try {
                        $destination = Join-Path $Script:Stage ('game/' + $relative)
                        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
                        [IO.File]::Copy($safe, $destination, $false)
                        $item.status = 'collected_original_dump'; $item['sha256'] = Get-Hash $destination
                    } catch { $item.status = 'unreadable'; Add-Warning ('Could not copy dump: ' + $relative) }
                }
                [void]$Script:Collection.Add($item)
            }
        }
        $install = Get-Installation
        $report.installation = $install.summary; $report.verification = $install.verification
        $report.compressed_twins = Get-CompressedTwins $install.verification
        $report.title_packs = @(Get-TitlePacks $install.summary)
    } else {
        $report.installation = [ordered]@{ status = 'game_not_found' }
    }
    $report.collection = @($Script:Collection); $report.warnings = @($Script:Warnings)
    Write-Text (Join-Path $Script:Stage 'report.json') ($report | ConvertTo-Json -Depth 12)
    Write-Text (Join-Path $Script:Stage 'LEEME.txt') @'
DIAGNOSTICO LINK IN RE4
Envia este ZIP a Donald de forma privada. No se modifico ni se inicio el juego.
report.json: Windows, GPU, region, VC++, DLL/EXE SHA-256, LAA y verificacion del mod.
game.path_lengths y canonical_path_lengths: solo longitudes UTF-16 de las rutas,
incluido el separador final de Bin32. Un riesgo MAX_PATH no demuestra la causa.
title_packs y compressed_twins: titulo SD/HD y archivos .lfs que siguen junto al mod.
game/: copias de logs, INI y registro de instalacion, con rutas personales redactadas.
Los dumps .dmp se conservan intactos para depurar: pueden incluir memoria y rutas
personales del proceso. No publiques este ZIP. No contiene partidas ni cuentas Steam.
Se recogen como maximo los 3 dumps mas nuevos (por defecto, 64 MiB por dump).
Los limites efectivos de archivos omitidos aparecen en collection; logs largos se recortan.
La verificacion usa installed.json LOCAL: detecta diferencias con esa instalacion,
pero no acredita que el registro sea autentico ni la version oficial publicada.
Un INI cambiado puede ser intencional. Este informe no determina por si solo la causa.
Si game.found=false, repite con Diagnostico-LinkInRE4.bat -GamePath "ruta del juego".
No se recopilan nombres de equipo, usuario, seriales, direcciones IP ni cuentas Steam.
'@
    [void][IO.Directory]::CreateDirectory($OutputDirectory)
    $archive = Join-Path $OutputDirectory ('LinkInRE4-Diagnostico-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6) + '.zip')
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($Script:Stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
    Write-Host ('ZIP creado: ' + $archive)
    Write-Host 'Envialo a Donald de forma privada. No se modifico el juego.'
    exit 0
} catch {
    Write-Host ('ERROR: ' + (Protect-Text $_.Exception.Message)) -ForegroundColor Red
    exit 1
} finally {
    if ($Script:Stage -and [IO.Directory]::Exists($Script:Stage)) {
        # Only the freshly generated GUID-named staging directory is ever removed.
        [IO.Directory]::Delete($Script:Stage, $true)
    }
}
