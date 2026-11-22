<#
.SYNOPSIS
  Builds BLUI for several Unreal Engine versions (default 5.7, 5.8 and 5.9) without the
  builds overwriting each other.

.DESCRIPTION
  Each engine gets its own Blueprint-only host project in BLUIHost/UE_<ver>. Its
  Plugins/BLUI folder is a real folder holding a copy of BLUI.uplugin and junctions to the
  repo's Source, Content, Resources and ThirdParty, so every engine compiles the same
  source but keeps its own Binaries/ and Intermediate/.

  -Mode Editor   compiles the editor modules in each host (Build.bat). Use the host to
                 open the editor / run PIE and automation tests for that engine.
  -Mode Package  runs RunUAT BuildPlugin per engine and writes a distributable plugin to
                 Builds/UE_<ver>/BLUI.

  BLUIHost/ and Builds/ are git-ignored.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File builder/build-engines.ps1 -SetupOnly
  powershell -ExecutionPolicy Bypass -File builder/build-engines.ps1
  powershell -ExecutionPolicy Bypass -File builder/build-engines.ps1 -Engines 5.9 -Mode Package
#>
param(
	[string[]]$Engines = @("5.7", "5.8", "5.9"),
	[ValidateSet("Editor", "Package")]
	[string]$Mode = "Editor",
	[switch]$SetupOnly,
	[string]$EpicRoot = "C:\Program Files\Epic Games"
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Linked = @("Source", "Content", "Resources", "ThirdParty")

function Initialize-Host([string]$Version)
{
	$HostDir = Join-Path $Repo "BLUIHost\UE_$Version"
	$PluginDir = Join-Path $HostDir "Plugins\BLUI"
	New-Item -ItemType Directory -Force $PluginDir | Out-Null

	$Project = Join-Path $HostDir "BLUIHost.uproject"
	$Json = "{`"FileVersion`":3,`"EngineAssociation`":`"$Version`",`"Plugins`":[{`"Name`":`"BLUI`",`"Enabled`":true}]}"
	Set-Content -Path $Project -Value $Json -Encoding Ascii

	# copied every run so version/description edits in the repo reach every host
	Copy-Item (Join-Path $Repo "BLUI.uplugin") $PluginDir -Force

	foreach ($Name in $Linked)
	{
		$Target = Join-Path $Repo $Name
		$Link = Join-Path $PluginDir $Name
		if (-not (Test-Path $Target)) { continue }
		if (Test-Path $Link)
		{
			$Item = Get-Item $Link -Force
			if ($Item.LinkType -ne "Junction") { throw "$Link exists and is not a junction; remove it by hand." }
			continue
		}
		New-Item -ItemType Junction -Path $Link -Target $Target | Out-Null
	}
	return $Project
}

$Results = @()
foreach ($Version in $Engines)
{
	$Engine = Join-Path $EpicRoot "UE_$Version\Engine"
	if (-not (Test-Path $Engine))
	{
		$Results += [pscustomobject]@{ Engine = $Version; Result = "SKIPPED (engine not found at $Engine)" }
		continue
	}

	$Project = Initialize-Host $Version
	if ($SetupOnly)
	{
		$Results += [pscustomobject]@{ Engine = $Version; Result = "host ready: $Project" }
		continue
	}

	$Log = Join-Path (Split-Path $Project) "build-$Mode.log"
	Write-Host "== UE $Version ($Mode) -> $Log"
	# Continue: under "Stop", Windows PowerShell 5.1 aborts on the first stderr line of a redirected native command
	$ErrorActionPreference = "Continue"
	if ($Mode -eq "Editor")
	{
		& (Join-Path $Engine "Build\BatchFiles\Build.bat") UnrealEditor Win64 Development "-Project=$Project" -WaitMutex *>&1 | Out-File -FilePath $Log -Encoding utf8
	}
	else
	{
		$Package = Join-Path $Repo "Builds\UE_$Version\BLUI"
		$Plugin = Join-Path (Split-Path $Project) "Plugins\BLUI\BLUI.uplugin"
		& (Join-Path $Engine "Build\BatchFiles\RunUAT.bat") BuildPlugin "-Plugin=$Plugin" "-Package=$Package" -TargetPlatforms=Win64 *>&1 | Out-File -FilePath $Log -Encoding utf8
	}
	$Code = $LASTEXITCODE
	$ErrorActionPreference = "Stop"
	$Results += [pscustomobject]@{ Engine = $Version; Result = $(if ($Code -eq 0) { "OK" } else { "FAILED (exit $Code, see $Log)" }) }
}

$Results | Format-Table -AutoSize | Out-String | Write-Host
if ($Results | Where-Object { $_.Result -like "FAILED*" }) { exit 1 }
