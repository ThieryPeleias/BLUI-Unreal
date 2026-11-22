#include "IBlu.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "BluManager.h"

#if PLATFORM_WINDOWS && IS_MONOLITHIC
#include "Windows/AllowWindowsPlatformTypes.h"
#include <delayimp.h>
#include "Windows/HideWindowsPlatformTypes.h"

/**
 * In a monolithic exe that also links the engine's CEF3 (WebBrowser module), both CEF SDKs are statically linked into one
 * binary and their symbols collide, so the linker binds every CEF call, BLUI's and the engine's, to one of the two imports.
 * - Bound to "blucef.dll": the engine WebBrowser calls it at startup, before BluLoader has loaded our runtime, and the
 *   delay-load fails. Load BLUI's runtime by full path instead of crashing.
 * - Bound to "libcef.dll": if that DLL can't be loaded when BLUI initializes, hand back our already loaded runtime.
 * Either way the engine WebBrowser then reports a CEF version mismatch and stays disabled (same behavior as BLUI <= 5.0.0).
 */
static HMODULE LoadBluCefRuntime()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BLUI"));
	if (!Plugin.IsValid())
	{
		return nullptr;
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("ThirdParty/cef/Win/shipping"));
	FPlatformProcess::PushDllDirectory(*Dir);
	HMODULE Handle = (HMODULE)FPlatformProcess::GetDllHandle(*(Dir / TEXT("blucef.dll")));
	FPlatformProcess::PopDllDirectory(*Dir);
	return Handle;
}

// Set when the hook proves the engine CEF3 is linked in too (e.g. pulled in by a plugin other than WebBrowserWidget)
static bool bEngineCefLinked = false;

static FARPROC WINAPI BluDelayLoadFailureHook(unsigned Notify, PDelayLoadInfo Info)
{
	if (Notify == dliFailLoadLib && Info && Info->szDll && _stricmp(Info->szDll, "blucef.dll") == 0)
	{
		if (HMODULE BluCef = LoadBluCefRuntime())
		{
			bEngineCefLinked = true;
			UE_LOG(LogBlu, Warning, TEXT("Engine CEF3 is linked into this monolithic build and called BLUI's CEF runtime first; loaded it early so the game doesn't crash."));
			return (FARPROC)BluCef;
		}
	}
	if (Notify == dliFailLoadLib && Info && Info->szDll && _stricmp(Info->szDll, "libcef.dll") == 0)
	{
		if (HMODULE BluCef = GetModuleHandleW(L"blucef.dll"))
		{
			bEngineCefLinked = true;
			UE_LOG(LogBlu, Warning, TEXT("Engine CEF3 is linked into this monolithic build, sharing BLUI's CEF runtime with it."));
			return (FARPROC)BluCef;
		}
	}
	return nullptr;
}

extern "C" const PfnDliHook __pfnDliFailureHook2 = BluDelayLoadFailureHook;
#endif

class FBlu : public IBlu
{

	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		// Two CEF SDKs in one exe collide at link time; CefInitialize then fails or crashes (seen on UE 5.8 and 5.9)
		bool bEngineCefInSameBinary = BLU_ENGINE_CEF_IN_SAME_BINARY != 0;
	#if PLATFORM_WINDOWS && IS_MONOLITHIC
		bEngineCefInSameBinary |= bEngineCefLinked;
	#endif
		if (bEngineCefInSameBinary)
		{
			UE_LOG(LogBlu, Error, TEXT(" STATUS: BLUI disabled: the engine Web Browser (CEF3) is linked into this packaged build and the two CEF runtimes collide. Disable the Web Browser plugin (WebBrowserWidget) to use BLUI in packaged games."));
			return;
		}

		CefString GameDirCef = *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() + "BluCache");
		FString ExecutablePath = FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin("BLUI")->GetBaseDir() + "/ThirdParty/cef/");

		// Setup the default settings for BluManager
		BluManager::Settings.windowless_rendering_enabled = true;
		BluManager::Settings.no_sandbox = true;
		BluManager::Settings.remote_debugging_port = 7777;
		BluManager::Settings.uncaught_exception_stack_size = 5;

	#if PLATFORM_LINUX
		ExecutablePath = "./blu_ue4_process";
	#endif
	#if PLATFORM_MAC
		ExecutablePath += "Mac/shipping/blu_ue4_process.app/Contents/MacOS/blu_ue4_process";
	#endif
	#if PLATFORM_WINDOWS
		ExecutablePath += "Win/shipping/BluBrowserProcess.exe";
	#endif

		CefString realExePath = *ExecutablePath;

		// Set the sub-process path
		CefString(&BluManager::Settings.browser_subprocess_path).FromString(realExePath);

		// Set the cache path
		CefString(&BluManager::Settings.cache_path).FromString(GameDirCef);

		// Make a new manager instance
		CefRefPtr<BluManager> BluApp = new BluManager();

		//CefExecuteProcess(BluManager::main_args, BluApp, NULL);
		if (!CefInitialize(BluManager::MainArgs, BluManager::Settings, BluApp, NULL))
		{
			UE_LOG(LogBlu, Error, TEXT(" STATUS: CefInitialize failed (exit code %d), BLUI browsers will not be created"), CefGetExitCode());
			return;
		}
		BluManager::bCefInitialized = true;

		UE_LOG(LogBlu, Log, TEXT(" STATUS: Loaded CEF %d.%d.%d (chromium %d.%d.%d.%d)"),
			cef_version_info(0), cef_version_info(1), cef_version_info(2),
			cef_version_info(4), cef_version_info(5), cef_version_info(6), cef_version_info(7));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogBlu, Log, TEXT(" STATUS: Shutdown"));
		//CefShutdown();
	}

};


IMPLEMENT_MODULE( FBlu, Blu )
DEFINE_LOG_CATEGORY(LogBlu);