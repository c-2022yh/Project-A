#pragma once
#include "Commandlets/Commandlet.h"
#include "FlowBuildCommandlet.generated.h"
UCLASS()
class UFlowBuildCommandlet : public UCommandlet {
 GENERATED_BODY()
public:
 UFlowBuildCommandlet();
 virtual int32 Main(const FString& Params) override;
};
