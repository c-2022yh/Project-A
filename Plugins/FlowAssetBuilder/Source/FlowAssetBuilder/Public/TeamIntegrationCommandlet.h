#pragma once
#include "Commandlets/Commandlet.h"
#include "TeamIntegrationCommandlet.generated.h"

UCLASS()
class UTeamIntegrationCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UTeamIntegrationCommandlet();
    virtual int32 Main(const FString& Params) override;
};
