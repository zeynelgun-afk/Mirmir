param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $IsWindows -and $env:OS -ne 'Windows_NT') { throw 'Windows shell builds require an actual Windows host.' }
$Venv=Join-Path $Root '.local\windows-build-venv'
if (-not (Test-Path "$Venv\Scripts\python.exe")) { python -m venv $Venv; if ($LASTEXITCODE) { throw 'venv failed' } }
& "$Venv\Scripts\python.exe" -m pip install --requirement "$Root\packaging\requirements-windows.txt"
if ($LASTEXITCODE) { throw 'Dependency installation failed' }
& "$Venv\Scripts\python.exe" -m PyInstaller --noconfirm --clean --distpath $OutputRoot --workpath "$Root\.local\windows-pyinstaller" "$PSScriptRoot\mirmir-shell.spec"
if ($LASTEXITCODE) { throw 'PyInstaller failed' }
$Process=Start-Process -FilePath "$OutputRoot\MirmirShell\MirmirShell.exe" -ArgumentList '--self-test' -Wait -PassThru
if ($Process.ExitCode) { throw 'Frozen Windows shell self-test failed' }
$Licenses="$OutputRoot\MirmirShell\licenses"
New-Item -ItemType Directory -Force $Licenses | Out-Null
Copy-Item "$Root\native-shell\LICENSE" "$Licenses\native-shell-GPL-3.txt"
Get-ChildItem "$Venv\Lib\site-packages\*.dist-info\licenses" | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $Licenses $_.Parent.Name) -Recurse -Force
}
