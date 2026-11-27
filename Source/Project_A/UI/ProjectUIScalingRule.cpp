#include "ProjectUIScalingRule.h"

#include "Engine/UserInterfaceSettings.h"

float UProjectUIScalingRule::GetDPIScaleBasedOnSize(FIntPoint Size) const
{
    const auto* Settings = GetDefault<UUserInterfaceSettings>();
    const FIntPoint DesignSize = Settings->DesignScreenSize;
    if (Size.X <= 0 || Size.Y <= 0 || DesignSize.X <= 0 || DesignSize.Y <= 0) return 1.f;

    // Evaluate the height of the largest design-aspect rectangle that fits inside the viewport.
    const float FitHeight = FMath::Min(float(Size.Y), float(Size.X) * DesignSize.Y / DesignSize.X);
    return Settings->UIScaleCurve.GetRichCurveConst()->Eval(FitHeight, FitHeight / DesignSize.Y);
}
