#include "MainMapShopPlacement.h"
#include "../AutoBattleGameMode.h"
#include "../AutoBattlePlayerController.h"
#include "../BattleUnitBase.h"
#include "../BenchTile.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "BenchManager.h"
#include "ItemManager.h"
#include "RosterManager.h"
#include "Engine/World.h"
#include "../UI/UnitStatusWidget.h"
#include "Components/WidgetComponent.h"

namespace
{
    const FName ShopEquipmentId(TEXT("ShopEquipment"));
}

bool UMainMapShopPlacement::Initialize(AAutoBattleGameMode& InMode, ARosterManager& Roster, AItemManager& InItems)
{
    if (!Roster.Units.IsEmpty() || !IsValid(InMode.BoardManager)) return false;
    Mode = &InMode;
    Items = &InItems;
    UnitDefinitions = InMode.MainMapUnitDefinitions.LoadSynchronous();
    UnitClass = InMode.MainMapUnitClass.LoadSynchronous();
    if (!UnitDefinitions || UnitDefinitions->GetRowStruct() != FUnitDefinition::StaticStruct()
        || !UnitClass || UnitClass->HasAnyClassFlags(CLASS_Abstract)) return false;
    TArray<FRosterUnit> StartingRoster;
    for (ABattleUnitBase* Unit : Mode->BattleUnits)
    {
        if (!IsValid(Unit) || Unit->TeamType != EBattleTeam::Player) continue;
        const FUnitDefinition* Definition = Unit->UnitData.GetRow<FUnitDefinition>(TEXT("MainMap shop roster"));
        if (!Definition) return false;
        FRosterUnit Entry{FGuid::NewGuid(), Unit->UnitID, Unit->StarLevel, Definition->Cost};
        if (!Unit->CaptureSkillProgress(Entry.SkillProgress)) return false;
        Units.Add(Entry.Id, Unit);
        Unit->EquipmentQuery.BindUObject(this, &UMainMapShopPlacement::GetActorEquipment, Entry.Id);
        if (GetUnitPosition_Implementation(Entry.Id).Location == EUnitLocation::None) return false;
        StartingRoster.Add(MoveTemp(Entry));
    }
    Roster.Placement.SetObject(this);
    Roster.Placement.SetInterface(this);
    Roster.bUseLiveBench = true;
    Roster.Units = MoveTemp(StartingRoster);
    return true;
}

FGuid UMainMapShopPlacement::FindUnitId(const ABattleUnitBase* Actor) const
{
    if (IsValid(Actor))
        for (const auto& Entry : Units)
            if (Entry.Value == Actor) return Entry.Key;
    return {};
}

ABattleUnitBase* UMainMapShopPlacement::GetBattleUnit_Implementation(FGuid Id) const
{
    ABattleUnitBase* Unit = Units.FindRef(Id);
    return IsValid(Unit) ? Unit : nullptr;
}

FUnitPosition UMainMapShopPlacement::GetUnitPosition_Implementation(FGuid Id) const
{
    const ABattleUnitBase* Unit = GetBattleUnit_Implementation(Id);
    if (!Unit || !IsValid(Mode) || !IsValid(Mode->BoardManager)) return {};
    if (const ABoardTile* Tile = Unit->CurrentTile; IsValid(Tile) && Tile->OccupiedUnit == Unit)
        return {EUnitLocation::Board, Tile->Row, Tile->Column};
    for (const ABenchTile* Slot : Mode->BoardManager->BenchSlots)
        if (IsValid(Slot) && Slot->OccupiedUnit == Unit)
            return {EUnitLocation::Bench, INDEX_NONE, Slot->SlotIndex};
    return {};
}

FGuid UMainMapShopPlacement::GetOccupant_Implementation(FUnitPosition Position) const
{
    if (!IsValid(Mode) || !IsValid(Mode->BoardManager)) return {};
    if (Position.Location == EUnitLocation::Bench)
    {
        const ABenchTile* Slot = Mode->BoardManager->GetBenchSlot(Position.Column);
        return IsValid(Slot) ? FindUnitId(Cast<ABattleUnitBase>(Slot->OccupiedUnit)) : FGuid();
    }
    if (Position.Location == EUnitLocation::Board)
    {
        const ABoardTile* Tile = Mode->BoardManager->GetTile(Position.Row, Position.Column);
        return IsValid(Tile) ? FindUnitId(Cast<ABattleUnitBase>(Tile->OccupiedUnit)) : FGuid();
    }
    return {};
}

bool UMainMapShopPlacement::GetUnitEquipment_Implementation(FGuid Id, TArray<FName>& Equipment) const
{
    Equipment.Reset();
    if (!GetBattleUnit_Implementation(Id)) return false;
    Equipment = EquipmentByUnit.FindRef(Id);
    return true;
}

bool UMainMapShopPlacement::GetUnitProfile_Implementation(FGuid Id, FRosterUnitProfile& Profile) const
{
    const ABattleUnitBase* Unit = GetBattleUnit_Implementation(Id);
    if (!Unit) return false;
    const FUnitStats& Stats = Unit->FinalStats;
    Profile = {Stats.MaxHealth, Stats.AttackDamage, Stats.AbilityPower, Stats.Armor,
        Stats.AttackSpeed, static_cast<float>(Stats.AttackRange), Stats.MaxMana};
    return true;
}

FGuid UMainMapShopPlacement::GetHoveredUnit_Implementation() const
{
    const auto* PC = IsValid(Mode) ? Cast<AAutoBattlePlayerController>(Mode->GetWorld()->GetFirstPlayerController()) : nullptr;
    if (!PC) return {};
    // 보상 중에는 전투 컨트롤러의 월드 입력이 잠긴다. 상점은 판매할 기물만 조회한다.
    if (Mode->GetShopMatchState_Implementation().Phase == EShopPhase::RewardSelection)
    {
        FHitResult Hit;
        if (!PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit)) return {};
        AActor* Actor = Hit.GetActor();
        if (const auto* Tile = Cast<ABoardTile>(Actor)) Actor = Tile->OccupiedUnit;
        else if (const auto* Slot = Cast<ABenchTile>(Actor)) Actor = Slot->OccupiedUnit;
        return FindUnitId(Cast<ABattleUnitBase>(Actor));
    }
    if (IsValid(PC->HoveredTile)) return FindUnitId(Cast<ABattleUnitBase>(PC->HoveredTile->OccupiedUnit));
    if (IsValid(PC->HoveredBenchSlot)) return FindUnitId(Cast<ABattleUnitBase>(PC->HoveredBenchSlot->OccupiedUnit));
    return FindUnitId(PC->SelectedUnit);
}

bool UMainMapShopPlacement::ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit)
{
    ABoardManager* Board = IsValid(Mode) ? Mode->BoardManager.Get() : nullptr;
    if (!IsValid(Board) || !Mode->bEncounterReady || Board->bPlacementInProgress
        || (Phase != EShopPhase::Preparation && Phase != EShopPhase::Combat && Phase != EShopPhase::RewardSelection))
        return false;

    struct FEdit
    {
        const FRosterUnitChange* Change = nullptr;
        ABattleUnitBase* Actor = nullptr;
        ABoardTile* OldTile = nullptr;
        ABenchTile* OldBench = nullptr;
        ABoardTile* Tile = nullptr;
        ABenchTile* Bench = nullptr;
        FTransform OldTransform;
        FUnitStatModifier Modifier;
    };
    TArray<FEdit> Edits;
    TSet<AActor*> Vacating;
    TSet<AActor*> Destinations;
    TSet<FGuid> Ids;
    int32 Deployed = Mode->GetDeployedPlayerCount();
    for (const FRosterUnitChange& Change : Changes)
    {
        if (!Change.Unit.Id.IsValid() || Ids.Contains(Change.Unit.Id)) return false;
        Ids.Add(Change.Unit.Id);
        FEdit& Edit = Edits.AddDefaulted_GetRef();
        Edit.Change = &Change;
        Edit.Actor = GetBattleUnit_Implementation(Change.Unit.Id);
        const bool bAdd = Change.Edit == ERosterUnitEdit::Add;
        const bool bRemove = Change.Edit == ERosterUnitEdit::Remove;
        const bool bMove = Change.Edit == ERosterUnitEdit::Move;
        if (bAdd)
        {
            if (Edit.Actor || !UnitDefinitions->FindRow<FUnitDefinition>(Change.Unit.UnitId, TEXT("Shop purchase"), false)) return false;
        }
        else
        {
            if (!IsValid(Edit.Actor) || !Edit.Actor->IsAlive() || Edit.Actor->bInCombat
                || Edit.Actor->UnitID != Change.Unit.UnitId || !Mode->BattleUnits.Contains(Edit.Actor)) return false;
            const FUnitPosition Old = GetUnitPosition_Implementation(Change.Unit.Id);
            if (Old.Location == EUnitLocation::Board) Edit.OldTile = Edit.Actor->CurrentTile;
            else if (Old.Location == EUnitLocation::Bench) Edit.OldBench = Board->GetBenchSlot(Old.Column);
            if ((!Edit.OldTile && !Edit.OldBench) || (Edit.OldTile && Phase == EShopPhase::Combat)) return false;
            Edit.OldTransform = Edit.Actor->GetActorTransform();
            if (bMove || bRemove)
            {
                Vacating.Add(Edit.Actor);
                if (Edit.OldTile) --Deployed;
            }
        }
        if (!bRemove && (Change.Unit.Stars < 1 || Change.Unit.Stars > 2
            || (!bAdd && !Edit.Actor->CanRestoreSkillProgress(Change.Unit.SkillProgress)))) return false;
        if (bAdd || bMove)
        {
            const FUnitPosition& Destination = Change.Destination;
            if (Destination.Location == EUnitLocation::Board)
            {
                Edit.Tile = Board->GetTile(Destination.Row, Destination.Column);
                if (Phase == EShopPhase::Combat || !IsValid(Edit.Tile) || Edit.Tile->TeamType != EBoardTileTeam::Player
                    || Edit.Tile->bBlocksMovement || Edit.Tile->IsReserved() || !Edit.Tile->HasConsistentOccupancy()) return false;
                ++Deployed;
            }
            else if (Destination.Location == EUnitLocation::Bench && UBenchManager::IsValidSlot(Destination.Column))
            {
                Edit.Bench = Board->GetBenchSlot(Destination.Column);
                if (!IsValid(Edit.Bench) || !Edit.Bench->HasConsistentOccupancy()) return false;
            }
            AActor* Slot = Edit.Tile ? static_cast<AActor*>(Edit.Tile) : static_cast<AActor*>(Edit.Bench);
            if (!Slot || Destinations.Contains(Slot)) return false;
            Destinations.Add(Slot);
        }
        if (Change.bReplaceEquipment)
        {
            if (Change.Equipment.Num() > AItemManager::EquipmentSlots) return false;
            for (FName ItemId : Change.Equipment)
            {
                FItemDefinition Item;
                if (!Items->GetItem(ItemId, Item)) return false;
                Edit.Modifier.MaxHealth += Item.Bonuses.HP;
                Edit.Modifier.AttackDamage += Item.Bonuses.AttackDamage;
                Edit.Modifier.AbilityPower += Item.Bonuses.AbilityPower;
                Edit.Modifier.Armor += Item.Bonuses.Armor;
                Edit.Modifier.AttackSpeedBonus += Item.Bonuses.AttackSpeed;
                Edit.Modifier.HealthPercent += Item.Bonuses.HealthPercent;
                Edit.Modifier.AttackDamagePercent += Item.Bonuses.AttackDamagePercent;
                Edit.Modifier.AbilityPowerPercent += Item.Bonuses.AbilityPowerPercent;
                Edit.Modifier.ArmorPercent += Item.Bonuses.ArmorPercent;
            }
        }
    }
    if (Deployed > Limit) return false;
    for (const FEdit& Edit : Edits)
    {
        AActor* Occupant = Edit.Tile ? Edit.Tile->OccupiedUnit : Edit.Bench ? Edit.Bench->OccupiedUnit.Get() : nullptr;
        if (IsValid(Occupant) && !Vacating.Contains(Occupant)) return false;
    }

    TGuardValue<bool> PlacementGuard(Board->bPlacementInProgress, true);
    TArray<ABattleUnitBase*> Spawned;
    // 생성과 성장 기록을 먼저 검증해 기존 유닛의 위치를 바꾸기 전에 실패를 처리한다.
    for (FEdit& Edit : Edits)
    {
        if (Edit.Change->Edit != ERosterUnitEdit::Add) continue;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Location = Edit.Tile ? Edit.Tile->GetSurfaceLocation() : Edit.Bench->GetSurfaceLocation();
        Edit.Actor = Mode->GetWorld()->SpawnActor<ABattleUnitBase>(UnitClass, Location, FRotator::ZeroRotator, Params);
        if (IsValid(Edit.Actor)) Spawned.Add(Edit.Actor);
        FDataTableRowHandle Row;
        Row.DataTable = UnitDefinitions;
        Row.RowName = Edit.Change->Unit.UnitId;
        FString Error;
        if (!IsValid(Edit.Actor) || !Edit.Actor->InitializeUnit(Row, EBattleTeam::Player, Edit.Change->Unit.Stars, Error)
            || !Edit.Actor->CanRestoreSkillProgress(Edit.Change->Unit.SkillProgress))
        {
            for (ABattleUnitBase* Unit : Spawned) Unit->Destroy();
            return false;
        }
    }

    const auto ClearPlacement = [Board](ABattleUnitBase* Unit, ABenchTile* Slot)
    {
        Board->ReleaseUnitTiles(Unit);
        if (IsValid(Slot) && Slot->OccupiedUnit == Unit) Slot->ClearTile();
    };
    for (const FEdit& Edit : Edits)
        if (Vacating.Contains(Edit.Actor)) ClearPlacement(Edit.Actor, Edit.OldBench);
    for (const FEdit& Edit : Edits)
    {
        if ((!Edit.Tile && !Edit.Bench) || Board->ApplyUnitPlacement(Edit.Actor, Edit.Tile, Edit.Bench)) continue;
        for (const FEdit& Rollback : Edits)
            if (Rollback.Tile || Rollback.Bench) ClearPlacement(Rollback.Actor, Rollback.Bench);
        for (const FEdit& Rollback : Edits)
        {
            if (!Vacating.Contains(Rollback.Actor)) continue;
            if (!Board->ApplyUnitPlacement(Rollback.Actor, Rollback.OldTile, Rollback.OldBench)
                || !Rollback.Actor->SetActorTransform(Rollback.OldTransform, false, nullptr, ETeleportType::TeleportPhysics))
                UE_LOG(LogTemp, Error, TEXT("MainMap shop could not restore a unit after a failed placement."));
        }
        for (ABattleUnitBase* Unit : Spawned) Unit->Destroy();
        return false;
    }

    for (const FEdit& Edit : Edits)
    {
        const FRosterUnitChange& Change = *Edit.Change;
        if (Change.Edit == ERosterUnitEdit::Remove)
        {
            if (auto* PC = Cast<AAutoBattlePlayerController>(Mode->GetWorld()->GetFirstPlayerController()); PC && PC->SelectedUnit == Edit.Actor)
                PC->ClearSelection();
            Mode->BattleUnits.Remove(Edit.Actor);
            EquipmentByUnit.Remove(Change.Unit.Id);
            Units.Remove(Change.Unit.Id);
            Edit.Actor->Destroy();
            continue;
        }
        if (Change.Edit == ERosterUnitEdit::Add)
        {
            Mode->BattleUnits.Add(Edit.Actor);
            Units.Add(Change.Unit.Id, Edit.Actor);
            Edit.Actor->EquipmentQuery.BindUObject(this, &UMainMapShopPlacement::GetActorEquipment, Change.Unit.Id);
        }
        if (Edit.Actor->StarLevel != Change.Unit.Stars) Edit.Actor->SetStarLevel(Change.Unit.Stars);
        Edit.Actor->RestoreSkillProgress(Change.Unit.SkillProgress);
        if (Change.bReplaceEquipment)
        {
            EquipmentByUnit.Add(Change.Unit.Id, Change.Equipment);
            if (Change.Equipment.IsEmpty()) Edit.Actor->RemoveUpgrade(ShopEquipmentId);
            else Edit.Actor->ApplyUpgrade(ShopEquipmentId, Edit.Modifier);
            if (UUnitStatusWidget* Widget = Cast<UUnitStatusWidget>(Edit.Actor->StatusWidget->GetUserWidgetObject()))
            {
                TArray<FItemDefinition> Definitions;
                for (FName ItemId : Change.Equipment)
                {
                    FItemDefinition Definition;
                    if (Items->GetItem(ItemId, Definition)) Definitions.Add(Definition);
                }
                Widget->SetEquipment(Definitions);
            }
            Edit.Actor->OnEquipmentChanged.Broadcast(Edit.Actor);
        }
    }
    return true;
}
