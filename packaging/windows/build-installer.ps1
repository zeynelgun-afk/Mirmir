param([Parameter(Mandatory=$true)][string]$PayloadRoot,[string]$Version,[string]$OutputRoot)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Version) { $Version=(Get-Content "$Root\packaging\VERSION" -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be MAJOR.MINOR.PATCH.' }
if (-not $OutputRoot) { $OutputRoot=Join-Path $Root '.local\dist\windows-installers' }
$PayloadRoot=(Resolve-Path $PayloadRoot).Path
foreach ($File in @('MirmirShell.exe','runtime\Mirmir\Binaries\Win64\Mirmir-Win64-Shipping.exe','.local\package.json','ASSET-LICENSES.md','native-shell\LICENSE')) {
    if (-not (Test-Path (Join-Path $PayloadRoot $File))) { throw "Missing real Windows payload file: $File; no installer was built." }
}
$Manifest=Get-Content "$PayloadRoot\.local\package.json" -Raw | ConvertFrom-Json
if ($Manifest.platform -ne 'Win64' -or $Manifest.configuration -ne 'Shipping' -or -not $Manifest.packaging_command_succeeded) { throw 'A successful Win64 Shipping manifest is required.' }
if ($Manifest.project_version -ne $Version) { throw 'Installer version must match the version of the cooked Windows game.' }
if (Get-ChildItem $PayloadRoot -Recurse -File | Where-Object { $_.Name -match '^(UnrealEditor|UnrealBuildTool|RunUAT)' -or $_.FullName -match '\\Engine\\(Source|Extras\\ThirdPartyNotUE)\\' }) { throw 'SDK/editor/toolchain files are forbidden in the installer.' }
$Compiler=(Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
if (-not $Compiler) {
    foreach ($Candidate in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe","$env:ProgramFiles\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $Candidate) { $Compiler=$Candidate; break }
    }
}
if (-not $Compiler) { throw 'Install Inno Setup on the Windows build host; no compiler/license installation is automated.' }
New-Item -ItemType Directory -Force $OutputRoot | Out-Null
& $Compiler "/DAppVersion=$Version" "/DPayloadRoot=$PayloadRoot" "/DOutputRoot=$OutputRoot" "$PSScriptRoot\Mirmir.iss"
if ($LASTEXITCODE) { throw 'Inno Setup failed' }
Get-ChildItem "$OutputRoot\*.exe" | Get-FileHash -Algorithm SHA256 | Format-Table -AutoSize
$Sums=Get-ChildItem "$OutputRoot\*.exe" | ForEach-Object { (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()+'  '+$_.Name }
[IO.File]::WriteAllLines("$OutputRoot\SHA256SUMS",$Sums,[Text.UTF8Encoding]::new($false))
