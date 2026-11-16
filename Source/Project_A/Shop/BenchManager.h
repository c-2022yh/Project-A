#pragma once

#include "Components/ActorComponent.h"
#include "ShopTypes.h"
#include "BenchManager.generated.h"

UCLASS(ClassGroup=(Shop))
class PROJECT_A_API UBenchManager : public UActorComponent
{
    GENERATED_BODY()

public:
    UBenchManager();
    static constexpr int32 SlotCount = 7;

    UFUNCTION(BlueprintPure, Category="Bench")
    int32 GetSlotCount() const { return SlotCount; }

    UFUNCTION(BlueprintPure, Category="Bench")
    FGuid GetOccupant(int32 Slot) const;

    UFUNCTION(BlueprintPure, Category="Bench")
    FUnitPosition FindFreeSlot() const;

    FUnitPosition FindPosition(FGuid UnitId) const;

    // Only existing bench occupants can move or swap here.
    UFUNCTION(BlueprintCallable, Category="Bench")
    ETransactionResult RequestMove(FGuid UnitId, int32 Slot);

    // Native integration entry point for a validated actor batch.
    // Bench occupancy is committed only after the actor/board adapter accepts the batch.
    bool ApplyChanges(const TArray<FRosterUnitChange>& Changes, const FShopMatchState& Match);

    bool IsApplying() const { return bApplying; }

    static bool IsValidSlot(int32 Slot);
    static FUnitPosition SlotPosition(int32 Slot);

private:
    TArray<FGuid> Slots;
    bool bApplying = false;
};
