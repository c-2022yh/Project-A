#pragma once

#include "Engine/DPICustomScalingRule.h"
#include "ProjectUIScalingRule.generated.h"

UCLASS()
class UProjectUIScalingRule final : public UDPICustomScalingRule
{
    GENERATED_BODY()

public:
    virtual float GetDPIScaleBasedOnSize(FIntPoint Size) const override;
};
