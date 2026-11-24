#include "AutoBattleGameMode.h"
#include "Blueprint/UserWidget.h"
#include "BoardManager.h"
#include "EngineUtils.h"
#include "Shop/ItemManager.h"
#include "Shop/MainMapShopPlacement.h"
#include "Shop/RosterManager.h"
#include "Shop/ShopManager.h"
#include "Shop/ShopTransactionManager.h"
#include "UObject/UnrealType.h"

namespace
{
    template<typename ActorType>
    ActorType* FindOrSpawnShopActor(UWorld& World, const TCHAR* DefaultClass = nullptr)
    {
        ActorType* Found = nullptr;
        for (TActorIterator<ActorType> It(&World); It; ++It)
        {
            if (Found) return nullptr;
            Found = *It;
        }
        if (Found) return Found;
        UClass* Class = DefaultClass ? LoadClass<ActorType>(nullptr, DefaultClass) : ActorType::StaticClass();
        return Class ? World.SpawnActor<ActorType>(Class) : nullptr;
    }
}

bool AAutoBattleGameMode::InitializeMainMapShop()
{
    if (IsValid(MainMapShop) && MainMapShop->IsConfigured()) return true;
    FString FailureReason;
    if (!IsValid(BoardManager) || !BoardManager->TrySynchronizeBenchOccupancy(FailureReason))
    {
        UE_LOG(LogTemp, Error, TEXT("MainMap shop bench initialization failed: %s"), *FailureReason);
        return false;
    }
    UWorld& World = *GetWorld();
    AShopTransactionManager* Transactions = FindOrSpawnShopActor<AShopTransactionManager>(World);
    if (!IsValid(Transactions)) return false;
    if (!Transactions->Shop)
        Transactions->Shop = FindOrSpawnShopActor<AShopManager>(World, TEXT("/Game/BluePrints/Shop/BP_ShopManager.BP_ShopManager_C"));
    if (!Transactions->Roster)
        Transactions->Roster = FindOrSpawnShopActor<ARosterManager>(World, TEXT("/Game/BluePrints/Shop/BP_RosterManager.BP_RosterManager_C"));
    if (!Transactions->Items)
        Transactions->Items = FindOrSpawnShopActor<AItemManager>(World, TEXT("/Game/BluePrints/Shop/BP_ItemManager.BP_ItemManager_C"));
    if (!IsValid(Transactions->Shop) || !IsValid(Transactions->Roster) || !IsValid(Transactions->Items)) return false;

    UMainMapShopPlacement* Placement = NewObject<UMainMapShopPlacement>(this);
    if (!Placement->Initialize(*this, *Transactions->Roster, *Transactions->Items)) return false;
    const UDataTable* Table = Placement->GetUnitDefinitions();
    AShopManager& Shop = *Transactions->Shop;
    Shop.Catalog.RemoveAll([Table](const FShopOffer& Offer)
    {
        return Offer.UnitId.IsNone() || Offer.Cost <= 0
            || !Table->FindRow<FUnitDefinition>(Offer.UnitId, TEXT("Shop catalog"), false);
    });
    // 현재 메인맵의 카탈로그는 비어 있다. 편집한 카탈로그가 없을 때만 실제 DT 기물로 초기화한다.
    if (Shop.Catalog.IsEmpty())
    {
        for (FName Row : Table->GetRowNames())
        {
            const FUnitDefinition* Unit = Table->FindRow<FUnitDefinition>(Row, TEXT("Shop catalog"), false);
            if (Unit && Unit->Cost >= 1 && Unit->Cost <= 5)
                Shop.Catalog.Add({Row, FText::FromName(Row), Unit->Cost});
        }
    }
    if (Shop.StartingGold == 0) Shop.StartingGold = MainMapShopStartingGold;
    if (Shop.Catalog.IsEmpty()) return false;
    if (RoundsTable.IsNull() && Shop.RewardPool.IsEmpty())
    {
        for (const FShopOffer& Offer : Shop.Catalog)
            if (Offer.Cost == 1) Shop.RewardPool.Add({ERewardKind::Unit, Offer.UnitId, 1, 1.f});
        // 1코스트 기물 수가 적은 편성에서도 보상 카드 수를 채울 수 있게 한다.
        for (const FShopOffer& Offer : Shop.Catalog)
            if (Shop.RewardPool.Num() < AShopManager::RewardSlots && Offer.Cost != 1)
                Shop.RewardPool.Add({ERewardKind::Unit, Offer.UnitId, 1, 1.f});
    }
    if (RoundsTable.IsNull() && Shop.RewardPool.Num() < AShopManager::RewardSlots) return false;
    if (!RoundsTable.IsNull())
    {
        for (int32 Round = 1; Round <= 5; ++Round)
        {
            const auto* Definition = GetUnifiedRound(Round);
            TSet<FString> Unique;
            if (!Definition) return false;
            for (const FRewardOffer& Reward : Definition->RewardPool)
            {
                if (Reward.Weight == 0.f) continue;
                FItemDefinition Item;
                const bool Valid = Reward.Kind == ERewardKind::Unit
                    ? Reward.Stars >= 1 && Reward.Stars <= 2 && !Shop.GetOfferDefinition(Reward.DefinitionId).UnitId.IsNone()
                    : Reward.Kind == ERewardKind::Gold ? Reward.GoldAmount > 0 : Transactions->Items->GetItem(Reward.DefinitionId, Item);
                const FString Key = FString::Printf(TEXT("%d:%s:%d"), static_cast<int32>(Reward.Kind),
                    Reward.Kind == ERewardKind::Gold ? TEXT("Gold") : *Reward.DefinitionId.ToString(), Reward.Kind == ERewardKind::Unit ? Reward.Stars
                        : Reward.Kind == ERewardKind::Gold ? Reward.GoldAmount : 0);
                if (!Valid || !FMath::IsFinite(Reward.Weight) || Reward.Weight <= 0.f || Unique.Contains(Key))
                {
                    ManagedError(FString::Printf(TEXT("Invalid or duplicate reward in unified round R%d: %s"), Round, *Key));
                    return false;
                }
                Unique.Add(Key);
            }
            if (Unique.Num() < AShopManager::RewardSlots)
            {
                ManagedError(FString::Printf(TEXT("Unified round R%d needs at least four distinct reward candidates."), Round));
                return false;
            }
        }
    }

    MainMapShop = Transactions;
    MainMapShopPlacement = Placement;
    Transactions->OnRewardReceived.AddUniqueDynamic(this, &AAutoBattleGameMode::ManagedRewardReceived);
    BoardManager->OnUnitMovedToBench.AddUniqueDynamic(this, &AAutoBattleGameMode::ReturnEquipmentFromBench);
    return Transactions->IsConfigured();
}

const FAutoBattleRoundDefinition* AAutoBattleGameMode::GetUnifiedRound(int32 Round) const
{
    const UDataTable* Table = RoundsTable.LoadSynchronous();
    return Table && Table->GetRowStruct() == FAutoBattleRoundDefinition::StaticStruct()
        ? Table->FindRow<FAutoBattleRoundDefinition>(FName(*FString::Printf(TEXT("R%d"), Round)), TEXT("Unified round"), false) : nullptr;
}

bool AAutoBattleGameMode::GrantManagedClearGold()
{
    if (bManagedDefeated || ManagedLastOutcome != TEXT("Victory") || !IsValid(MainMapShop)) return false;
    int32 Amount = 0;
    // 테이블 미지정은 기존 맵과 동일하게 0골드로 처리한다.
    if (!RoundsTable.IsNull())
    {
        const auto* Row = GetUnifiedRound(CurrentRound);
        if (!Row) { ManagedError(TEXT("Unified round row missing.")); return false; }
        Amount = Row->ClearGold;
    }
    else if (!RoundRewardsTable.IsNull())
    {
        const UDataTable* Table = RoundRewardsTable.LoadSynchronous();
        const FName RowName(*FString::Printf(TEXT("R%d"), CurrentRound));
        const FRoundClearReward* Reward = Table && Table->GetRowStruct() == FRoundClearReward::StaticStruct()
            ? Table->FindRow<FRoundClearReward>(RowName, TEXT("Round clear reward"), false) : nullptr;
        if (!Reward || Reward->ClearGold < 0)
        {
            ManagedError(FString::Printf(TEXT("Round Rewards Table requires a valid %s row and non-negative ClearGold."), *RowName.ToString()));
            return false;
        }
        Amount = Reward->ClearGold;
    }
    if (!MainMapShop->GrantRoundGold(Amount))
    {
        ManagedError(TEXT("Round clear gold could not be credited."));
        return false;
    }
    LastClearGoldAwarded = Amount;
    UE_LOG(LogTemp, Display, TEXT("ROUND_CLEAR_GOLD: round=%d amount=%d"), CurrentRound, Amount);
    return true;
}

FShopMatchState AAutoBattleGameMode::GetShopMatchState_Implementation() const
{
    EShopPhase Phase = EShopPhase::Inactive;
    if (bManagedDefeated) return {Phase, CurrentRound, MaxBoardUnits, false};
    if (bManagedTestFinished) Phase = EShopPhase::Finished;
    else if (PendingShopRewardId.IsValid() && GetManagedRoundPhase() == 4) Phase = EShopPhase::RewardSelection;
    else if (CurrentPhase == EGamePhase::Preparation && bEncounterReady && bPreparationActive)
        Phase = EShopPhase::Preparation;
    else if (CurrentPhase == EGamePhase::Combat)
        Phase = bBattleResultPending ? EShopPhase::Settlement : EShopPhase::Combat;
    else if (CurrentPhase == EGamePhase::RoundClear) Phase = EShopPhase::Settlement;
    else if (CurrentPhase == EGamePhase::GameOver || CurrentPhase == EGamePhase::Victory)
        Phase = EShopPhase::Finished;
    return {Phase, CurrentRound, MaxBoardUnits, ManagedLastOutcome == TEXT("Victory"), LastClearGoldAwarded};
}

void AAutoBattleGameMode::ManagedRewardRequested(int32 CompletedRoundNumber, FGuid RewardRequestId)
{
    if (CompletedRoundNumber != CurrentRound || GetManagedRoundPhase() != 4
        || !ManagedRequestMatches(TEXT("bAwaitingReward"), TEXT("RewardRequestId"), RewardRequestId)) return;
    if (PendingShopRewardId == RewardRequestId) return;
    if (!RoundsTable.IsNull())
    {
        const auto* Row = GetUnifiedRound(CompletedRoundNumber);
        if (!Row || !IsValid(MainMapShop)) { ManagedError(TEXT("Unified reward pool missing.")); return; }
        MainMapShop->Shop->RewardPool = Row->RewardPool;
    }
    if (!IsValid(MainMapShop) || !RestoreManagedUnits())
    {
        ManagedError(TEXT("Could not restore participants for rewards"));
        return;
    }
    PendingShopRewardId = RewardRequestId;
    if (!MainMapShop->OpenRewards())
    {
        PendingShopRewardId.Invalidate();
        ManagedError(TEXT("Shop could not open the requested rewards"));
        return;
    }
    StatusMessage = FString::Printf(TEXT("Round %d: choose a reward."), CompletedRoundNumber);
    SetBattleHUDHiddenForResult(true);
    UE_LOG(LogTemp, Display, TEXT("MAINMAP_SHOP_REWARD_OPEN: round=%d"), CompletedRoundNumber);
}

void AAutoBattleGameMode::SetBattleHUDHiddenForResult(bool bHideBattleHUD)
{
    if (!bHideBattleHUD)
    {
        if (UUserWidget* Widget = HiddenBattleHUD.Get()) Widget->SetVisibility(SavedBattleHUDVisibility);
        HiddenBattleHUD.Reset();
        return;
    }
    if (HiddenBattleHUD.IsValid()) return;

    // 승리 연출과 보상 선택 중에는 메인맵 BP가 만든 기존 시작 버튼을 숨긴다.
    const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("BattleHUD"));
    UUserWidget* Widget = Property ? Cast<UUserWidget>(Property->GetObjectPropertyValue_InContainer(this)) : nullptr;
    if (!IsValid(Widget)) return;
    HiddenBattleHUD = Widget;
    SavedBattleHUDVisibility = Widget->GetVisibility();
    Widget->SetVisibility(ESlateVisibility::Collapsed);
}

void AAutoBattleGameMode::ReturnEquipmentFromBench(ABattleUnitBase* Unit)
{
    if (!IsValid(MainMapShopPlacement) || !IsValid(MainMapShop)) return;
    const FGuid Id = MainMapShopPlacement->FindUnitId(Unit);
    if (Id.IsValid() && !MainMapShop->ReturnEquipmentFromBench(Id))
        UE_LOG(LogTemp, Warning, TEXT("MainMap shop could not return equipment after moving a unit to the bench."));
}
