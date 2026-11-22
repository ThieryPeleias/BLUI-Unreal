#include "BluLayout.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBluLayoutComputeSizesTest, "Blu.Layout.ComputeSizes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBluLayoutComputeSizesTest::RunTest(const FString& Parameters)
{
	BluLayout::FSizes S;
	TestTrue (TEXT("800x600@1"),    BluLayout::ComputeSizes(FVector2D(800, 600), 1.f, S));
	TestEqual(TEXT("view"),         S.ViewSize,    FIntPoint(800, 600));
	TestEqual(TEXT("tex"),          S.TextureSize, FIntPoint(800, 600));
	TestTrue (TEXT("800x600@1.5"),  BluLayout::ComputeSizes(FVector2D(800, 600), 1.5f, S));
	TestEqual(TEXT("view dpi"),     S.ViewSize,    FIntPoint(800, 600));
	TestEqual(TEXT("tex dpi"),      S.TextureSize, FIntPoint(1200, 900));
	TestTrue (TEXT("801x601@1.25"), BluLayout::ComputeSizes(FVector2D(801, 601), 1.25f, S));
	TestEqual(TEXT("tex ceil"),     S.TextureSize, FIntPoint(1002, 752));
	TestTrue (TEXT("rounding"),     BluLayout::ComputeSizes(FVector2D(799.6, 600.4), 1.f, S));
	TestEqual(TEXT("rounded"),      S.ViewSize,    FIntPoint(800, 600));
	TestFalse(TEXT("zero width"),   BluLayout::ComputeSizes(FVector2D(0, 600), 1.f, S));
	TestFalse(TEXT("sub-pixel"),    BluLayout::ComputeSizes(FVector2D(0.4, 600), 1.f, S));
	TestFalse(TEXT("zero height"),   BluLayout::ComputeSizes(FVector2D(800, 0), 1.f, S));
	TestFalse(TEXT("sub-pixel height"), BluLayout::ComputeSizes(FVector2D(800, 0.4), 1.f, S));
	TestFalse(TEXT("zero scale"),  BluLayout::ComputeSizes(FVector2D(800, 600), 0.f, S));
	TestTrue (TEXT("tiny scale"),   BluLayout::ComputeSizes(FVector2D(1, 1), 0.1f, S));
	TestEqual(TEXT("min 1px"),      S.TextureSize, FIntPoint(1, 1));
	return true;
}

#endif
