#include "ItemManager.h"

void AItemManager::PlanReturn(TArray<FName>& Proposed, const TArray<FName>& Equipment)
{
    Proposed.Append(Equipment);
}

void AItemManager::LimitInheritedEquipment(TArray<FName>& Equipment, TArray<FName>& Returned)
{
    while (Equipment.Num() > EquipmentSlots)
    {
        const int32 Index = FMath::RandHelper(Equipment.Num());
        Returned.Add(Equipment[Index]);
        Equipment.RemoveAtSwap(Index);
    }
}
bool AItemManager::GetItem(FName Id, FItemDefinition& Item) const
{
    if (!ItemDefinitions || Id.IsNone() || ItemDefinitions->GetRowStruct() != FItemDefinition::StaticStruct()) return false;
    const FItemDefinition* Row = ItemDefinitions->FindRow<FItemDefinition>(Id, TEXT("ItemManager"), false);
    if (!Row) return false;
    Item = *Row;
    return true;
}

bool AItemManager::GrantItem(FName Id)
{
    FItemDefinition Item;
    if (!GetItem(Id, Item)) return false;
    Inventory.Add(Id);
    return true;
}

void AItemManager::TryGrantKillDrop()
{
    if (!ItemDefinitions || FMath::FRand() >= KillItemChance) return;
    TArray<FName> Candidates;
    for (FName Id : ItemDefinitions->GetRowNames())
    {
        FItemDefinition Item;
        if (GetItem(Id, Item) && Item.bCanDropOnKill) Candidates.Add(Id);
    }
    if (!Candidates.IsEmpty()) GrantItem(Candidates[FMath::RandHelper(Candidates.Num())]);
}

ETransactionResult AItemManager::PlanEquipmentChange(TArray<FName>& Proposed, TArray<FName>& Equipment, FName ItemId,
    TOptional<int32> UnequipSlot) const
{
    if (UnequipSlot.IsSet())
    {
        const int32 Slot = UnequipSlot.GetValue();
        if (!Equipment.IsValidIndex(Slot)) return ETransactionResult::InvalidSelection;
        Proposed.Add(Equipment[Slot]);
        Equipment.RemoveAt(Slot);
    }
    else
    {
        FItemDefinition Definition;
        const int32 Index = Proposed.IndexOfByKey(ItemId);
        if (Index == INDEX_NONE || !GetItem(ItemId, Definition)) return ETransactionResult::InvalidSelection;
        if (Equipment.Num() >= EquipmentSlots) return ETransactionResult::NoSpace;
        Equipment.Add(ItemId);
        Proposed.RemoveAt(Index);
    }

    return ETransactionResult::Success;
}
