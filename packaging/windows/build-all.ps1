param([Parameter(Mandatory=$true)][string]$UnrealRoot,[string]$Version)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Version) { $Version=(Get-Content "$Root\packaging\VERSION" -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be MAJOR.MINOR.PATCH.' }
$Project="$Root\unreal\Mirmir\Mirmir.uproject"
$Config="$Root\unreal\Mirmir\Config\DefaultGame.ini"
$ProjectVersion=(Select-String -Path $Config -Pattern '^ProjectVersion=(.+)$').Matches.Groups[1].Value
if ($ProjectVersion -ne $Version) { throw 'Installer version must match DefaultGame.ini ProjectVersion.' }
$Stamp=Get-Date -Format 'yyyyMMddTHHmmss'
$Build="$Root\.local\dist\windows-$Stamp"
if (-not (Test-Path "$UnrealRoot\Engine\Binaries\Win64\UnrealEditor.exe")) { throw 'A separately licensed installed Windows UE5.8.4 build host is required.' }
& "$UnrealRoot\Engine\Build\BatchFiles\Build.bat" MirmirEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE) { throw 'Windows editor module build failed' }
& "$UnrealRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$Project" -platform=Win64 -clientconfig=Shipping -nocompileeditor -unattended -NoP4 -UTF8Output -map=/Game/Mirmir/Preview/MirmirPreview -build -cook -stage -pak -package -archive "-archivedirectory=$Build\cooked"
if ($LASTEXITCODE) { throw 'Windows Shipping build/cook failed; no installer will be built.' }
if ((Select-String -Path $Config -Pattern '^ProjectVersion=(.+)$').Matches.Groups[1].Value -ne $ProjectVersion) { throw 'Project version changed during the cook; no installer will be built.' }
& "$PSScriptRoot\build-shell.ps1" -OutputRoot "$Build\shell"
$Payload="$Build\payload"
New-Item -ItemType Directory -Force "$Payload\.local" | Out-Null
Copy-Item "$Build\shell\MirmirShell\*" $Payload -Recurse
$Binary=Get-ChildItem "$Build\cooked" -Recurse -Filter Mirmir-Win64-Shipping.exe | Select-Object -First 1
if (-not $Binary) { throw 'Cooked Windows Shipping executable missing' }
$CookedRoot=$Binary.Directory.Parent.Parent.Parent.FullName
Copy-Item $CookedRoot "$Payload\runtime" -Recurse
New-Item -ItemType Directory -Force "$Payload\native-shell" | Out-Null
Copy-Item "$Root\native-shell\mirmir_shell.py","$Root\native-shell\README.md","$Root\native-shell\LICENSE" "$Payload\native-shell"
Copy-Item "$Root\ASSET-LICENSES.md","$Root\LICENSE" $Payload
New-Item -ItemType Directory -Force "$Payload\licenses","$Payload\source" | Out-Null
Copy-Item "$Root\packaging\THIRD-PARTY-NOTICES.md","$Root\packaging\runtime-lock.json","$Root\packaging\requirements-windows.txt" "$Payload\licenses"
Copy-Item "$Root\packaging" "$Payload\source\packaging" -Recurse
Copy-Item "$Root\assets\personal-use\sources.json" "$Payload\licenses\cat-original-sources.json"
Copy-Item "$Root\assets\provenance\cat-walk-personal\original\license.txt" "$Payload\licenses\cat-original-license.txt"
Copy-Item "$Root\assets\provenance\tuxedo-motion\LICENSE.md" "$Payload\licenses\additional-motion-license.md"
Copy-Item "$Root\assets\generated\fur-detail-v1.json" "$Payload\licenses\generated-fur-provenance.json"
$Manifest=@{schema_version=1;configuration='Shipping';platform='Win64';project_version=$ProjectVersion;packaging_command_succeeded=$true;archive_directory='runtime';binary='runtime/Mirmir/Binaries/Win64/Mirmir-Win64-Shipping.exe';launcher='runtime/Mirmir.exe';map='/Game/Mirmir/Preview/MirmirPreview'}
[IO.File]::WriteAllText("$Payload\.local\package.json",($Manifest | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
& "$PSScriptRoot\build-installer.ps1" -PayloadRoot $Payload -Version $Version -OutputRoot "$Build\installers"
Write-Output "Windows artifacts: $Build\installers"
