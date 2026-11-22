#include "SBluBrowser.h"
#include "BluEye.h"
#include "BluLayout.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"

void SBluBrowser::Construct(const FArguments& InArgs)
{
	BluEye = InArgs._BluEye;
	DesiredSize = InArgs._DesiredSize;
	Brush.DrawAs = ESlateBrushDrawType::Image;
	SetCanTick(true);
	// The texture object changes on resize; cached draw elements would keep the old one
	ForceVolatile(true);
}

void SBluBrowser::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UBluEye* Eye = BluEye.Get();
	if (!Eye)
	{
		return;
	}

	const float Scale = AllottedGeometry.GetAccumulatedLayoutTransform().GetScale();
	BluLayout::FSizes Sizes;
	if (!BluLayout::ComputeSizes(AllottedGeometry.GetLocalSize(), Scale, Sizes))
	{
		return;
	}

	if (Sizes.ViewSize != LastViewSize || Scale != LastScale)
	{
		LastViewSize = Sizes.ViewSize;
		LastScale = Scale;
		Eye->ResizeBrowserScaled(Sizes.ViewSize.X, Sizes.ViewSize.Y, Scale);
	}
	Eye->RetryPendingResize();
}

int32 SBluBrowser::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UBluEye* Eye = BluEye.Get();
	UTexture2D* Texture = Eye ? Eye->GetTextureIfValid() : nullptr;
	if (!Texture)
	{
		return LayerId;
	}

	// Re-read every paint: a resize replaces the texture object
	Brush.SetResourceObject(Texture);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &Brush, ESlateDrawEffect::None, InWidgetStyle.GetColorAndOpacityTint());
	return LayerId;
}

FVector2D SBluBrowser::ComputeDesiredSize(float) const
{
	return DesiredSize;
}

UBluEye* SBluBrowser::GetLiveEye() const
{
	UBluEye* Eye = BluEye.Get();
	return (Eye && Eye->IsBrowserValid()) ? Eye : nullptr;
}

FReply SBluBrowser::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UBluEye* Eye = GetLiveEye();
	if (!Eye)
	{
		return FReply::Unhandled();
	}

	// Local Slate unit == browser CSS pixel (D3), so no extra mapping
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	Eye->TriggerMouseMove(Local, 1.f);
	return FReply::Handled();
}

FReply SBluBrowser::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UBluEye* Eye = GetLiveEye();
	if (!Eye)
	{
		return FReply::Unhandled();
	}

	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button == EKeys::LeftMouseButton)
	{
		Eye->TriggerLeftMouseDown(Local, 1.f);
	}
	else if (Button == EKeys::RightMouseButton)
	{
		Eye->TriggerRightMouseDown(Local, 1.f);
	}
	else
	{
		return FReply::Unhandled();
	}

	// Capture keeps drag-select working outside the widget; focus routes the keyboard here
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
}

FReply SBluBrowser::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Slate sends double-click instead of the second down when the widget is not captured
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SBluBrowser::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UBluEye* Eye = GetLiveEye();
	if (!Eye)
	{
		return FReply::Unhandled();
	}

	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button == EKeys::LeftMouseButton)
	{
		Eye->TriggerLeftMouseUp(Local, 1.f);
	}
	else if (Button == EKeys::RightMouseButton)
	{
		Eye->TriggerRightMouseUp(Local, 1.f);
	}
	else
	{
		return FReply::Unhandled();
	}

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SBluBrowser::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UBluEye* Eye = GetLiveEye();
	if (!Eye)
	{
		return FReply::Unhandled();
	}

	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	Eye->TriggerMouseWheel(MouseEvent.GetWheelDelta(), Local, 1.f);
	return FReply::Handled();
}

FReply SBluBrowser::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	UBluEye* Eye = GetLiveEye();
	if (!Eye)
	{
		return FReply::Unhandled();
	}

	Eye->KeyDown(InKeyEvent);
	return FReply::Handled();
}

FReply SBluBrowser::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	UBluEye* Eye = GetLiveEye();
	if (!Eye)
	{
		return FReply::Unhandled();
	}

	Eye->KeyUp(InKeyEvent);
	return FReply::Handled();
}

FReply SBluBrowser::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	UBluEye* Eye = GetLiveEye();
	if (!Eye)
	{
		return FReply::Unhandled();
	}

	Eye->CharKeyInput(InCharacterEvent);
	return FReply::Handled();
}
