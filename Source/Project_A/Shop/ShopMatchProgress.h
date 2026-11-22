#pragma once

#include "ShopTypes.h"
#include "UObject/Interface.h"
#include "ShopMatchProgress.generated.h"

// GameMode provides the current phase, round and deployment limit without side effects.
// After switching to Preparation call EnterPreparation and handle a failed callback.
// During RewardSelection keep the round unchanged until OnRewardReceived, then
// advance the round and enter Preparation. Settlement applies before rewards.
// Use fresh shop, roster and item instances when starting a new match.
UINTERFACE(BlueprintType, Blueprintable)
class PROJECT_A_API UShopMatchProgress : public UInterface
{
    GENERATED_BODY()
};

class PROJECT_A_API IShopMatchProgress
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Progression")
    FShopMatchState GetShopMatchState() const;
};

// Read the current GameMode. The legacy GameMode fields cover preparation,
// combat, settlement and finish. Reward selection needs the GameMode contract.
PROJECT_A_API FShopMatchState ReadShopMatchState(const UWorld* World);
