#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/UserInterfaceSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectUIScalingTest, "ProjectA.UI.ViewportScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FProjectUIScalingTest::RunTest(const FString& Parameters)
{
    const auto& Settings = *GetDefault<UUserInterfaceSettings>();
    TestEqual(TEXT("1920 x 1080 uses the design scale"), Settings.GetDPIScaleBasedOnSize(FIntPoint(1920, 1080)), 1.f);
    TestEqual(TEXT("Half-size viewport scales both axes equally"), Settings.GetDPIScaleBasedOnSize(FIntPoint(960, 540)), 0.5f);
    TestEqual(TEXT("Narrow viewport fits the width"), Settings.GetDPIScaleBasedOnSize(FIntPoint(960, 1080)), 0.5f);
    TestEqual(TEXT("Ultrawide viewport fits the height"), Settings.GetDPIScaleBasedOnSize(FIntPoint(2560, 1080)), 1.f);
    TestEqual(TEXT("4K scales above the design size"), Settings.GetDPIScaleBasedOnSize(FIntPoint(3840, 2160)), 2.f);
    return true;
}

#endif
