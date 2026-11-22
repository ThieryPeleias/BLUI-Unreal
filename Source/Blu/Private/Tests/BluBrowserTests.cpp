#include "BluBrowser.h"
#include "BluEye.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "BluLayout.h"
#include "BluTestLogListener.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h"

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

// A click on a scaled, offset widget must reach the page at the CSS pixel under the cursor
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBluBrowserClickTest, "Blu.Browser.ClickLandsUnderCursor", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBluBrowserClickTest::RunTest(const FString& Parameters)
{
	if (GEngine && GEngine->IsEditor())
	{
		AddInfo(TEXT("Skipped: run with -game"));
		return true;
	}

	struct FClickState
	{
		TStrongObjectPtr<UBluBrowser> Widget;
		TStrongObjectPtr<UBluTestLogListener> Listener;
		TSharedPtr<SWidget> Slate;
		double Deadline = 0.0;
		bool bClicked = false;
	};
	TSharedRef<FClickState> State = MakeShared<FClickState>();
	State->Widget.Reset(NewObject<UBluBrowser>(GetTransientPackage()));
	State->Widget->InitialURL = TEXT("data:text/html,<body style='margin:0;height:100vh' onmousedown=\"document.title='down '+event.clientX+' '+event.clientY\">x</body>");
	State->Slate = State->Widget->TakeWidget();
	UBluEye* Eye = State->Widget->GetBluEye();
	if (!TestNotNull(TEXT("BluEye created"), Eye) || !TestTrue(TEXT("CEF browser created"), Eye->IsBrowserValid()))
	{
		return false;
	}
	State->Listener.Reset(NewObject<UBluTestLogListener>());
	Eye->LogEventEmitter.AddDynamic(State->Listener.Get(), &UBluTestLogListener::OnLog);

	// 400x300 widget drawn at (300, 200) with layout scale 1.5: local (100, 50) is absolute (450, 275)
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(400, 300), FSlateLayoutTransform(1.5f, FVector2D(300, 200)));
	const FVector2D ScreenPos(450, 275);
	const FString Expected = TEXT("down 100 50");

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State, Geometry, ScreenPos, Expected]()
	{
		const double Now = FPlatformTime::Seconds();
		if (State->Deadline == 0.0)
		{
			State->Deadline = Now + 20.0;
		}
		State->Slate->Tick(Geometry, Now, 0.016f);

		UBluEye* Eye = State->Widget->GetBluEye();
		const bool bReady = Eye->GetTextureIfValid() && Eye->GetTextureIfValid()->GetSizeX() == 600 && !Eye->IsBrowserLoading();
		if (bReady && !State->bClicked)
		{
			const TSet<FKey> Pressed = { EKeys::LeftMouseButton };
			State->Slate->OnMouseButtonDown(Geometry, FPointerEvent(0, ScreenPos, ScreenPos, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
			State->Slate->OnMouseButtonUp(Geometry, FPointerEvent(0, ScreenPos, ScreenPos, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
			State->bClicked = true;
		}

		const bool bFound = State->Listener->Logs.Contains(Expected);
		if (!bFound && Now < State->Deadline)
		{
			return false;
		}
		if (bFound)
		{
			UE_LOG(LogTemp, Display, TEXT("Blu.Browser: page saw '%s'"), *Expected);
		}
		else
		{
			AddError(FString::Printf(TEXT("page never reported '%s' (clicked=%d, logs: %s)"), *Expected, State->bClicked, *FString::Join(State->Listener->Logs, TEXT(" | "))));
		}
		State->Slate.Reset();
		State->Widget.Reset();
		State->Listener.Reset();
		return true;
	}));
	return true;
}

// Real UMG canvas in the game viewport: stretch anchors fill the viewport, centered auto-size uses Settings.ViewSize
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBluBrowserUmgLayoutTest, "Blu.Browser.UmgCanvasLayout", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBluBrowserUmgLayoutTest::RunTest(const FString& Parameters)
{
	if ((GEngine && GEngine->IsEditor()) || !GEngine || !GEngine->GameViewport)
	{
		AddInfo(TEXT("Skipped: run with -game"));
		return true;
	}

	struct FUmgState
	{
		TStrongObjectPtr<UCanvasPanel> Canvas;
		TStrongObjectPtr<UBluBrowser> Widget;
		TSharedPtr<SWidget> CanvasSlate;
		UCanvasPanelSlot* Slot = nullptr;
		double Deadline = 0.0;
	};
	TSharedRef<FUmgState> State = MakeShared<FUmgState>();
	State->Canvas.Reset(NewObject<UCanvasPanel>(GetTransientPackage()));
	State->Widget.Reset(NewObject<UBluBrowser>(GetTransientPackage()));
	State->Widget->InitialURL = TEXT("data:text/html,<body style='margin:0;background:%2336c'>BLUI</body>");
	State->Widget->Settings.ViewSize = FVector2D(640, 360);
	State->Slot = Cast<UCanvasPanelSlot>(State->Canvas->AddChild(State->Widget.Get()));
	if (!TestNotNull(TEXT("canvas slot"), State->Slot))
	{
		return false;
	}
	State->Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	State->Slot->SetOffsets(FMargin(0.f));
	State->CanvasSlate = State->Canvas->TakeWidget();
	GEngine->GameViewport->AddViewportWidgetContent(State->CanvasSlate.ToSharedRef());

	// Waits until the browser texture matches what its real tick geometry asks for; bCentered also checks the slot is centered
	auto WaitForLayout = [this, State](bool bCentered)
	{
		return [this, State, bCentered]()
		{
			const double Now = FPlatformTime::Seconds();
			if (State->Deadline == 0.0)
			{
				State->Deadline = Now + 20.0;
			}

			TSharedPtr<SWidget> Slate = State->Widget->GetCachedWidget();
			UBluEye* Eye = State->Widget->GetBluEye();
			UTexture2D* Texture = Eye ? Eye->GetTextureIfValid() : nullptr;
			FVector2D ViewportSize;
			GEngine->GameViewport->GetViewportSize(ViewportSize);
			if (!Slate.IsValid() || !Texture || ViewportSize.X <= 0)
			{
				return Now >= State->Deadline ? (AddError(TEXT("widget, texture or viewport never became valid")), true) : false;
			}

			const FGeometry& Geometry = Slate->GetTickSpaceGeometry();
			const float Scale = Geometry.GetAccumulatedLayoutTransform().GetScale();
			const FVector2D Local = Geometry.GetLocalSize();
			const FVector2D AbsSize = Local * Scale;
			const FVector2D AbsCenter = FVector2D(Geometry.GetAbsolutePosition()) + AbsSize * 0.5f;
			BluLayout::FSizes Sizes;
			const bool bSizes = BluLayout::ComputeSizes(Local, Scale, Sizes);

			const bool bLayout = bCentered
				? Local.Equals(FVector2D(640, 360), 1.f) && AbsCenter.Equals(ViewportSize * 0.5f, 2.f)
				: AbsSize.Equals(ViewportSize, 2.f);
			const bool bTexture = bSizes && FMath::Abs(int32(Texture->GetSizeX()) - Sizes.TextureSize.X) <= 1 && FMath::Abs(int32(Texture->GetSizeY()) - Sizes.TextureSize.Y) <= 1;
			if (!(bLayout && bTexture) && Now < State->Deadline)
			{
				return false;
			}

			const FString Message = FString::Printf(TEXT("%s: viewport %s, widget local %s scale %.3f abs center %s, texture %dx%d (expected %s)"),
				bCentered ? TEXT("centered") : TEXT("stretched"), *ViewportSize.ToString(), *Local.ToString(), Scale, *AbsCenter.ToString(),
				Texture->GetSizeX(), Texture->GetSizeY(), *Sizes.TextureSize.ToString());
			if (bLayout && bTexture)
			{
				UE_LOG(LogTemp, Display, TEXT("Blu.Browser: %s"), *Message);
			}
			else
			{
				AddError(Message);
			}
			State->Deadline = 0.0;
			return true;
		};
	};

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(WaitForLayout(false)));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
	{
		State->Slot->SetAnchors(FAnchors(0.5f));
		State->Slot->SetAlignment(FVector2D(0.5f, 0.5f));
		State->Slot->SetAutoSize(true);
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(WaitForLayout(true)));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([State]()
	{
		if (GEngine && GEngine->GameViewport && State->CanvasSlate.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(State->CanvasSlate.ToSharedRef());
		}
		State->CanvasSlate.Reset();
		State->Widget.Reset();
		State->Canvas.Reset();
		return true;
	}));
	return true;
}

#endif
