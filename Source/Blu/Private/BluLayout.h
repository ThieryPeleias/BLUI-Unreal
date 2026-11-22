#pragma once

#include "CoreMinimal.h"

namespace BluLayout
{
	struct FSizes
	{
		FIntPoint ViewSize;
		FIntPoint TextureSize;
	};

	// false when LocalSize rounds to < 1 on either axis, or Scale <= 0
	inline bool ComputeSizes(const FVector2D& LocalSize, float Scale, FSizes& Out)
	{
		const FIntPoint View(FMath::RoundToInt32(LocalSize.X), FMath::RoundToInt32(LocalSize.Y));
		if (View.X < 1 || View.Y < 1 || Scale <= 0.f)
		{
			return false;
		}
		Out.ViewSize = View;
		// ceil matches Chromium's rounding of the scaled size
		Out.TextureSize = FIntPoint(
			FMath::Max(1, FMath::CeilToInt(View.X * Scale)),
			FMath::Max(1, FMath::CeilToInt(View.Y * Scale)));
		return true;
	}
}
