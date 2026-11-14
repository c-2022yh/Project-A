#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShopTypes.h"
#include "ShopOdds.generated.h"

UCLASS(BlueprintType)
class PROJECT_A_API UShopOdds : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Shop")
    TMap<int32, FShopRoundOdds> Rounds;
};
