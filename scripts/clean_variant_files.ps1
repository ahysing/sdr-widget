$ErrorActionPreference = 'Stop'
# Remove inner PRECISE/FAST ifdef wrappers from variant files (keep MSVC/int128 ifdefs)

function Clean-VariantFile($path, $outerDefine) {
    $lines = [System.IO.File]::ReadAllLines($path)
    $out = New-Object System.Collections.Generic.List[string]
    $skipDepth = 0
    foreach ($line in $lines) {
        if ($line -match "^\#ifdef $outerDefine\s*$" -or $line -match "^\#if defined\($outerDefine\)\s*$") {
            if ($out.Count -eq 0) {
                $out.Add($line)
            }
            continue
        }
        if ($line -match "^\#endif /\* $outerDefine \*/\s*$") {
            continue
        }
        $out.Add($line)
    }
    # Remove stray #endif that closed inner PRECISE/FAST blocks (line is exactly #endif)
    $final = New-Object System.Collections.Generic.List[string]
    $pendingIfdef = $false
    foreach ($line in $out) {
        if ($line -eq '#endif' -and $pendingIfdef) {
            $pendingIfdef = $false
            continue
        }
        if ($line -match '^\#ifdef PRECISE\s*$' -or $line -match '^\#ifdef FAST\s*$' -or $line -match '^\#if defined\(PRECISE\)\s*$' -or $line -match '^\#if defined\(FAST\)\s*$') {
            $pendingIfdef = $true
            continue
        }
        $final.Add($line)
    }
    [System.IO.File]::WriteAllLines($path, $final)
}

Clean-VariantFile 'src/loudness_precise.c' 'PRECISE'
Clean-VariantFile 'src/loudness_fast.c' 'FAST'

# Remove FAST scale block accidentally included in precise file
$precise = Get-Content 'src/loudness_precise.c' -Raw
$precise = $precise -replace '(?ms)#if defined\(FAST\).*?^#endif\s*\r?\n', ''
Set-Content 'src/loudness_precise.c' -Value $precise -NoNewline

# Fix precise slice end - re-append change_frequency if missing
if ($precise -notmatch 'loudness_change_frequency_precise') {
    Write-Warning 'loudness_change_frequency_precise missing - manual fix needed'
}

Write-Host 'Cleaned variant files'
