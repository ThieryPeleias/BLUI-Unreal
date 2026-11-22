#include "BluBrowser.h"
#include "BluEye.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FBluBrowserTestState
	{
		TStrongObjectPtr<UBluBrowser> Widget;
		TSharedPtr<SWidget> Slate;
		double Deadline = 0.0;
	};

	struct FBluBrowserTestStep
	{
		FVector2D LocalSize;
		float Scale;
		FIntPoint ExpectedTexture;
		int32 Tolerance;
	};

	FIntPoint GetTextureSize(UBluBrowser* Widget)
	{
		UBluEye* Eye = Widget ? Widget->GetBluEye() : nullptr;
		UTexture2D* Texture = Eye ? Eye->GetTextureIfValid() : nullptr;
		return Texture ? FIntPoint(Texture->GetSizeX(), Texture->GetSizeY()) : FIntPoint::ZeroValue;
	}
}

// Drives the real widget + CEF: layout size and DPI must reach the browser and come back as a texture of the right size
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBluBrowserResizeTest, "Blu.Browser.ResizeFollowsLayout", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBluBrowserResizeTest::RunTest(const FString& Parameters)
{
	// UBluEye::Init refuses in the editor outside PIE, so this only runs with -game
	if (GEngine && GEngine->IsEditor())
	{
		AddInfo(TEXT("Skipped: run with -game"));
		return true;
	}

	TSharedRef<FBluBrowserTestState> State = MakeShared<FBluBrowserTestState>();
	State->Widget.Reset(NewObject<UBluBrowser>(GetTransientPackage()));
	State->Widget->InitialURL = TEXT("data:text/html,<body style='margin:0;background:%23c33'>BLUI</body>");
	State->Slate = State->Widget->TakeWidget();

	UBluEye* Eye = State->Widget->GetBluEye();
	if (!TestNotNull(TEXT("BluEye created"), Eye) || !TestTrue(TEXT("CEF browser created"), Eye->IsBrowserValid()))
	{
		return false;
	}

	const TArray<FBluBrowserTestStep> Steps = {
		{ FVector2D(640, 360), 1.5f, FIntPoint(960, 540), 0 },
		{ FVector2D(801, 601), 1.25f, FIntPoint(1002, 752), 1 },
		{ FVector2D(800, 450), 1.f, FIntPoint(800, 450), 0 },
	};

	for (const FBluBrowserTestStep& Step : Steps)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, Step]()
		{
			const double Now = FPlatformTime::Seconds();
			if (State->Deadline == 0.0)
			{
				State->Deadline = Now + 20.0;
			}

			State->Slate->Tick(FGeometry::MakeRoot(Step.LocalSize, FSlateLayoutTransform(Step.Scale)), Now, 0.016f);

			const FIntPoint Size = GetTextureSize(State->Widget.Get());
			const bool bMatch = FMath::Abs(Size.X - Step.ExpectedTexture.X) <= Step.Tolerance
				&& FMath::Abs(Size.Y - Step.ExpectedTexture.Y) <= Step.Tolerance;
			if (!bMatch && Now < State->Deadline)
			{
				return false;
			}

			const FString Message = FString::Printf(TEXT("texture %s for layout %s at scale %.2f (expected %s +-%d)"),
				*Size.ToString(), *Step.LocalSize.ToString(), Step.Scale, *Step.ExpectedTexture.ToString(), Step.Tolerance);
			if (bMatch)
			{
				UE_LOG(LogTemp, Display, TEXT("Blu.Browser: %s"), *Message);
			}
			else
			{
				AddError(Message);
			}
			State->Deadline = 0.0;
			return true;
		}));
	}

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
	{
		State->Slate.Reset();
		State->Widget.Reset();
		return true;
	}));
	return true;
}

#endif
