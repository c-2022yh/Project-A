#include "ShopUnitBridge.h"
#include "ShopManager.h"
#include "RosterManager.h"
#include "ItemManager.h"
#include "ShopTransactionManager.h"
#include "../AutoBattleGameMode.h"
#include "../AutoBattlePlayerController.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "../BenchTile.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
bool SamePosition(const FUnitPosition& A, const FUnitPosition& B)
{
    return A.Location == B.Location && A.Column == B.Column
        && (A.Location == EUnitLocation::Bench || A.Row == B.Row);
}
bool ValidBonuses(const FRosterUnitChange& C)
{
    return FMath::IsFinite(C.StarStatMultiplier) && C.StarStatMultiplier >= 1.f
        && FMath::IsFinite(C.StarVisualScale) && C.StarVisualScale >= 1.f
        && FMath::IsFinite(C.ItemBonuses.HP) && FMath::IsFinite(C.ItemBonuses.AttackDamage)
        && FMath::IsFinite(C.ItemBonuses.AbilityPower) && FMath::IsFinite(C.ItemBonuses.Armor)
        && FMath::IsFinite(C.ItemBonuses.AttackSpeed);
}
}

AShopUnitBridge::AShopUnitBridge()
{
    PrimaryActorTick.bCanEverTick = false;
    UnitClass = TSoftClassPtr<ABattleUnitBase>(FSoftObjectPath(TEXT("/Game/BluePrints/Units/Test/BP_UnitBase.BP_UnitBase_C")));
    UnitDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/Units/DT_Units.DT_Units")));
    ShopClass = AShopManager::StaticClass(); RosterClass = ARosterManager::StaticClass(); ItemClass = AItemManager::StaticClass();
}

bool AShopUnitBridge::Fail(const FString& Message)
{
    Status = Message;
    UE_LOG(LogTemp, Warning, TEXT("ShopUnitBridge: %s"), *Message);
    return false;
}

void AShopUnitBridge::BeginPlay()
{
    Super::BeginPlay();
    if (!bAutoSetupSingleBattle) return;
    Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(Mode)) { Fail(TEXT("This bridge requires AutoBattleGameMode or its BP subclass.")); return; }
    if (IsValid(Mode->ShopBridge) || Mode->bEncounterReady)
    { Fail(TEXT("Place one bridge before Play; do not attach it to an existing encounter.")); return; }
    // GameMode가 다음 Tick에 보드를 준비한 뒤 이 액터로 초기화 요청을 보낸다.
    Mode->ShopBridge = this;
}

bool AShopUnitBridge::Connect(AAutoBattleGameMode* GameMode, AShopTransactionManager* Manager)
{
    if (bApplying || !Links.IsEmpty() || !IsValid(GameMode) || !IsValid(GameMode->BoardManager)
        || GameMode->GetWorld() != GetWorld() || !IsValid(Manager) || Manager->GetWorld() != GetWorld()
        || !IsValid(Manager->Shop) || !IsValid(Manager->Roster) || !IsValid(Manager->Items)
        || Manager->Shop->GetWorld() != GetWorld() || Manager->Roster->GetWorld() != GetWorld()
        || Manager->Items->GetWorld() != GetWorld() || !Manager->Roster->GetUnits().IsEmpty()
        || (IsValid(GameMode->ShopBridge) && GameMode->ShopBridge != this))
        return Fail(TEXT("Connect requires same-world managers, an empty roster and a unique bridge."));
    if (IsValid(Manager->Roster->Placement.GetObject()) && Manager->Roster->Placement.GetObject() != this)
        return Fail(TEXT("Roster already belongs to another placement adapter."));
    for (ABattleUnitBase* Unit : GameMode->BattleUnits)
        if (IsValid(Unit) && Unit->TeamType == EBattleTeam::Player)
            return Fail(TEXT("Remove the old player spawn path before connecting the shop-owned roster."));
    Mode = GameMode; Board = GameMode->BoardManager; Transactions = Manager;
    Manager->Roster->Placement.SetObject(this); Manager->Roster->Placement.SetInterface(this);
    Mode->ShopBridge = this;
    Status = TEXT("Connected. Shop UnitId must equal a DT_Units row name.");
    return Manager->IsConfigured();
}

void AShopUnitBridge::ClearOwnedUnits()
{
    for (const auto& Pair : Links)
    {
        ABattleUnitBase* Unit = Pair.Value.Actor;
        if (!IsValid(Unit)) continue;
        if (IsValid(Board))
            for (ABenchTile* Slot : Board->BenchSlots)
                if (IsValid(Slot) && Slot->OccupiedUnit == Unit) Slot->ClearTile();
        if (IsValid(Mode)) Mode->BattleUnits.Remove(Unit);
        Unit->Destroy();
    }
    Links.Empty();
}

void AShopUnitBridge::InitializeSingleBattle()
{
    if (!IsValid(Mode) || Mode->ShopBridge != this) return;
    ClearOwnedUnits();
    for (AActor* Actor : OwnedManagers) if (IsValid(Actor)) Actor->Destroy();
    OwnedManagers.Empty(); Transactions = nullptr; Board = Mode->BoardManager;
    UDataTable* Table = UnitDataTable.LoadSynchronous();
    UClass* Class = UnitClass.LoadSynchronous();
    if (!IsValid(Board) || !Class || Class->HasAnyClassFlags(CLASS_Abstract) || !Table
        || Table->GetRowStruct() != FUnitDefinition::StaticStruct() || AvailableUnits.IsEmpty()
        || StartingGold < 0 || DeploymentLimit < 1 || !ShopClass || !RosterClass || !ItemClass)
    { Fail(TEXT("Assign a unit BP, UnitDefinition DT, catalog rows and manager classes.")); return; }
    TArray<FShopOffer> Catalog;
    TSet<FName> Seen;
    for (FName Name : AvailableUnits)
    {
        const FUnitDefinition* Definition = Table->FindRow<FUnitDefinition>(Name, TEXT("Shop catalog"), false);
        if (!Definition || Definition->Cost < 1 || Definition->Cost > 5 || Seen.Contains(Name))
        { Fail(TEXT("Catalog contains a missing/duplicate row or invalid cost.")); return; }
        Seen.Add(Name); Catalog.Add({Name, Definition->DisplayName.IsEmpty() ? FText::FromName(Name) : Definition->DisplayName, Definition->Cost});
    }
    ABoardTile* EnemyTile = Board->GetTile(EnemyBoardRow, EnemyBoardColumn);
    if (!EnemyTile || EnemyTile->TeamType != EBoardTileTeam::Enemy || !EnemyTile->CanPlaceUnit()
        || !Table->FindRow<FUnitDefinition>(EnemyRow, TEXT("Shop enemy"), false))
    { Fail(TEXT("Assign an existing enemy DT row and a free enemy tile.")); return; }
    if (!Mode->BeginEncounterSetup()) { Fail(Mode->StatusMessage); return; }
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABattleUnitBase* Enemy = GetWorld()->SpawnActor<ABattleUnitBase>(Class, FTransform::Identity, Spawn);
    FDataTableRowHandle Data; Data.DataTable = Table; Data.RowName = EnemyRow;
    FString Error;
    if (!Enemy || !Enemy->InitializeUnit(Data, EBattleTeam::Enemy, 1, Error)
        || !Mode->RegisterBattleUnit(Enemy) || !Enemy->PlaceOnTile(EnemyTile))
    {
        if (IsValid(Enemy)) Enemy->Destroy();
        Mode->CancelEncounterSetup(); Fail(TEXT("Enemy setup failed: ") + Error); return;
    }
    AShopManager* Shop = GetWorld()->SpawnActor<AShopManager>(ShopClass);
    ARosterManager* Roster = GetWorld()->SpawnActor<ARosterManager>(RosterClass);
    AItemManager* Items = GetWorld()->SpawnActor<AItemManager>(ItemClass);
    AShopTransactionManager* Manager = GetWorld()->SpawnActor<AShopTransactionManager>();
    for (AActor* Actor : {static_cast<AActor*>(Shop), static_cast<AActor*>(Roster), static_cast<AActor*>(Items), static_cast<AActor*>(Manager)})
        if (Actor) OwnedManagers.Add(Actor);
    if (!Shop || !Roster || !Items || !Manager)
    { Mode->CancelEncounterSetup(); Fail(TEXT("Manager spawn failed.")); return; }
    Shop->Catalog = MoveTemp(Catalog); Shop->StartingGold = StartingGold;
    Manager->Shop = Shop; Manager->Roster = Roster; Manager->Items = Items;
    Mode->MaxBoardUnits = DeploymentLimit; Mode->CurrentRound = Mode->MaxRound = 1; Mode->PreparationDuration = 0.f;
    if (!Connect(Mode, Manager) || !Mode->CompleteEncounterSetup() || !Manager->EnterPreparation(1, DeploymentLimit))
    { Mode->CancelEncounterSetup(); Fail(TEXT("Shop preparation failed. No automatic start was performed.")); return; }
    Mode->StartPreparationPhase();
    Mode->StatusMessage = Status = TEXT("Buy a unit, drag it onto the board, then press Start combat.");
}

ABattleUnitBase* AShopUnitBridge::GetUnitActor(FGuid Id) const
{
    const FShopUnitLink* Link = Links.Find(Id);
    return Link && IsValid(Link->Actor) ? Link->Actor.Get() : nullptr;
}
FGuid AShopUnitBridge::GetUnitId(ABattleUnitBase* Actor) const
{
    if (!IsValid(Actor)) return {};
    for (const auto& Pair : Links) if (Pair.Value.Actor == Actor) return Pair.Key;
    return {};
}
FUnitPosition AShopUnitBridge::GetUnitPosition_Implementation(FGuid Id) const
{
    const FShopUnitLink* Link = Links.Find(Id);
    // 전투 중 실제 이동 타일이 바뀌어도 상점에는 준비 배치를 돌려준다.
    return Link && IsValid(Link->Actor) ? Link->State.Destination : FUnitPosition();
}
FGuid AShopUnitBridge::GetOccupant_Implementation(FUnitPosition Position) const
{
    for (const auto& Pair : Links)
        if (IsValid(Pair.Value.Actor) && SamePosition(Pair.Value.State.Destination, Position)) return Pair.Key;
    return {};
}
FUnitPosition AShopUnitBridge::FindRewardTile_Implementation(int32 Limit) const
{
    if (!IsValid(Board) || !IsValid(Mode) || Mode->GetDeployedPlayerCount() >= Limit) return {};
    for (ABoardTile* Tile : Board->GetPlayerTiles())
    {
        const FUnitPosition Position{EUnitLocation::Board, Tile->Row, Tile->Column};
        if (Tile->CanPlaceUnit() && !GetOccupant_Implementation(Position).IsValid()) return Position;
    }
    return {};
}
bool AShopUnitBridge::GetUnitProfile_Implementation(FGuid Id, FRosterUnitProfile& Profile) const
{
    const ABattleUnitBase* Unit = GetUnitActor(Id);
    if (!Unit) return false;
    const FUnitStats& S = Unit->FinalStats;
    Profile.MaxHP = S.MaxHealth; Profile.AttackDamage = S.AttackDamage; Profile.AbilityPower = S.AbilityPower;
    Profile.Armor = S.Armor; Profile.AttackSpeed = S.AttackSpeed; Profile.AttackRange = S.AttackRange; Profile.MaxMP = S.MaxMana;
    return true;
}
FGuid AShopUnitBridge::GetHoveredUnit_Implementation() const
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    FHitResult Hit;
    return PC && PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit)
        ? GetUnitId(Cast<ABattleUnitBase>(Hit.GetActor())) : FGuid();
}

bool AShopUnitBridge::CanStartLinkedCombat() const
{
    if (bApplying || !IsValid(Mode) || !IsValid(Board) || !IsValid(Transactions) || !Transactions->IsConfigured()
        || Transactions->Shop->GetPhase() != EShopPhase::Preparation || Mode->CurrentPhase != EGamePhase::Preparation
        || !Mode->bEncounterReady || !Mode->bPreparationActive || !Board->IsPlacementEnabled()
        || Mode->GetDeployedPlayerCount() < 1 || Mode->GetDeployedPlayerCount() > Transactions->Shop->GetDeploymentLimit()) return false;
    const auto* PC = Cast<AAutoBattlePlayerController>(GetWorld()->GetFirstPlayerController());
    if (PC && PC->IsUnitDragActive()) return false;
    for (const auto& Pair : Links)
    {
        FString Reason;
        if (!Board->ValidateUnitPlacementState(Pair.Value.Actor, Reason)) return false;
    }
    return true;
}
bool AShopUnitBridge::StartLinkedCombat()
{
    if (!CanStartLinkedCombat()) return Fail(TEXT("Place at least one unit; finish dragging and close placement locks before starting."));
    if (!Board->SavePlayerFormation()) return Fail(Board->LastFormationFailureReason);
    if (!Transactions->Shop->BeginCombat()) return false;
    // 모두 같은 명단을 사용한다. 대기석 기물은 UnitBase.StartCombat이 제외한다.
    for (ABattleUnitBase* Unit : Mode->BattleUnits) if (IsValid(Unit)) Unit->SetCombatRoster(Mode->BattleUnits);
    Mode->StartCombatPhase();
    return Mode->CurrentPhase == EGamePhase::Combat;
}
void AShopUnitBridge::FinishLinkedCombat(uint8 Result)
{
    if (!IsValid(Mode) || !IsValid(Transactions)) return;
    Transactions->Shop->BeginSettlement();
    if (static_cast<EGamePhase>(Result) == EGamePhase::GameOver) Mode->HandleGameOver();
    else Mode->HandleVictory();
    Transactions->Shop->FinishMatch();
    Status = TEXT("Single battle complete. Restart to repeat; five-round integration is a separate step.");
}

void AShopUnitBridge::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(Mode) && Mode->ShopBridge == this) Mode->ShopBridge = nullptr;
    if (IsValid(Transactions) && IsValid(Transactions->Roster) && Transactions->Roster->Placement.GetObject() == this)
        Transactions->Roster->Placement = TScriptInterface<IUnitPlacement>();
    ClearOwnedUnits();
    for (AActor* Actor : OwnedManagers) if (IsValid(Actor)) Actor->Destroy();
    OwnedManagers.Empty();
    Super::EndPlay(Reason);
}

bool AShopUnitBridge::ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit)
{
    if (bApplying || !IsValid(Mode) || !IsValid(Board) || !IsValid(Transactions) || !Transactions->IsConfigured()
        || Board->bPlacementInProgress || Board->bBoardGenerationInProgress || Board->bBoardGenerationNotificationInProgress
        || Mode->bBattleResultPending || Limit < 1) return Fail(TEXT("Board/shop is not ready for a transaction."));
    const bool Combat = Phase == EShopPhase::Combat;
    if ((Combat && Mode->CurrentPhase != EGamePhase::Combat)
        || (!Combat && Phase != EShopPhase::Inactive && Phase != EShopPhase::Preparation && Phase != EShopPhase::RewardSelection)
        || (!Combat && Mode->CurrentPhase != EGamePhase::Preparation && Mode->CurrentPhase != EGamePhase::RoundClear))
        return Fail(TEXT("Shop and combat phases do not permit this transaction."));
    TGuardValue<bool> ApplyingGuard(bApplying, true);
    TGuardValue<bool> BoardGuard(Board->bPlacementInProgress, true);
    TMap<FGuid, FShopUnitLink> Next = Links;
    TSet<FGuid> Changed;
    TSet<ABattleUnitBase*> Affected;
    for (const FRosterUnitChange& C : Changes)
    {
        const FGuid Id = C.Unit.Id;
        const FShopUnitLink* Existing = Links.Find(Id);
        if (!Id.IsValid() || Changed.Contains(Id) || C.Unit.UnitId.IsNone() || C.Unit.Stars < 1 || C.Unit.Stars > 2 || !ValidBonuses(C))
            return Fail(TEXT("Invalid/duplicate unit change."));
        Changed.Add(Id);
        if (C.Edit == ERosterUnitEdit::Add)
        {
            if (Existing) return Fail(TEXT("Cannot add an existing unit ID."));
            FShopUnitLink Link; Link.State = C; Next.Add(Id, Link);
        }
        else
        {
            if (!Existing || !IsValid(Existing->Actor) || !Existing->Actor->IsAlive() || Existing->Actor->bInCombat
                || !Mode->BattleUnits.Contains(Existing->Actor) || Existing->State.Unit.UnitId != C.Unit.UnitId
                || (Combat && Existing->State.Destination.Location != EUnitLocation::Bench))
                return Fail(TEXT("Unit is missing, active in combat, or owned by another system."));
            // 범위 밖 직접 배치 변경이 있으면 상점 상태로 덮어쓰지 않는다.
            const FUnitPosition Old = Existing->State.Destination;
            ABattleUnitBase* Unit = Existing->Actor;
            if ((Old.Location == EUnitLocation::Board && (Unit->CurrentTile != Board->GetTile(Old.Row, Old.Column)
                    || !Unit->CurrentTile || Unit->CurrentTile->OccupiedUnit != Unit))
                || (Old.Location == EUnitLocation::Bench && (!Board->GetBenchSlot(Old.Column)
                    || Board->GetBenchSlot(Old.Column)->OccupiedUnit != Unit || Unit->CurrentTile || Unit->bIsDeployed)))
                return Fail(TEXT("Physical occupancy differs from the owned unit position."));
            Affected.Add(Unit);
            if (C.Edit == ERosterUnitEdit::Remove) Next.Remove(Id);
            else if (C.Edit == ERosterUnitEdit::Move || C.Edit == ERosterUnitEdit::Update)
            {
                Next[Id].State = C;
                if (C.Edit == ERosterUnitEdit::Update) Next[Id].State.Destination = Old;
            }
            else return Fail(TEXT("Unknown unit edit."));
        }
    }
    int32 Deployed = 0;
    for (const auto& Pair : Next)
    {
        const FUnitPosition P = Pair.Value.State.Destination;
        for (const auto& Other : Next)
            if (Pair.Key != Other.Key && SamePosition(P, Other.Value.State.Destination)) return Fail(TEXT("Duplicate destination."));
        AActor* Occupant = nullptr;
        if (P.Location == EUnitLocation::Board)
        {
            ABoardTile* Tile = Board->GetTile(P.Row, P.Column);
            if (Combat && Changed.Contains(Pair.Key)) return Fail(TEXT("Combat transactions are bench-only."));
            if (!Tile || Tile->TeamType != EBoardTileTeam::Player || Tile->bBlocksMovement
                || (Changed.Contains(Pair.Key) && Tile->IsReserved())) return Fail(TEXT("Invalid board destination."));
            // 전투 중 출전 기물의 준비 타일은 다른 기물이 지나갈 수 있으므로 조회만 유지한다.
            if (!Combat) Occupant = Tile->OccupiedUnit;
            ++Deployed;
        }
        else if (P.Location == EUnitLocation::Bench && P.Column >= 0 && P.Column < 7)
        {
            ABenchTile* Slot = Board->GetBenchSlot(P.Column);
            if (!Slot || !Slot->HasConsistentOccupancy()) return Fail(TEXT("Invalid bench slot."));
            Occupant = Slot->OccupiedUnit;
        }
        else return Fail(TEXT("Invalid destination type."));
        if (IsValid(Occupant) && Occupant != Pair.Value.Actor && !Affected.Contains(Cast<ABattleUnitBase>(Occupant)))
            return Fail(TEXT("Destination belongs to another unit."));
    }
    if (Deployed > Limit) return Fail(TEXT("Deployment limit exceeded."));

    // 먼저 신규 액터를 준비한다. 하나라도 실패하면 기존 점유/골드/보유 상태는 건드리지 않는다.
    TArray<ABattleUnitBase*> Created;
    const auto Discard = [&]() { for (ABattleUnitBase* Unit : Created) if (IsValid(Unit)) Unit->Destroy(); };
    UDataTable* Table = UnitDataTable.LoadSynchronous();
    UClass* Class = UnitClass.LoadSynchronous();
    for (const FRosterUnitChange& C : Changes)
    {
        if (C.Edit != ERosterUnitEdit::Add) continue;
        if (!Table || !Class || Class->HasAnyClassFlags(CLASS_Abstract)) { Discard(); return Fail(TEXT("Unit BP/DT is missing.")); }
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABattleUnitBase* Unit = GetWorld()->SpawnActor<ABattleUnitBase>(Class, FTransform::Identity, Spawn);
        if (!Unit) { Discard(); return false; }
        Created.Add(Unit);
        FDataTableRowHandle Data; Data.DataTable = Table; Data.RowName = C.Unit.UnitId;
        FString Error;
        if (!Unit->InitializeUnit(Data, EBattleTeam::Player, C.Unit.Stars, Error)) { Discard(); return Fail(Error); }
        Next[C.Unit.Id].Actor = Unit;
    }
    // 동기식 배치 중 실패할 경우 기물의 준비 상태·아이템 보정·점유까지 복구한다.
    struct FSnapshot
    {
        ABattleUnitBase* Unit; FTransform Transform, Preparation, Visual;
        FUnitDefinition Definition; FUnitStats Stats; TMap<FName, FUnitStatModifier> Upgrades;
        ABoardTile* Tile; ABoardTile* PreparationTile; EUnitPlacement Placement, PreparationPlacement;
        int32 Stars; float HP, Mana; bool Deployed;
    };
    TArray<FSnapshot> Before;
    for (ABattleUnitBase* Unit : Affected)
        Before.Add({Unit, Unit->GetActorTransform(), Unit->PreparationTransform, Unit->VisualRoot->GetRelativeTransform(),
            Unit->Definition, Unit->FinalStats, Unit->Upgrades, Unit->CurrentTile, Unit->PreparationTile,
            Unit->Placement, Unit->PreparationPlacement, Unit->StarLevel, Unit->CurrentHP, Unit->CurrentMana, Unit->bIsDeployed});
    TMap<ABoardTile*, AActor*> TileBefore;
    TMap<ABenchTile*, AActor*> BenchBefore;
    for (ABoardTile* Tile : Board->Tiles)
        if (IsValid(Tile) && Affected.Contains(Cast<ABattleUnitBase>(Tile->OccupiedUnit))) TileBefore.Add(Tile, Tile->OccupiedUnit);
    for (ABenchTile* Slot : Board->BenchSlots)
        if (IsValid(Slot) && Affected.Contains(Cast<ABattleUnitBase>(Slot->OccupiedUnit))) BenchBefore.Add(Slot, Slot->OccupiedUnit);
    for (const FGuid& Id : Changed)
    {
        const FShopUnitLink* Link = Next.Find(Id);
        if (!Link) continue;
        const FUnitPosition P = Link->State.Destination;
        if (P.Location == EUnitLocation::Board)
        { ABoardTile* Tile = Board->GetTile(P.Row, P.Column); if (!TileBefore.Contains(Tile)) TileBefore.Add(Tile, Tile->OccupiedUnit); }
        else
        { ABenchTile* Slot = Board->GetBenchSlot(P.Column); if (!BenchBefore.Contains(Slot)) BenchBefore.Add(Slot, Slot->OccupiedUnit); }
    }
    const auto Clear = [&]()
    {
        for (const auto& Pair : TileBefore) if (IsValid(Pair.Key)) Pair.Key->ClearTile();
        for (const auto& Pair : BenchBefore) if (IsValid(Pair.Key)) Pair.Key->ClearTile();
    };
    Clear();
    bool Success = true;
    for (const FGuid& Id : Changed)
    {
        FShopUnitLink* Link = Next.Find(Id);
        if (!Link) continue;
        ABattleUnitBase* Unit = Link->Actor;
        const FRosterUnitChange& C = Link->State;
        const FUnitPosition P = C.Destination;
        if (!IsValid(Unit) || !Board->ApplyUnitPlacement(Unit,
            P.Location == EUnitLocation::Board ? Board->GetTile(P.Row, P.Column) : nullptr,
            P.Location == EUnitLocation::Bench ? Board->GetBenchSlot(P.Column) : nullptr)) { Success = false; break; }
        // 성급 배율은 한 번만 적용한다. 원본 DT와 다른 강화 효과는 보존한다.
        Unit->Definition.StarHealthMultipliers = {1.f, C.StarStatMultiplier};
        Unit->Definition.StarAttackMultipliers = {1.f, C.StarStatMultiplier};
        Unit->StarLevel = C.Unit.Stars;
        FUnitStatModifier Items;
        Items.MaxHealth = C.ItemBonuses.HP; Items.AttackDamage = C.ItemBonuses.AttackDamage;
        Items.AbilityPower = C.ItemBonuses.AbilityPower; Items.Armor = C.ItemBonuses.Armor; Items.AttackSpeedBonus = C.ItemBonuses.AttackSpeed;
        Unit->Upgrades.Add(TEXT("Shop.Items"), Items); Unit->RecalculateStats();
        Unit->VisualRoot->SetRelativeScale3D(Unit->Definition.Visuals.VisualTransform.GetScale3D() * C.StarVisualScale);
        if (!IsValid(Unit) || !Unit->IsAlive()) { Success = false; break; }
    }
    if (!Success)
    {
        Clear();
        for (const auto& Pair : TileBefore) if (IsValid(Pair.Key) && IsValid(Pair.Value)) Pair.Key->SetOccupiedUnit(Pair.Value);
        for (const auto& Pair : BenchBefore) if (IsValid(Pair.Key) && IsValid(Pair.Value)) Pair.Key->SetOccupiedUnit(Pair.Value);
        for (const FSnapshot& S : Before)
        {
            if (!IsValid(S.Unit)) continue;
            S.Unit->Definition = S.Definition; S.Unit->FinalStats = S.Stats; S.Unit->Upgrades = S.Upgrades;
            S.Unit->StarLevel = S.Stars; S.Unit->CurrentHP = S.HP; S.Unit->CurrentMana = S.Mana;
            S.Unit->CurrentTile = S.Tile; S.Unit->PreparationTile = S.PreparationTile;
            S.Unit->Placement = S.Placement; S.Unit->PreparationPlacement = S.PreparationPlacement;
            S.Unit->PreparationTransform = S.Preparation; S.Unit->bIsDeployed = S.Deployed;
            S.Unit->SetActorTransform(S.Transform); S.Unit->VisualRoot->SetRelativeTransform(S.Visual);
        }
        Discard(); return Fail(TEXT("Transaction rejected; existing placements and unit stats restored."));
    }
    for (ABattleUnitBase* Unit : Created) Mode->BattleUnits.AddUnique(Unit);
    for (const FRosterUnitChange& C : Changes)
        if (C.Edit == ERosterUnitEdit::Remove)
        {
            ABattleUnitBase* Unit = Links[C.Unit.Id].Actor;
            Mode->BattleUnits.Remove(Unit);
            if (auto* PC = Cast<AAutoBattlePlayerController>(GetWorld()->GetFirstPlayerController()))
                if (PC->SelectedUnit == Unit) PC->ClearSelection();
            Unit->Destroy();
        }
    Links = MoveTemp(Next);
    Status = TEXT("Unit transaction committed.");
    return true;
}
