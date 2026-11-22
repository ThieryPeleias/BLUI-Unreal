#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "BluTypes.h"
#include "BluBrowser.generated.h"

class UBluEye;
class SBluBrowser;

/** UMG widget that shows a BLUI browser and follows the layout size of its slot. */
UCLASS(ClassGroup = Blu, meta = (DisplayName = "BLUI Browser"))
class BLU_API UBluBrowser : public UWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blu")
	FString InitialURL;

	// ViewSize here is only the desired size; the real size follows the layout
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blu")
	FBluEyeSettings Settings;

	UFUNCTION(BlueprintPure, Category = "Blu")
	UBluEye* GetBluEye() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UBluEye> BluEye;

	TSharedPtr<SBluBrowser> SlateBrowser;
};
