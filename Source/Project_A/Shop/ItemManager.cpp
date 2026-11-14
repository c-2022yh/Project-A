#include "ItemManager.h"

void AItemManager::FState::ReturnEquipment(FGuid UnitId)
{
    TArray<FName> Equipped;
    if (Equipment.RemoveAndCopyValue(UnitId, Equipped)) Inventory.Append(Equipped);
}

void AItemManager::FState::InheritEquipment(const TArray<FGuid>& Materials, FGuid Kept)
{
    TArray<FName> Inherited;
    for (FGuid MaterialId : Materials)
    {
        TArray<FName> Equipped;
        if (Equipment.RemoveAndCopyValue(MaterialId, Equipped)) Inherited.Append(Equipped);
    }
    while (Inherited.Num() > EquipmentSlots)
    {
        const int32 Index = FMath::RandHelper(Inherited.Num());
        Inventory.Add(Inherited[Index]);
        Inherited.RemoveAtSwap(Index);
    }
    if (!Inherited.IsEmpty()) Equipment.Add(Kept, MoveTemp(Inherited));
}

TArray<FName> AItemManager::GetEquipment(FGuid UnitId) const
{
    const TArray<FName>* Equipped = State.Equipment.Find(UnitId);
    return Equipped ? *Equipped : TArray<FName>();
}

bool AItemManager::GetItem(FName Id, FItemDefinition& Item) const
{
    if (!ItemDefinitions || Id.IsNone() || ItemDefinitions->GetRowStruct() != FItemDefinition::StaticStruct()) return false;
    const FItemDefinition* Row = ItemDefinitions->FindRow<FItemDefinition>(Id, TEXT("ItemManager"), false);
    if (!Row) return false;
    Item = *Row;
    return true;
}

FItemBonuses AItemManager::GetBonuses(const FState& Proposed, FGuid UnitId) const
{
    FItemBonuses Bonuses;
    if (const TArray<FName>* Equipped = Proposed.Equipment.Find(UnitId))
    {
        for (FName ItemId : *Equipped)
        {
            FItemDefinition Item;
            if (GetItem(ItemId, Item)) Bonuses.Add(Item.Bonuses);
        }
    }
    return Bonuses;
}

bool AItemManager::GrantItem(FName Id)
{
    FItemDefinition Item;
    if (!GetItem(Id, Item)) return false;
    State.Inventory.Add(Id);
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

ETransactionResult AItemManager::PlanEquipmentChange(FState& Proposed, FGuid UnitId, FName ItemId,
    TOptional<int32> UnequipSlot) const
{
    TArray<FName>& Equipped = Proposed.Equipment.FindOrAdd(UnitId);
    if (UnequipSlot.IsSet())
    {
        const int32 Slot = UnequipSlot.GetValue();
        if (!Equipped.IsValidIndex(Slot)) return ETransactionResult::InvalidSelection;
        Proposed.Inventory.Add(Equipped[Slot]);
        Equipped.RemoveAt(Slot);
        if (Equipped.IsEmpty()) Proposed.Equipment.Remove(UnitId);
    }
    else
    {
        FItemDefinition Definition;
        const int32 Index = Proposed.Inventory.IndexOfByKey(ItemId);
        if (Index == INDEX_NONE || !GetItem(ItemId, Definition)) return ETransactionResult::InvalidSelection;
        if (Equipped.Num() >= EquipmentSlots) return ETransactionResult::NoSpace;
        Equipped.Add(ItemId);
        Proposed.Inventory.RemoveAt(Index);
    }

    return ETransactionResult::Success;
}
