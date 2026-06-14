# Nordstjernen - fetch Android prebuilt dependency sysroots.

param(
    [string]$Sysroot = "$env:USERPROFILE\.cache\nordstjernen-android-sysroot",
    [string]$Repo = "nordstjernen-web/nordstjernen-android",
    [string]$Branch = "main",
    [string]$RunId = "",
    [string[]]$Abi = @("arm64-v8a", "x86_64"),
    [string]$Token = $env:GITHUB_TOKEN
)

$ErrorActionPreference = "Stop"

function Write-Step($Message) {
    Write-Host "[deps] $Message"
}

function Invoke-GitHubJson($Url) {
    $headers = @{
        "User-Agent" = "nordstjernen-android-fetch"
        "Accept" = "application/vnd.github+json"
    }
    if ($Token) {
        $headers["Authorization"] = "Bearer $Token"
    }
    Invoke-RestMethod -Headers $headers -Uri $Url
}

function Invoke-GitHubDownload($Url, $OutFile) {
    $headers = @{
        "User-Agent" = "nordstjernen-android-fetch"
        "Accept" = "application/vnd.github+json"
    }
    if ($Token) {
        $headers["Authorization"] = "Bearer $Token"
    }
    Invoke-WebRequest -Headers $headers -MaximumRedirection 5 -Uri $Url -OutFile $OutFile
}

$validAbi = @("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
foreach ($item in $Abi) {
    if ($validAbi -notcontains $item) {
        throw "invalid ABI: $item"
    }
}

New-Item -ItemType Directory -Force -Path $Sysroot | Out-Null
$Sysroot = (Resolve-Path $Sysroot).Path
$logDir = Join-Path (Resolve-Path "$PSScriptRoot\..") ".build\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $logDir "fetch-prebuilt-deps-$stamp.log"
Start-Transcript -Path $log -Force | Out-Null

try {
    Write-Step "repo: $Repo"
    Write-Step "branch: $Branch"
    Write-Step "sysroot: $Sysroot"
    Write-Step "abis: $($Abi -join ', ')"

    if (-not $RunId) {
        $runsUrl = "https://api.github.com/repos/$Repo/actions/workflows/build-deps.yml/runs?branch=$Branch&status=success&per_page=1"
        Write-Step "resolving latest successful workflow run"
        $runs = Invoke-GitHubJson $runsUrl
        if (-not $runs.workflow_runs -or $runs.workflow_runs.Count -eq 0) {
            throw "no successful build-deps workflow run found for $Repo@$Branch"
        }
        $RunId = [string]$runs.workflow_runs[0].id
    }
    Write-Step "run: $RunId"

    $artifacts = Invoke-GitHubJson "https://api.github.com/repos/$Repo/actions/runs/$RunId/artifacts?per_page=100"
    if (-not $artifacts.artifacts) {
        throw "workflow run $RunId has no artifacts"
    }

    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) "nordstjernen-android-deps-$stamp"
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null

    foreach ($item in $Abi) {
        $name = "nordstjernen-android-sysroot-$item"
        $artifact = $artifacts.artifacts | Where-Object { $_.name -eq $name } | Select-Object -First 1
        if (-not $artifact) {
            throw "artifact not found: $name"
        }

        $zip = Join-Path $tmp "$name.zip"
        $unzip = Join-Path $tmp $name
        Write-Step "downloading $name"
        Invoke-GitHubDownload $artifact.archive_download_url $zip
        New-Item -ItemType Directory -Force -Path $unzip | Out-Null
        Expand-Archive -Force -Path $zip -DestinationPath $unzip

        $source = Join-Path $unzip $item
        if (-not (Test-Path $source)) {
            $source = $unzip
        }

        $dest = Join-Path $Sysroot $item
        $destFull = [System.IO.Path]::GetFullPath($dest)
        $sysrootPrefix = $Sysroot.TrimEnd("\") + "\"
        if (-not $destFull.StartsWith($sysrootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "refusing to replace path outside sysroot: $destFull"
        }
        if (Test-Path $dest) {
            Remove-Item -Recurse -Force -LiteralPath $dest
        }
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item -Recurse -Force -Path (Join-Path $source "*") -Destination $dest
        Write-Step "installed $item -> $dest"
    }

    Write-Step "done"
    Write-Step "log: $log"
} catch {
    Write-Step "failed: $($_.Exception.Message)"
    Write-Step "log: $log"
    throw
} finally {
    Stop-Transcript | Out-Null
}
