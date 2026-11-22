#include "BluBrowser.h"
#include "BluEye.h"
#include "SBluBrowser.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BluBrowser"

UBluEye* UBluBrowser::GetBluEye() const
{
	return BluEye;
}

TSharedRef<SWidget> UBluBrowser::RebuildWidget()
{
	// Placeholder in the designer and in the editor outside PIE: never spawn CEF there
	if (IsDesignTime() || (GEngine && GEngine->IsEditor() && !(GetWorld() && GetWorld()->IsPlayInEditor())))
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BluBrowserPlaceholder", "BLUI Browser"))
			];
	}

	// Reuse the eye across rebuilds so the page and its state survive
	if (!BluEye)
	{
		BluEye = NewObject<UBluEye>(this);
		BluEye->Settings = Settings;
		BluEye->DefaultURL = InitialURL;
		BluEye->bEnabled = true;
		BluEye->Init();
	}

	SlateBrowser = SNew(SBluBrowser)
		.BluEye(BluEye)
		.DesiredSize(Settings.ViewSize);
	return SlateBrowser.ToSharedRef();
}

void UBluBrowser::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	// The browser closes when the GC destroys BluEye, not here
	SlateBrowser.Reset();
}

#if WITH_EDITOR
const FText UBluBrowser::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "BLUI");
}
#endif

#undef LOCTEXT_NAMESPACE
