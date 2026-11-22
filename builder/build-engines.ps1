<#
.SYNOPSIS
  Builds BLUI for several Unreal Engine versions (default 5.7, 5.8 and 5.9) without the
  builds overwriting each other.

.DESCRIPTION
  Each engine gets its own host project. Its Plugins/BLUI folder is a real folder holding
  copies of BLUI.uplugin and the docs plus junctions to the repo's Source, Content,
  Resources, ThirdParty and Config, so every engine compiles the same source but keeps its
  own Binaries/ and Intermediate/.

  -Mode Editor   compiles the editor modules in the Blueprint-only host BLUIHost/UE_<ver>.
                 Use it to open the editor / run PIE and automation tests for that engine.
  -Mode Package  runs RunUAT BuildPlugin and writes a distributable plugin to
                 Builds/UE_<ver>/BLUI.
  -Mode Game     packages a small C++ game (BLUIHost/Game_UE_<ver>, strictest build settings)
                 with BuildCookRun for each -GameConfigs entry, archived to
                 Builds/Game_UE_<ver>/<config>. -WithWebBrowser also enables the engine's
                 WebBrowserWidget (BLUIHost/GameWB_UE_<ver>, Builds/GameWB_UE_<ver>) to check
                 that combination; see README "Packaging a game".

  BLUIHost/ and Builds/ are git-ignored.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File builder/build-engines.ps1 -SetupOnly
  powershell -ExecutionPolicy Bypass -File builder/build-engines.ps1
  powershell -ExecutionPolicy Bypass -File builder/build-engines.ps1 -Engines 5.9 -Mode Package
  powershell -ExecutionPolicy Bypass -File builder/build-engines.ps1 -Mode Game -GameConfigs Shipping
#>
param(
	[string[]]$Engines = @("5.7", "5.8", "5.9"),
	[ValidateSet("Editor", "Package", "Game")]
	[string]$Mode = "Editor",
	[string[]]$GameConfigs = @("Development", "Shipping"),
	[switch]$WithWebBrowser,
	[switch]$SetupOnly,
	[string]$EpicRoot = "C:\Program Files\Epic Games"
)

$ErrorActionPreference = "Stop"
# powershell -File passes "-Engines 5.8,5.9" as one string
$Engines = $Engines | ForEach-Object { $_ -split "," }
$GameConfigs = $GameConfigs | ForEach-Object { $_ -split "," }
$Repo =(Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Linked = @("Source", "Content", "Resources", "ThirdParty", "Config")
$GameTag = if ($WithWebBrowser) { "GameWB" } else { "Game" }

function Write-IfMissing([string]$Path, [string]$Text)
{
	if (Test-Path $Path) { return }
	New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
	Set-Content -Path $Path -Value $Text -Encoding Ascii
}

function Initialize-Host([string]$Version, [bool]$Game)
{
	if ($Game)
	{
		$HostDir = Join-Path $Repo "BLUIHost\$GameTag`_UE_$Version"
		$Project = Join-Path $HostDir "PackGame.uproject"
		$WebBrowserJson = if ($WithWebBrowser) { ",{`"Name`":`"WebBrowserWidget`",`"Enabled`":true}" } else { "" }
		$Json = "{`"FileVersion`":3,`"EngineAssociation`":`"$Version`",`"Modules`":[{`"Name`":`"PackGame`",`"Type`":`"Runtime`",`"LoadingPhase`":`"Default`"}],`"Plugins`":[{`"Name`":`"BLUI`",`"Enabled`":true}$WebBrowserJson]}"
		foreach ($Kind in @("Game", "Editor"))
		{
			$Name = if ($Kind -eq "Game") { "PackGame" } else { "PackGameEditor" }
			Write-IfMissing (Join-Path $HostDir "Source\$Name.Target.cs") @"
using UnrealBuildTool;

public class ${Name}Target : TargetRules
{
	public ${Name}Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.$Kind;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("PackGame");
	}
}
"@
		}
		Write-IfMissing (Join-Path $HostDir "Source\PackGame\PackGame.Build.cs") @"
using UnrealBuildTool;

public class PackGame : ModuleRules
{
	public PackGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
"@
		Write-IfMissing (Join-Path $HostDir "Source\PackGame\PackGame.cpp") @"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, PackGame, "PackGame");
"@
	}
	else
	{
		$HostDir = Join-Path $Repo "BLUIHost\UE_$Version"
		$Project = Join-Path $HostDir "BLUIHost.uproject"
		$Json = "{`"FileVersion`":3,`"EngineAssociation`":`"$Version`",`"Plugins`":[{`"Name`":`"BLUI`",`"Enabled`":true}]}"
	}

	$PluginDir = Join-Path $HostDir "Plugins\BLUI"
	New-Item -ItemType Directory -Force $PluginDir | Out-Null
	Set-Content -Path $Project -Value $Json -Encoding Ascii

	# copied every run so edits in the repo reach every host (and the packaged plugin)
	foreach ($File in @("BLUI.uplugin", "README.md", "LICENSE", "CREDITS.md"))
	{
		Copy-Item (Join-Path $Repo $File) $PluginDir -Force
	}

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

# Runs a .bat with its output in $Log; returns the exit code
function Invoke-Logged([string]$Log, [string]$Bat, [string[]]$Arguments)
{
	Write-Host "   $([IO.Path]::GetFileName($Bat)) -> $Log"
	# Continue: under "Stop", Windows PowerShell 5.1 aborts on the first stderr line of a redirected native command
	$ErrorActionPreference = "Continue"
	& $Bat @Arguments *>&1 | Out-File -FilePath $Log -Encoding utf8
	return $LASTEXITCODE
}

$Results = @()
foreach ($Version in $Engines)
{
	$Engine = Join-Path $EpicRoot "UE_$Version\Engine"
	if (-not (Test-Path $Engine))
	{
		$Results += [pscustomobject]@{ Engine = $Version; Step = $Mode; Result = "SKIPPED (engine not found at $Engine)" }
		continue
	}

	$Project = Initialize-Host $Version ($Mode -eq "Game")
	$HostDir = Split-Path $Project
	if ($SetupOnly)
	{
		$Results += [pscustomobject]@{ Engine = $Version; Step = "setup"; Result = "ready: $Project" }
		continue
	}

	Write-Host "== UE $Version ($Mode)"
	$BuildBat = Join-Path $Engine "Build\BatchFiles\Build.bat"
	$RunUAT = Join-Path $Engine "Build\BatchFiles\RunUAT.bat"
	$Steps = @()
	if ($Mode -eq "Editor")
	{
		$Steps += ,@("Editor", $BuildBat, @("UnrealEditor", "Win64", "Development", "-Project=$Project", "-WaitMutex"))
	}
	elseif ($Mode -eq "Package")
	{
		$Package = Join-Path $Repo "Builds\UE_$Version\BLUI"
		$Steps += ,@("Package", $RunUAT, @("BuildPlugin", "-Plugin=$HostDir\Plugins\BLUI\BLUI.uplugin", "-Package=$Package", "-TargetPlatforms=Win64"))
	}
	else
	{
		# the cook runs in the project's editor, so its editor modules must exist first
		$Steps += ,@("GameEditor", $BuildBat, @("PackGameEditor", "Win64", "Development", "-Project=$Project", "-WaitMutex"))
		foreach ($Config in $GameConfigs)
		{
			$Archive = Join-Path $Repo "Builds\$GameTag`_UE_$Version\$Config"
			$Steps += ,@("Game-$Config", $RunUAT, @("BuildCookRun", "-project=$Project", "-noP4", "-platform=Win64", "-clientconfig=$Config", "-build", "-cook", "-stage", "-pak", "-archive", "-archivedirectory=$Archive", "-nocompileeditor", "-unattended", "-utf8output"))
		}
	}

	$Failed = $false
	foreach ($Step in $Steps)
	{
		$Log = Join-Path $HostDir "build-$($Step[0]).log"
		$Code = Invoke-Logged $Log $Step[1] $Step[2]
		$Results += [pscustomobject]@{ Engine = $Version; Step = $Step[0]; Result = $(if ($Code -eq 0) { "OK" } else { "FAILED (exit $Code, see $Log)" }) }
		if ($Code -ne 0) { $Failed = $true; break }
	}

	# Run the Blu.Browser tests inside the packaged Development exe (inner binary: the stub launcher would open a window)
	$Exe = Join-Path $Repo "Builds\$GameTag`_UE_$Version\Development\Windows\PackGame\Binaries\Win64\PackGame.exe"
	if ($Mode -eq "Game" -and -not $Failed -and $GameConfigs -contains "Development" -and (Test-Path $Exe))
	{
		$GameLog = Join-Path (Split-Path (Split-Path (Split-Path $Exe))) "Saved\Logs\PackGame.log"
		Remove-Item $GameLog -ErrorAction SilentlyContinue
		Write-Host "   PackGame.exe (Blu.Browser tests) -> $GameLog"
		$Game = Start-Process $Exe -ArgumentList '-ExecCmds="Automation RunTests Blu.Browser;Quit"', "-RenderOffscreen", "-nosplash", "-unattended", "-nosound", "-ResX=1280", "-ResY=720", "-log" -WindowStyle Hidden -PassThru
		if (-not $Game.WaitForExit(300000)) { $Game.Kill() }
		$Passed = if (Test-Path $GameLog) { (Select-String $GameLog -Pattern "Result=\{Success\} Name=\{\w+\} Path=\{Blu\.Browser\.").Count } else { 0 }
		$Crashed = if (Test-Path $GameLog) { (Select-String $GameLog -Pattern "Critical error").Count } else { 1 }
		$Failed = if (Test-Path $GameLog) { (Select-String $GameLog -Pattern "Result=\{Fail\}").Count } else { 1 }
		$Disabled = if (Test-Path $GameLog) { (Select-String $GameLog -Pattern "STATUS: BLUI disabled").Count } else { 0 }
		if ($WithWebBrowser)
		{
			# expected: BLUI turns itself off instead of crashing (see README "Packaging a game")
			$Ok = $Crashed -eq 0 -and $Disabled -gt 0
			$Text = if ($Ok) { "OK (BLUI disabled itself, no crash)" } else { "FAILED (crash=$Crashed disabled=$Disabled, see $GameLog)" }
		}
		else
		{
			$Ok = $Passed -ge 3 -and $Failed -eq 0 -and $Crashed -eq 0
			$Text = if ($Ok) { "OK ($Passed passed)" } else { "FAILED ($Passed passed, see $GameLog)" }
		}
		$Results += [pscustomobject]@{ Engine = $Version; Step = "Game-Tests"; Result = $Text }
	}
}

$Results | Format-Table -AutoSize | Out-String | Write-Host
if ($Results | Where-Object { $_.Result -like "FAILED*" }) { exit 1 }
