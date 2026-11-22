using System.Collections.Generic;
using UnrealBuildTool;
using System.IO;
using System;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

public class Blu : ModuleRules
{

	// True when the project's .uproject enables the engine's Web Browser plugin (which links the engine CEF3)
	private bool ProjectEnablesWebBrowser(ReadOnlyTargetRules Target)
	{
		if (Target.ProjectFile == null || !FileReference.Exists(Target.ProjectFile))
		{
			return false;
		}
		JsonObject[] Plugins;
		if (!JsonObject.Read(Target.ProjectFile).TryGetObjectArrayField("Plugins", out Plugins))
		{
			return false;
		}
		foreach (JsonObject Plugin in Plugins)
		{
			string Name;
			bool bEnabled;
			if (Plugin.TryGetStringField("Name", out Name) && Name == "WebBrowserWidget" && Plugin.TryGetBoolField("Enabled", out bEnabled) && bEnabled)
			{
				return true;
			}
		}
		return false;
	}

	private string ThirdPartyPath
	{
		get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/")); }
	}

	private void stageFiles(String[] FilesToStage)
	{
		foreach (var File in FilesToStage)
		{
			RuntimeDependencies.Add(File);
		}
	}

	public Blu(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Projects",
				"InputCore",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"UMG",
				"Json",
				"RenderCore"
			});

		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(ModuleDirectory, "Private"),
			});

		// A monolithic exe (packaged game) can't hold two CEF SDKs: their symbols collide at link time
		bool bEngineCefInSameBinary = Target.LinkType == TargetLinkType.Monolithic && ProjectEnablesWebBrowser(Target);
		PrivateDefinitions.Add("BLU_ENGINE_CEF_IN_SAME_BINARY=" + (bEngineCefInSameBinary ? "1" : "0"));
		if (bEngineCefInSameBinary)
		{
			Logger.LogWarning("BLUI: the project enables the engine's Web Browser plugin. In packaged (monolithic) builds both CEF SDKs end up in one exe and collide, so BLUI disables itself at startup. Disable the Web Browser plugin (WebBrowserWidget) to use BLUI in packaged games.");
		}

		if(Target.Platform == UnrealTargetPlatform.Win64)
		{

			// BLUI's CEF runtime is renamed (libcef.dll -> blucef.dll, chrome_elf.dll -> blucef_elf.dll, see builder/isolate_cef.ps1)
			// so it can't collide with the engine's own CEF (WebBrowser module) which is loaded into the same process by name.
			PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyPath, "cef/Win/lib", "blucef.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyPath, "cef/Win/lib", "libcef_dll_wrapper.lib"));

			PublicDelayLoadDLLs.Add("blucef.dll");

			PublicIncludePaths.AddRange(
				new string[] {
					Path.Combine(ThirdPartyPath, "cef/Win")
				});

			// Add our runtime dependencies
			var FilesToStage = Directory.GetFiles(Path.Combine(ThirdPartyPath, "cef/Win/shipping"), "*", SearchOption.AllDirectories);
			stageFiles(FilesToStage);

		} else if(Target.Platform == UnrealTargetPlatform.Linux)
		{

			PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyPath, "cef/Linux/lib", "libcef.so"));
			PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyPath, "cef/Linux/lib", "libcef_dll_wrapper.a"));

			PublicIncludePaths.AddRange(
				new string[] {
					Path.Combine(ThirdPartyPath, "cef/Linux")
				});

		} else if(Target.Platform == UnrealTargetPlatform.Mac)
		{
			
			var FrameworkPath = Path.Combine(ThirdPartyPath, "cef/Mac/lib", "Chromium Embedded Framework.framework");

			PublicFrameworks.Add(FrameworkPath);
			PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyPath, "cef/Mac/lib", "libcef_dll_wrapper.a"));

			PublicIncludePaths.AddRange(
				new string[] {
					Path.Combine(ThirdPartyPath, "cef", "Mac")
				});

			var FilesToStage = Directory.GetFiles(Path.Combine(ThirdPartyPath, "cef/Mac/shipping"), "*", SearchOption.AllDirectories);
			stageFiles(FilesToStage);

			FilesToStage = Directory.GetFiles(Path.Combine(ThirdPartyPath, "cef/Mac/lib"), "*", SearchOption.AllDirectories);
			stageFiles(FilesToStage);

			if(!Target.bBuildEditor)
			{
				AdditionalBundleResources.Add(new BundleResource(Path.Combine(FrameworkPath, "Chromium Embedded Framework"), "MacOS", false));
			}
		}
		else
		{
			throw new BuildException("BLUI: Platform not supported");
		}
	}
}
