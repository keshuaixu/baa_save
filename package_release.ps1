param(
    [string]$PackageName = "baa_save_win64",
    [string]$OutputRoot = "release",
    [string]$ToolboxVersion = "1.0.0",
    [string]$ToolboxIdentifier = "baasave",
    [switch]$NoSource
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function To-MatlabStringLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ScriptDir = [System.IO.Path]::GetFullPath($ScriptDir)

$OutputRootPath = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir $OutputRoot))
if (-not (Test-Path -LiteralPath $OutputRootPath -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputRootPath | Out-Null
}

$OutputFile = Join-Path $OutputRootPath ($PackageName + ".mltbx")
if (Test-Path -LiteralPath $OutputFile -PathType Leaf) {
    Remove-Item -LiteralPath $OutputFile -Force
}

$ToolboxFiles = @(
    (Join-Path $ScriptDir "baa_save.mexw64"),
    (Join-Path $ScriptDir "README.md")
)
if (-not $NoSource) {
    $ToolboxFiles += (Join-Path $ScriptDir "baa_save.cpp")
}

foreach ($FilePath in $ToolboxFiles) {
    if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
        throw "Missing required file: $FilePath"
    }
}

$toolboxFilesLiteral = "{" + (($ToolboxFiles | ForEach-Object { To-MatlabStringLiteral $_ }) -join ",") + "}"
$scriptDirLiteral = To-MatlabStringLiteral $ScriptDir
$outputFileLiteral = To-MatlabStringLiteral $OutputFile
$identifierLiteral = To-MatlabStringLiteral $ToolboxIdentifier
$versionLiteral = To-MatlabStringLiteral $ToolboxVersion
$nameLiteral = To-MatlabStringLiteral "baa_save"
$summaryLiteral = To-MatlabStringLiteral "Ultra-fast MATLAB MEX writer for NumPy .npy"
$descriptionLiteral = To-MatlabStringLiteral "Ultra-fast MATLAB MEX writer for NumPy .npy files on Windows."
$authorLiteral = To-MatlabStringLiteral "baa_save"

$matlabCmd = @"
opts = matlab.addons.toolbox.ToolboxOptions($scriptDirLiteral, $identifierLiteral);
opts.ToolboxName = $nameLiteral;
opts.ToolboxVersion = $versionLiteral;
opts.Summary = $summaryLiteral;
opts.Description = $descriptionLiteral;
opts.AuthorName = $authorLiteral;
opts.ToolboxFiles = $toolboxFilesLiteral;
opts.ToolboxMatlabPath = {$scriptDirLiteral};
opts.OutputFile = $outputFileLiteral;
matlab.addons.toolbox.packageToolbox(opts);
disp(opts.OutputFile);
"@

& matlab -batch $matlabCmd
if ($LASTEXITCODE -ne 0) {
    throw "MATLAB packaging failed with exit code $LASTEXITCODE"
}

Write-Host "Toolbox package created:"
Write-Host ("  " + $OutputFile)
Write-Host "Included files:"
foreach ($FilePath in $ToolboxFiles) {
    Write-Host ("  - " + [System.IO.Path]::GetFileName($FilePath))
}
