#pragma once

#include "Components/ActorComponent.h"
#include "ShopTypes.h"
#include "BenchManager.generated.h"

UCLASS(ClassGroup=(Shop))
class PROJECT_A_API UBenchManager : public UActorComponent
{
    GENERATED_BODY()

public:
    static constexpr int32 SlotCount = 7;

    UFUNCTION(BlueprintPure, Category="Bench")
    int32 GetSlotCount() const { return SlotCount; }

    UFUNCTION(BlueprintPure, Category="Bench")
    FGuid GetOccupant(int32 Slot) const;

    UFUNCTION(BlueprintPure, Category="Bench")
    FUnitPosition FindFreeSlot() const;

    static bool IsValidSlot(int32 Slot);

    static FUnitPosition SlotPosition(int32 Slot);
};
