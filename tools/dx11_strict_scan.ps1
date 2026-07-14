param(
    [string]$RepoRoot = "",
    [string]$ManifestPath = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $RepoRoot "tools/dx11_strict_scan_manifest.txt"
}

# Runtime symbols and quality markers forbidden in DX11 strict runtime paths.
$patterns = @(
    "SetTextureStageState(",
    "SetFVF(",
    "DrawPrimitiveUP(",
    "D3DFVF_",
    "D3DTSS_",
    "IDirect3DDevice9",
    "IDirect3DDevice9Ex",
    "D3DLIGHT9",
    "TODO",
    "todo",
    "FIXME",
    "fixme",
    "noop",
    "no-op",
    "stub",
    "placeholder"
)

$excludeRegex = @(
    "extern\\include",
    "extern\\third_party",
    "DDSTextureLoader9"
)

$sourceExtensions = @(
    ".h", ".hpp", ".hxx", ".inl",
    ".c", ".cc", ".cpp", ".cxx",
    ".fx", ".hlsl", ".psh", ".vsh"
)

# Disallow empty function bodies in strict runtime files.
# This catches placeholder/no-op callbacks that bypass the token scan.
$emptyFunctionRegex = '^\s*(?:(?:virtual|static|inline|constexpr)\s+)*(?:[\w:<>~\*\&]+\s+)+[\w:<>~]+\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{\s*\}\s*(?://.*)?$'
$statementKeywords = @("if", "for", "while", "switch", "catch")

$scanTargets = @()
if (Test-Path $ManifestPath) {
    Get-Content -Path $ManifestPath | ForEach-Object {
        $line = $_.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) { return }
        if ($line.StartsWith("#")) { return }
        $scanTargets += $line
    }
}

if ($scanTargets.Count -eq 0) {
    Write-Host "DX11_STRICT_GATE FAILED: empty or missing manifest at $ManifestPath"
    exit 2
}

$hits = New-Object System.Collections.Generic.List[string]
$resolvedTargetCount = 0
foreach ($target in $scanTargets) {
    $full = Join-Path $RepoRoot $target
    if (-not (Test-Path $full)) { continue }
    ++$resolvedTargetCount

    $candidateFiles = @()
    $item = Get-Item -LiteralPath $full
    if ($item.PSIsContainer) {
        $candidateFiles = Get-ChildItem -LiteralPath $full -Recurse -File -ErrorAction SilentlyContinue
    } else {
        $candidateFiles = @($item)
    }

    foreach ($file in $candidateFiles) {
        if ($sourceExtensions -notcontains $file.Extension.ToLowerInvariant()) {
            continue
        }

        $filePath = $file.FullName
        $skipFile = $false
        foreach ($rx in $excludeRegex) {
            if ($filePath -match $rx) {
                $skipFile = $true
                break
            }
        }
        if ($skipFile) {
            continue
        }

        $resolvedPath = (Resolve-Path -LiteralPath $filePath).Path
        $relativePath = $resolvedPath
        if ($resolvedPath.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            $relativePath = $resolvedPath.Substring($RepoRoot.Length).TrimStart('\', '/')
        }

        foreach ($pattern in $patterns) {
            $matches = Select-String -LiteralPath $resolvedPath -SimpleMatch -Pattern $pattern -ErrorAction SilentlyContinue
            foreach ($match in $matches) {
                $lineText = $match.Line.Trim()
                if ([string]::IsNullOrWhiteSpace($lineText)) {
                    continue
                }

                $entry = "[${target}] ${relativePath}:$($match.LineNumber): $lineText"
                $hits.Add($entry) | Out-Null
            }
        }

        $emptyMatches = Select-String -LiteralPath $resolvedPath -Pattern $emptyFunctionRegex -ErrorAction SilentlyContinue
        foreach ($match in $emptyMatches) {
            $lineText = $match.Line.Trim()
            if ([string]::IsNullOrWhiteSpace($lineText)) {
                continue
            }

            $isStatement = $false
            foreach ($kw in $statementKeywords) {
                if ($lineText.StartsWith("$kw(") -or $lineText.StartsWith("$kw (")) {
                    $isStatement = $true
                    break
                }
            }
            if ($isStatement) {
                continue
            }

            $entry = "[${target}] ${relativePath}:$($match.LineNumber): EMPTY_FUNCTION_BODY forbidden in DX11 strict runtime scope"
            $hits.Add($entry) | Out-Null
        }
    }
}

if ($resolvedTargetCount -eq 0) {
    Write-Host "DX11_STRICT_GATE FAILED: no manifest targets resolved under RepoRoot=$RepoRoot"
    exit 2
}

if ($hits.Count -gt 0) {
    Write-Host "DX11_STRICT_GATE FAILED: detected forbidden DX9 runtime symbols/quality markers in strict-migrated targets."
    $hits | Sort-Object -Unique | Select-Object -First 300 | ForEach-Object { Write-Host $_ }
    exit 1
}

Write-Host "DX11_STRICT_GATE PASS: no forbidden DX9 runtime symbols/quality markers in strict-migrated targets."
exit 0
