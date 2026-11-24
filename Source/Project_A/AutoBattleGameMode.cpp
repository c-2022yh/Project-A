#include "AutoBattleGameMode.h"
#include "AutoBattlePlayerController.h"
#include "AutoBattleHUD.h"
#include "BoardManager.h"
#include "BoardTile.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "Shop/ShopTransactionManager.h"
#include "UnifiedRoundManager.h"

AAutoBattleGameMode::AAutoBattleGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PlayerControllerClass = AAutoBattlePlayerController::StaticClass();
    HUDClass = AAutoBattleHUD::StaticClass();
    DefaultPawnClass = nullptr;
    MainMapUnitClass = TSoftClassPtr<ABattleUnitBase>(
        FSoftObjectPath(TEXT("/Game/BluePrints/Units/UnitBase/BP_UnitBase.BP_UnitBase_C")));
    MainMapUnitDefinitions = TSoftObjectPtr<UDataTable>(
        FSoftObjectPath(TEXT("/Game/Data/DT_Units.DT_Units")));
}
void AAutoBattleGameMode::BeginPlay()
{
    Super::BeginPlay();
    // 생성된 타일을 사용해야 하므로 BoardManager의 BeginPlay가 끝난 다음 유닛 구성을 초기화한다.
    GetWorldTimerManager().SetTimerForNextTick(this, &AAutoBattleGameMode::InitializeEncounter);
}
void AAutoBattleGameMode::ChangePhase(EGamePhase Phase)
{
    CurrentPhase = Phase;
    if (Phase != EGamePhase::Combat)
        for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StopCombat();
    if (Phase != EGamePhase::Preparation) bPreparationActive = false;
    if (Phase != EGamePhase::Combat) bBattleResultPending = false;
    OnPhaseChanged.Broadcast(Phase);
    ReceiveGamePhaseChanged(Phase);
}
void AAutoBattleGameMode::ClearEncounter()
{
    GetWorldTimerManager().ClearTimer(RoundClearTimer);
    if (bShowingRoundClear)
    {
        bShowingRoundClear = false;
        if (RoundClearBanner.IsValid()) RoundClearBanner->SetVisibility(EVisibility::Collapsed);
        SetBattleHUDHiddenForResult(false);
    }
    bEncounterReady = false;
    bEncounterSetupOpen = false;
    bPreparationActive = false;
    bPreparationExpiryReported = false;
    bBattleResultPending = false;
    if (AAutoBattlePlayerController* PC = Cast<AAutoBattlePlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
        PC->ClearSelection();
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->Destroy();
    BattleUnits.Empty();
}
void AAutoBattleGameMode::InitializeEncounter()
{
    ClearEncounter();
    // 초기화 상태만 설정한다. 실제 배치가 가능한 준비 단계 알림은 아직 보내지 않는다.
    CurrentPhase = EGamePhase::Preparation;
    RemainingPreparationTime = 0;
    ElapsedCombatTime = 0;
    TArray<AActor*> Boards;
    UGameplayStatics::GetAllActorsOfClass(this, ABoardManager::StaticClass(), Boards);
    if (Boards.Num() != 1)
    {
        StatusMessage = TEXT("Setup: place exactly one BoardManager in the level.");
        return;
    }
    BoardManager = Cast<ABoardManager>(Boards[0]);
    if (BoardManager->Tiles.IsEmpty()) BoardManager->GenerateBoard();
    const TArray<ABoardTile*> PlayerTiles = BoardManager->GetPlayerTiles();
    if (PlayerTiles.IsEmpty() || BoardManager->GetEnemyTiles().IsEmpty())
    {
        StatusMessage = TEXT("Setup: generate both player and enemy tile areas.");
        return;
    }

    if (bEnableMainMapRoundTest && (bUseRoundEnemyFormations || !RoundsTable.IsNull()))
    {
        // 메인맵의 라운드 편성은 테스트 UnitRoster를 생성하는 Board Ready 그래프를 거치지 않는다.
        bEncounterReady = true;
        StartPreparationPhase();
        return;
    }

    StatusMessage = TEXT("Board ready. Spawn units and start preparation in the GameMode graph.");
    ReceiveBoardReady();
}

bool AAutoBattleGameMode::BeginEncounterSetup()
{
    if (CurrentPhase != EGamePhase::Preparation || bEncounterReady || bEncounterSetupOpen || !BattleUnits.IsEmpty())
    {
        StatusMessage = TEXT("Setup: units already exist or another setup is active. Do not run both spawn paths.");
        return false;
    }
    if (!IsValid(BoardManager) || BoardManager->GetPlayerTiles().IsEmpty() || BoardManager->GetEnemyTiles().IsEmpty())
    {
        StatusMessage = TEXT("Setup: wait for Board Ready and provide both tile areas.");
        return false;
    }
    bEncounterSetupOpen = true;
    bPreparationActive = false;
    StatusMessage = TEXT("Setup: spawn and register units, then complete the encounter setup.");
    return true;
}

bool AAutoBattleGameMode::RegisterBattleUnit(ABattleUnitBase* Unit)
{
    if (!bEncounterSetupOpen || CurrentPhase != EGamePhase::Preparation || bEncounterReady)
    {
        StatusMessage = TEXT("Setup: call Begin Encounter Setup before registering units.");
        return false;
    }
    if (!IsValid(Unit) || Unit->GetWorld() != GetWorld() || !Unit->IsAlive() || Unit->bInCombat
        || (Unit->TeamType != EBattleTeam::Player && Unit->TeamType != EBattleTeam::Enemy))
    {
        StatusMessage = TEXT("Setup: register a living, inactive battle unit from this world.");
        return false;
    }
    if (BattleUnits.Contains(Unit))
    {
        StatusMessage = TEXT("Setup: this unit is already registered.");
        return false;
    }
    Unit->StopCombat();
    BattleUnits.Add(Unit);
    return true;
}

bool AAutoBattleGameMode::MoveUnitToWaitingArea(ABattleUnitBase* Unit, int32 WaitingIndex, int32 WaitingCount)
{
    if (!bEncounterSetupOpen || CurrentPhase != EGamePhase::Preparation || !IsValid(BoardManager)
        || !IsValid(Unit) || !BattleUnits.Contains(Unit) || !Unit->IsAlive()
        || Unit->TeamType != EBattleTeam::Player || WaitingCount <= 0 || WaitingIndex < 0 || WaitingIndex >= WaitingCount)
    {
        StatusMessage = TEXT("Setup: waiting positions require a registered player and a valid zero-based index/count.");
        return false;
    }
    const TArray<ABoardTile*> PlayerTiles = BoardManager->GetPlayerTiles();
    if (PlayerTiles.IsEmpty() || !IsValid(PlayerTiles[0]->TileMesh))
    {
        StatusMessage = TEXT("Setup: a player tile mesh is required to calculate waiting positions.");
        return false;
    }
    Unit->StopCombat();
    const FVector NearSurface = PlayerTiles[0]->GetSurfaceLocation();
    const FBoxSphereBounds NearBounds = PlayerTiles[0]->TileMesh->CalcBounds(PlayerTiles[0]->TileMesh->GetComponentTransform());
    const float Spacing = FMath::Max(300.0f, BoardManager->EffectiveHorizontalSpacing * 0.65f);
    const FVector Center(BoardManager->GetActorLocation().X,
        NearBounds.Origin.Y - NearBounds.BoxExtent.Y + 120.0f, NearSurface.Z);
    const FVector Position = Center + WaitingAreaOffset
        + FVector((WaitingIndex - (WaitingCount - 1) * 0.5f) * Spacing, 0,
            1.0f);
    return Unit->SetWaitingPlacement(Position);
}

bool AAutoBattleGameMode::CompleteEncounterSetup()
{
    if (!bEncounterSetupOpen || CurrentPhase != EGamePhase::Preparation || bEncounterReady || !IsValid(BoardManager))
    {
        StatusMessage = TEXT("Setup: no active encounter setup to complete.");
        return false;
    }
    int32 Players = 0, Enemies = 0, DeployedPlayers = 0;
    TSet<ABoardTile*> OccupiedTiles;
    for (ABattleUnitBase* Unit : BattleUnits)
    {
        if (!IsValid(Unit) || !Unit->IsAlive() || Unit->bInCombat
            || (Unit->TeamType != EBattleTeam::Player && Unit->TeamType != EBattleTeam::Enemy))
        {
            StatusMessage = TEXT("Setup: every registered unit must be living, inactive and assigned to a valid team.");
            return false;
        }
        if (Unit->TeamType == EBattleTeam::Player) ++Players; else ++Enemies;
        if (Unit->TeamType == EBattleTeam::Enemy && !Unit->bIsDeployed)
        {
            StatusMessage = TEXT("Setup: place every enemy on an enemy tile before completing setup.");
            return false;
        }
        if (Unit->bIsDeployed)
        {
            ABoardTile* Tile = Unit->CurrentTile;
            const EBoardTileTeam Required = Unit->TeamType == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
            if (!IsValid(Tile) || !BoardManager->Tiles.Contains(Tile) || Tile->TeamType != Required
                || Tile->OccupiedUnit != Unit || OccupiedTiles.Contains(Tile))
            {
                StatusMessage = TEXT("Setup: a deployed unit has an invalid, shared or wrong-team tile.");
                return false;
            }
            OccupiedTiles.Add(Tile);
            if (Unit->TeamType == EBattleTeam::Player) ++DeployedPlayers;
        }
        else if (IsValid(Unit->CurrentTile))
        {
            StatusMessage = TEXT("Setup: a waiting unit must not occupy a tile.");
            return false;
        }
    }
    if (Players == 0 || Enemies == 0 || DeployedPlayers > MaxBoardUnits)
    {
        StatusMessage = TEXT("Setup: register both teams and respect the player deployment limit.");
        return false;
    }
    bEncounterSetupOpen = false;
    bEncounterReady = true;
    StatusMessage = TEXT("Units ready. Start preparation to enable deployment.");
    return true;
}

void AAutoBattleGameMode::CancelEncounterSetup()
{
    if (CurrentPhase != EGamePhase::Preparation) return;
    if (bEnableMainMapRoundTest) return;
    ClearEncounter();
    RemainingPreparationTime = 0;
    StatusMessage = TEXT("Setup cancelled. Registered units were removed.");
}
void AAutoBattleGameMode::StartPreparationPhase()
{
    if (!bEncounterReady || CurrentPhase != EGamePhase::Preparation) return;
    const TArray<ABattleUnitBase*> Units = BattleUnits;
    for (ABattleUnitBase* Unit : Units)
        if (IsValid(Unit)) Unit->PrepareSkillsForRound();
    RestartPreparationCountdown();
    StatusMessage = TEXT("Select an ally, then click an empty player tile. Enter: start. Right click: cancel.");
    if (bEnableMainMapRoundTest && (bUseRoundEnemyFormations || !RoundsTable.IsNull()) && !bManagedBridgeReady)
        bPreparationActive = false;
    ChangePhase(EGamePhase::Preparation);
    if (bEnableMainMapRoundTest && !bManagedBridgeReady) InitializeManagedRoundBridge();
}
void AAutoBattleGameMode::RestartPreparationCountdown()
{
    if (!bEncounterReady || CurrentPhase != EGamePhase::Preparation) return;
    RemainingPreparationTime = bEnableMainMapRoundTest ? 0.f : FMath::Max(0.0f, PreparationDuration);
    bPreparationActive = true;
    bPreparationExpiryReported = false;
}
void AAutoBattleGameMode::RequestCombatStart()
{
    if (bEnableMainMapRoundTest)
    {
        if (bManagedBridgeReady && bEncounterReady && !bManagedTestFinished)
        { RefreshManagedInteraction(true); CallManaged(TEXT("RequestCombatStart")); }
        return;
    }
    if (!bEncounterReady) return;
    if (CurrentPhase == EGamePhase::Preparation && !bPreparationActive) return;
    if (CurrentPhase != EGamePhase::Preparation && CurrentPhase != EGamePhase::RoundClear) return;
    ReceiveCombatStartRequested();
}
void AAutoBattleGameMode::RequestBattleRestart()
{
    if (bEnableMainMapRoundTest)
    {
        if ((bManagedDefeated || bManagedTestFinished) && !bShowingRoundClear)
            UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
        return;
    }
    ReceiveRestartRequested();
}
void AAutoBattleGameMode::ReportBattleResult(EGamePhase Result)
{
    if (CurrentPhase != EGamePhase::Combat || bBattleResultPending) return;
    bBattleResultPending = true;
    PendingBattleResult = Result;
    // BP에 결과를 알리기 전에 공격을 멈춘다. 결과 화면이 늦게 표시되어도 전투가 계속되지 않게 한다.
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StopCombat();
    if (bEnableMainMapRoundTest && bManagedBridgeReady)
    {
        ManagedLastOutcome = Result == EGamePhase::GameOver ? TEXT("Defeat") : TEXT("Victory");
        CallManaged(TEXT("NotifyCombatFinished"), [this](UFunction* F,uint8* P) {
            auto Prop=FindFProperty<FStructProperty>(F,TEXT("CompletedEncounterId"));
            if(Prop) *Prop->ContainerPtrToValuePtr<FGuid>(P)=ManagedEncounterId;
        }); return;
    }
    ReceiveBattleResultDetected(Result);
}
void AAutoBattleGameMode::StartCombatPhase()
{
    if (bEnableMainMapRoundTest && !bManagedStartAuthorized) { RequestCombatStart(); return; }
    if (!bEncounterReady || CurrentPhase != EGamePhase::Preparation) return;
    if (GetDeployedPlayerCount() == 0)
    {
        StatusMessage = TEXT("Place at least one player unit before starting.");
        RestartPreparationCountdown();
        return;
    }
    bBattleResultPending = false;
    ElapsedCombatTime = 0;
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StartCombat();
    StatusMessage = TEXT("Combat: deployment is locked.");
    ChangePhase(EGamePhase::Combat);
}
int32 AAutoBattleGameMode::GetDeployedPlayerCount() const
{
    int32 Count = 0;
    for (ABattleUnitBase* Unit : BattleUnits)
        if (IsValid(Unit) && Unit->TeamType == EBattleTeam::Player && Unit->IsAlive() && Unit->bIsDeployed) ++Count;
    return Count;
}
int32 AAutoBattleGameMode::GetAliveTeamCount(EBattleTeam Team) const
{
    int32 Count = 0;
    for (ABattleUnitBase* Unit : BattleUnits)
        if (IsValid(Unit) && Unit->TeamType == Team && Unit->IsAlive() && Unit->bIsDeployed) ++Count;
    return Count;
}
void AAutoBattleGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bEncounterReady) return;
    if (bEnableMainMapRoundTest && bManagedBridgeReady) RefreshManagedInteraction();
    if (!bEnableMainMapRoundTest && CurrentPhase == EGamePhase::Preparation && bPreparationActive && !bPreparationExpiryReported && PreparationDuration > 0)
    {
        RemainingPreparationTime = FMath::Max(0.0f, RemainingPreparationTime - DeltaSeconds);
        if (RemainingPreparationTime <= 0)
        {
            // 준비 시간이 끝날 때 이벤트를 한 번만 보낸다. BP에서 다음 단계 전환을 지연할 수도 있다.
            // 배치는 계속 허용한다. 출전 유닛이 없을 때의 재시도는 StartCombatPhase에서 처리한다.
            bPreparationExpiryReported = true;
            ReceivePreparationExpired();
        }
    }
    else if (CurrentPhase == EGamePhase::Combat && !bBattleResultPending)
    {
        ElapsedCombatTime += DeltaSeconds;
        // 전투 중 생존자를 집계한다. 양쪽 모두 전멸했으면 패배로 처리한다.
        if (GetAliveTeamCount(EBattleTeam::Player) == 0)
        {
            StatusMessage = TEXT("Defeat. R: restart.");
            ReportBattleResult(EGamePhase::GameOver);
        }
        else if (GetAliveTeamCount(EBattleTeam::Enemy) == 0)
        {
            StatusMessage = TEXT("Round clear.");
            ReportBattleResult(CurrentRound >= FMath::Max(1, MaxRound) ? EGamePhase::Victory : EGamePhase::RoundClear);
        }
        else if (!bEnableMainMapRoundTest && ElapsedCombatTime >= FMath::Max(1.0f, CombatTimeLimit))
        {
            StatusMessage = TEXT("Defeat: combat time limit reached. R: restart.");
            ReportBattleResult(EGamePhase::GameOver);
        }
    }
}
void AAutoBattleGameMode::HandleRoundClear()
{
    if (bEnableMainMapRoundTest) return;
    if (CurrentPhase != EGamePhase::Combat || (bBattleResultPending && PendingBattleResult == EGamePhase::GameOver)) return;
    if (CurrentRound >= FMath::Max(1, MaxRound)) { HandleVictory(); return; }
    StatusMessage = TEXT("Round clear! Enter: next round. R: restart.");
    ChangePhase(EGamePhase::RoundClear);
}
void AAutoBattleGameMode::HandleGameOver()
{
    if (bEnableMainMapRoundTest) return;
    if (CurrentPhase != EGamePhase::Combat || (bBattleResultPending && PendingBattleResult != EGamePhase::GameOver)) return;
    if (!StatusMessage.StartsWith(TEXT("Defeat:"))) StatusMessage = TEXT("Defeat. R: restart.");
    ChangePhase(EGamePhase::GameOver);
}
void AAutoBattleGameMode::HandleVictory()
{
    if (bEnableMainMapRoundTest) return;
    if (CurrentPhase != EGamePhase::Combat && CurrentPhase != EGamePhase::RoundClear) return;
    if (bBattleResultPending && PendingBattleResult == EGamePhase::GameOver) return;
    StatusMessage = TEXT("Victory! R: restart.");
    ChangePhase(EGamePhase::Victory);
}
void AAutoBattleGameMode::AdvanceRound()
{
    if (bEnableMainMapRoundTest) return;
    if (CurrentPhase != EGamePhase::RoundClear || CurrentRound >= MaxRound) return;
    ++CurrentRound;
    InitializeEncounter();
}
void AAutoBattleGameMode::RestartBattle()
{
    if (bEnableMainMapRoundTest) return;
    CurrentRound = 1;
    InitializeEncounter();
}

namespace ManagedRoundBridge
{
    void Guid(UFunction* F, uint8* P, FName Name, FGuid Value)
    { *FindFProperty<FStructProperty>(F, Name)->ContainerPtrToValuePtr<FGuid>(P) = Value; }
    void Bool(UFunction* F, uint8* P, FName Name, bool Value)
    { FindFProperty<FBoolProperty>(F, Name)->SetPropertyValue_InContainer(P, Value); }
    void Int(UFunction* F, uint8* P, FName Name, int32 Value)
    { FindFProperty<FIntProperty>(F, Name)->SetPropertyValue_InContainer(P, Value); }
    bool UMGDragging()
    {
        // UMG already belongs to the existing widget; no new build dependency.
        UClass* Library = FindObject<UClass>(nullptr, TEXT("/Script/UMG.WidgetBlueprintLibrary"));
        UFunction* Fn = Library ? Library->FindFunctionByName(TEXT("IsDragDropping")) : nullptr;
        if (!Fn) return false;
        FStructOnScope Params(Fn);
        Library->GetDefaultObject()->ProcessEvent(Fn, Params.GetStructMemory());
        const FBoolProperty* Result = FindFProperty<FBoolProperty>(Fn, TEXT("ReturnValue"));
        return Result && Result->GetPropertyValue_InContainer(Params.GetStructMemory());
    }
}

void AAutoBattleGameMode::ManagedError(const FString& Message)
{
    StatusMessage = TEXT("Round integration error: ") + Message;
    bManagedBridgeReady = false;
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StopCombat();
    UE_LOG(LogTemp, Error, TEXT("MANAGED_MAINMAP_ERROR: %s"), *Message);
}

bool AAutoBattleGameMode::CallManaged(FName Name, TFunctionRef<void(UFunction*, uint8*)> Fill)
{
    UFunction* Fn = IsValid(ManagedRoundManager) ? ManagedRoundManager->FindFunction(Name) : nullptr;
    if (!Fn) { ManagedError(TEXT("Missing RoundManager function: ") + Name.ToString()); return false; }
    FStructOnScope Params(Fn);
    Fill(Fn, Params.GetStructMemory());
    ManagedRoundManager->ProcessEvent(Fn, Params.GetStructMemory());
    return true;
}
bool AAutoBattleGameMode::CallManaged(FName Name)
{ return CallManaged(Name, [](UFunction*, uint8*) {}); }

bool AAutoBattleGameMode::BindManagedEvent(FName EventName, FName HandlerName)
{
    const FMulticastDelegateProperty* Property = FindFProperty<FMulticastDelegateProperty>(ManagedRoundManager->GetClass(), EventName);
    if (!Property || !FindFunction(HandlerName)) return false;
    FScriptDelegate Delegate; Delegate.BindUFunction(this, HandlerName);
    Property->AddDelegate(Delegate, ManagedRoundManager);
    return true;
}

int32 AAutoBattleGameMode::GetManagedRoundPhase() const
{
    if (!IsValid(ManagedRoundManager)) return -1;
    const FByteProperty* Phase = FindFProperty<FByteProperty>(ManagedRoundManager->GetClass(), TEXT("CurrentPhase"));
    return Phase ? Phase->GetPropertyValue_InContainer(ManagedRoundManager) : -1;
}

bool AAutoBattleGameMode::InitializeManagedRoundBridge()
{
    if (!bEnableMainMapRoundTest || !bEncounterReady) return false;
    if (bManagedBridgeReady) return true;
    const FObjectPropertyBase* Ref = FindFProperty<FObjectPropertyBase>(GetClass(), TEXT("RoundManagerReference"));
    if (!RoundsTable.IsNull())
    {
        auto* Manager = GetWorld()->SpawnActor<AUnifiedRoundManager>();
        if (!Manager) return false;
        Manager->RoundsTable = RoundsTable.LoadSynchronous();
        ManagedRoundManager = Manager;
    }
    else
        ManagedRoundManager = Ref ? Cast<AActor>(Ref->GetObjectPropertyValue_InContainer(this)) : nullptr;
    if (!IsValid(ManagedRoundManager))
    {
        auto Class=LoadClass<AActor>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager_C"));
        if(!Class)return false;
        for(TActorIterator<AActor> It(GetWorld(),Class);It;++It){if(ManagedRoundManager){ManagedError(TEXT("Duplicate RoundManagers"));return false;}ManagedRoundManager=*It;}
        if(!ManagedRoundManager)ManagedRoundManager=GetWorld()->SpawnActor<AActor>(Class);
        if (Ref && IsValid(ManagedRoundManager) && ManagedRoundManager->IsA(Ref->PropertyClass))
            Ref->SetObjectPropertyValue_InContainer(this, ManagedRoundManager);
    }
    if(!ParticipantRecords()){ManagedError(TEXT("RoundManager participant array missing"));return false;}
    for (FName Name : {FName(TEXT("ConfigureIntegration")), FName(TEXT("InitializeFlow")), FName(TEXT("ConfigureTemporaryRewardBypass")), FName(TEXT("UpdateInteractionState")), FName(TEXT("NotifyDeploymentLimitApplied")), FName(TEXT("NotifyRoundPreparationCompleted")), FName(TEXT("RequestCombatStart")), FName(TEXT("NotifyCombatFinished")), FName(TEXT("NotifyCombatResolutionCompleted")), FName(TEXT("ConfirmProgressionToReward")), FName(TEXT("ResolveFinalRoundPolicy")), FName(TEXT("NotifyRewardSelectionCompleted"))})
        if (!ManagedRoundManager->FindFunction(Name)) { ManagedError(TEXT("Required Blueprint API missing: ") + Name.ToString()); return false; }
    const TPair<FName, FName> Bindings[] = {
        {TEXT("OnDeploymentLimitChangeRequested"), TEXT("ManagedDeploymentRequested")},
        {TEXT("OnRoundPreparationRequested"), TEXT("ManagedPreparationRequested")},
        {TEXT("OnCombatStartRequested"), TEXT("ManagedCombatStartRequested")},
        {TEXT("OnCombatStopRequested"), TEXT("ManagedCombatStopRequested")},
        {TEXT("OnCombatResolutionRequired"), TEXT("ManagedResolutionRequested")},
        {TEXT("OnFinalRoundPolicyRequired"), TEXT("ManagedFinalPolicyRequested")},
        {TEXT("OnRewardRequested"), TEXT("ManagedRewardRequested")},
        {TEXT("OnPhaseChanged"), TEXT("ManagedPhaseChanged")},
        {TEXT("OnStartAvailabilityChanged"), TEXT("ManagedAvailabilityChanged")},
        {TEXT("OnConfigurationError"), TEXT("ManagedConfigurationError")}
    };
    for (const auto& Binding : Bindings) if (!BindManagedEvent(Binding.Key, Binding.Value)) { ManagedError(TEXT("Unable to bind ") + Binding.Key.ToString()); return false; }
    if (!InitializeMainMapShop()) { ManagedError(TEXT("MainMap shop setup failed")); return false; }
    bManagedBridgeReady = true;
    MaxRound = 5;
    CallManaged(TEXT("ConfigureTemporaryRewardBypass"), [](UFunction* F, uint8* P) { ManagedRoundBridge::Bool(F, P, TEXT("bEnabled"), false); });
    CallManaged(TEXT("ConfigureIntegration"), [](UFunction* F, uint8* P) {
        for (FName Name : {FName(TEXT("TimerOwnershipConfirmed")), FName(TEXT("PhaseSyncConfirmed")), FName(TEXT("DeploymentAdapterReady")), FName(TEXT("CombatAdapterReady")), FName(TEXT("PreparationAdapterReady")), FName(TEXT("RewardAdapterReady"))}) ManagedRoundBridge::Bool(F, P, Name, true);
    });
    RefreshManagedInteraction(true);
    CallManaged(TEXT("InitializeFlow"));
    UE_LOG(LogTemp, Display, TEXT("MANAGED_MAINMAP_READY: units=%d timerOwner=RoundManager reward=shop"), BattleUnits.Num());
    return bManagedBridgeReady;
}

void AAutoBattleGameMode::SetManagedDragState(bool Unit, bool Item)
{
    bManagedUnitDragging = Unit; bManagedItemDragging = Item;
    if (bEnableMainMapRoundTest && bManagedBridgeReady) RefreshManagedInteraction(true);
}
void AAutoBattleGameMode::RefreshManagedInteraction(bool bForce)
{
    if (!bManagedBridgeReady || (!bForce && GetManagedRoundPhase() != 1)) return;
    const int32 Count = GetDeployedPlayerCount();
    auto PC=Cast<AAutoBattlePlayerController>(UGameplayStatics::GetPlayerController(this,0));
    const bool Unit = bManagedUnitDragging || (PC && PC->IsUnitDragActive()) || ManagedRoundBridge::UMGDragging();
    const bool Item = bManagedItemDragging;
    if (!bForce && Count == LastManagedDeployedCount && Unit == bLastManagedUnitDragging && Item == bLastManagedItemDragging) return;
    LastManagedDeployedCount = Count; bLastManagedUnitDragging = Unit; bLastManagedItemDragging = Item;
    CallManaged(TEXT("UpdateInteractionState"), [Count, Unit, Item](UFunction* F, uint8* P) {
        ManagedRoundBridge::Int(F, P, TEXT("AllyCount"), Count);
        ManagedRoundBridge::Bool(F, P, TEXT("UnitDragging"), Unit);
        ManagedRoundBridge::Bool(F, P, TEXT("ItemDragging"), Item);
    });
}

void AAutoBattleGameMode::ManagedDeploymentRequested(int32 Limit, int32 LimitRoundNumber, FGuid RequestId)
{
    const auto* Row = GetUnifiedRound(LimitRoundNumber);
    const int32 ExpectedLimit = Row ? Row->MaxDeployedUnits : LimitRoundNumber + 1;
    const bool Valid = ManagedRequestMatches(TEXT("bAwaitingDeployment"),TEXT("DeploymentRequestId"),RequestId) && LimitRoundNumber >= 1 && LimitRoundNumber <= 5 && Limit == ExpectedLimit;
    if (Valid) MaxBoardUnits = Limit;
    UE_LOG(LogTemp, Display, TEXT("MANAGED_LIMIT: round=%d limit=%d applied=%d"), LimitRoundNumber, Limit, Valid);
    CallManaged(TEXT("NotifyDeploymentLimitApplied"), [RequestId, Valid](UFunction* F, uint8* P) {
        ManagedRoundBridge::Guid(F, P, TEXT("RequestId"), RequestId); ManagedRoundBridge::Bool(F, P, TEXT("bSuccess"), Valid);
    });
}


TArray<FIntegrationRoundParticipant>* AAutoBattleGameMode::ParticipantRecords() const
{
    if (!IsValid(ManagedRoundManager)) return nullptr;
    auto P=FindFProperty<FArrayProperty>(ManagedRoundManager->GetClass(),TEXT("RoundParticipantSnapshots"));
    auto Inner=P?CastField<FStructProperty>(P->Inner):nullptr;
    return Inner && Inner->Struct==FIntegrationRoundParticipant::StaticStruct()
        ? P->ContainerPtrToValuePtr<TArray<FIntegrationRoundParticipant>>(ManagedRoundManager) : nullptr;
}
bool AAutoBattleGameMode::ManagedRequestMatches(FName Awaiting,FName IdProperty,FGuid Id) const
{
    if(!bManagedBridgeReady || !IsValid(ManagedRoundManager) || !Id.IsValid())return false;
    auto Flag=FindFProperty<FBoolProperty>(ManagedRoundManager->GetClass(),Awaiting);
    auto Key=FindFProperty<FStructProperty>(ManagedRoundManager->GetClass(),IdProperty);
    return Flag && Key && Flag->GetPropertyValue_InContainer(ManagedRoundManager)
        && *Key->ContainerPtrToValuePtr<FGuid>(ManagedRoundManager)==Id;
}
bool AAutoBattleGameMode::CaptureManagedUnits()
{
    auto Records=ParticipantRecords(); if(!Records || !IsValid(BoardManager))return false;
    const auto Participants=BoardManager->GetUnitsOnBoard();
    int32 Allies=0,Enemies=0; TArray<FIntegrationRoundParticipant> Next;
    for(auto Unit:Participants)
    {
        if(!IsValid(Unit)||!BattleUnits.Contains(Unit)||!Unit->IsAlive()||Unit->bInCombat||!Unit->bIsDeployed
            ||!IsValid(Unit->CurrentTile)||Unit->CurrentTile->OccupiedUnit!=Unit)return false;
        if(Unit->TeamType==EBattleTeam::Player){++Allies;FString Reason;if(!BoardManager->ValidateUnitPlacementState(Unit,Reason))return false;}else ++Enemies;
        FIntegrationRoundParticipant S;S.Unit=Unit;S.Tile=Unit->CurrentTile;S.Transform=Unit->GetActorTransform();
        S.HP=Unit->CurrentHP;S.Mana=Unit->CurrentMana;S.Team=Unit->TeamType;S.EncounterId=ManagedEncounterId;Next.Add(S);
    }
    if(Allies<1||Allies>MaxBoardUnits||Enemies<1||!BoardManager->SavePlayerFormation())return false;
    *Records=MoveTemp(Next); bManagedParticipantsRestored=false;
    for(auto Unit:Participants)Unit->SetCombatRoster(Participants);
    UE_LOG(LogTemp,Display,TEXT("INTEGRATION_CAPTURE: owner=RoundManager participants=%d benchExcluded=1"),Records->Num());return true;
}
bool AAutoBattleGameMode::RestoreManagedUnits()
{
    if(bManagedParticipantsRestored)return true;
    auto Records=ParticipantRecords();if(!Records||Records->IsEmpty()||!IsValid(BoardManager))return false;
    TSet<ABoardTile*> Tiles;TSet<ABattleUnitBase*> Units;
    for(const auto& S:*Records)
    {
        if(!IsValid(S.Unit)||!BattleUnits.Contains(S.Unit)||!IsValid(S.Tile)||!BoardManager->Tiles.Contains(S.Tile)
            ||S.Team!=S.Unit->TeamType||S.EncounterId!=ManagedEncounterId||Tiles.Contains(S.Tile)||Units.Contains(S.Unit))return false;
        Tiles.Add(S.Tile);Units.Add(S.Unit);
    }
    for(auto Tile:Tiles)if(IsValid(Tile->OccupiedUnit)&&!Units.Contains(Cast<ABattleUnitBase>(Tile->OccupiedUnit)))return false;
    // The existing result-phase BoardManager callback restores surviving allies once.
    if(BoardManager->HasSavedPlayerFormation()||!BoardManager->LastFormationFailureReason.IsEmpty())return false;
    for(const auto& S:*Records)S.Unit->StopCombat();
    for(const auto& S:*Records)BoardManager->ReleaseUnitTiles(S.Unit);
    for(const auto& S:*Records){ if(!S.Unit->ResetForRound())return false; /* Board restoration may refresh the unit preparation rotation; the manager keeps the original encounter transform. */ if(!S.Unit->SetActorTransform(S.Transform,false,nullptr,ETeleportType::TeleportPhysics))return false; }
    for(const auto& S:*Records)
    {
        if(!S.Unit->IsAlive()||S.Unit->bInCombat||S.Unit->CurrentTile!=S.Tile||S.Tile->OccupiedUnit!=S.Unit
            ||!S.Tile->HasConsistentOccupancy()||!S.Unit->GetActorTransform().Equals(S.Transform,0.1f))return false;
    }
    FString Reason;if(!BoardManager->TrySynchronizeBenchOccupancy(Reason))return false;
    bManagedParticipantsRestored=true;
    UE_LOG(LogTemp,Display,TEXT("INTEGRATION_RESTORE: ResetForRound=%d sameActors=1 reciprocalTiles=1"),Records->Num());return true;
}
void AAutoBattleGameMode::ManagedPreparationRequested(int32 Round, FGuid RequestId)
{
    const auto* Row = GetUnifiedRound(Round);
    const int32 ExpectedLimit = Row ? Row->MaxDeployedUnits : Round + 1;
    bool Valid = ManagedRequestMatches(TEXT("bAwaitingPreparation"),TEXT("PreparationRequestId"),RequestId) && Round >= 1 && Round <= 5 && (Round == 1 || Round == CurrentRound + 1) && MaxBoardUnits == ExpectedLimit;
    if (Valid && Round > 1) Valid = RestoreManagedUnits();
    if (Valid && (bUseRoundEnemyFormations || !RoundsTable.IsNull()))
    {
        FString Error;
        Valid = PrepareManagedEnemies(Round, Error);
        if (!Valid) ManagedError(FString::Printf(TEXT("Round %d enemy formation: %s"), Round, *Error));
    }
    if (Valid)
    {
        CurrentRound = Round; ElapsedCombatTime = 0; RemainingPreparationTime = 0;
        bBattleResultPending = false; bPreparationActive = true; bPreparationExpiryReported = false; bEncounterReady = true;
        ChangePhase(EGamePhase::Preparation); LastManagedDeployedCount = -1; RefreshManagedInteraction(true);
        Valid = IsValid(MainMapShop) && MainMapShop->EnterPreparation();
        if (!Valid) ManagedError(TEXT("Shop could not enter preparation"));
    }
    UE_LOG(LogTemp, Display, TEXT("MANAGED_PREPARATION: round=%d ready=%d units=%d"), Round, Valid, BattleUnits.Num());
    CallManaged(TEXT("NotifyRoundPreparationCompleted"), [RequestId, Valid](UFunction* F, uint8* P) {
        ManagedRoundBridge::Guid(F, P, TEXT("RequestId"), RequestId); ManagedRoundBridge::Bool(F, P, TEXT("bSuccess"), Valid);
    });
}
void AAutoBattleGameMode::ManagedCombatStartRequested(FGuid Encounter, int32 Round)
{
    ManagedEncounterId=Encounter;
    auto PC=Cast<AAutoBattlePlayerController>(UGameplayStatics::GetPlayerController(this,0));
    if (!bManagedBridgeReady || (PC && PC->IsUnitDragActive()) || Round != CurrentRound || GetManagedRoundPhase() != 2 || CurrentPhase != EGamePhase::Preparation || GetDeployedPlayerCount() < 1 || GetDeployedPlayerCount() > MaxBoardUnits || bManagedUnitDragging || bManagedItemDragging || ManagedRoundBridge::UMGDragging() || !CaptureManagedUnits())
    { ManagedError(TEXT("Actual combat prerequisites changed during the start request.")); return; }
    ManagedEncounterId = Encounter; ManagedLastOutcome.Empty(); bManagedStartAuthorized = true;
    StartCombatPhase(); bManagedStartAuthorized = false;
    UE_LOG(LogTemp, Display, TEXT("MANAGED_COMBAT: round=%d encounter=%s units=%d actualAI=1"), Round, *Encounter.ToString(), BattleUnits.Num());
}
void AAutoBattleGameMode::ManagedCombatStopRequested(FGuid Encounter, bool TimedOut)
{
    if (!bManagedBridgeReady || Encounter != ManagedEncounterId) return;
    bBattleResultPending = true;
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StopCombat();
    if (TimedOut) ManagedLastOutcome = TEXT("TimedOut");
}
void AAutoBattleGameMode::ManagedResolutionRequested(FGuid Encounter, bool TimedOut)
{
    if (!bManagedBridgeReady || Encounter != ManagedEncounterId || GetManagedRoundPhase() != 3
        || !bBattleResultPending || bShowingRoundClear || bManagedDefeated) return;
    // 패배와 시간 초과는 보상 진행을 중단하고 같은 길이의 결과 연출을 보여준다.
    bManagedDefeated = TimedOut || ManagedLastOutcome != TEXT("Victory");
    if (RoundClearDisplaySeconds <= 0.f)
    {
        FinishManagedCombatResolution(Encounter, TimedOut);
        return;
    }

    // RoundClear로 바뀌면 보드가 배치를 복원하므로, 연출 동안은 전투 종료 대기 상태를 유지한다.
    bShowingRoundClear = true;
    if (AAutoBattlePlayerController* PC = Cast<AAutoBattlePlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
        PC->ClearSelection();
    SetBattleHUDHiddenForResult(true);
    RefreshRoundDisplay();

    const TArray<FIntegrationRoundParticipant>* Records = ParticipantRecords();
    if (!bManagedDefeated && Records)
    {
        for (const FIntegrationRoundParticipant& Record : *Records)
        {
            ABattleUnitBase* Unit = Record.Unit;
            if (Record.EncounterId == Encounter && Record.Team == EBattleTeam::Player && IsValid(Unit) && Unit->IsAlive())
                Unit->PlayVictoryAnimation();
        }
    }
    UE_LOG(LogTemp, Display, TEXT("ROUND_CLEAR_PRESENTATION: round=%d duration=%.2f"), CurrentRound, RoundClearDisplaySeconds);
    GetWorldTimerManager().SetTimer(RoundClearTimer,
        FTimerDelegate::CreateUObject(this, &AAutoBattleGameMode::FinishManagedCombatResolution, Encounter, TimedOut),
        RoundClearDisplaySeconds, false);
}

void AAutoBattleGameMode::FinishManagedCombatResolution(FGuid Encounter, bool TimedOut)
{
    if (!bManagedBridgeReady || Encounter != ManagedEncounterId || GetManagedRoundPhase() != 3 || !bBattleResultPending) return;

    bShowingRoundClear = false;
    if (bManagedDefeated)
    {
        GetWorldTimerManager().ClearAllTimersForObject(ManagedRoundManager);
        bBattleResultPending = false;
        bPreparationActive = false;
        StatusMessage = TEXT("Defeat. Restart to return to round one.");
        // 전장 모습을 유지한 채 종료한다. 보상/다음 라운드 완료 응답은 보내지 않는다.
        CurrentPhase = EGamePhase::GameOver;
        RefreshRoundDisplay();
        UE_LOG(LogTemp, Display, TEXT("MANAGED_DEFEAT: round=%d progressionStopped=1 restartReady=1"), CurrentRound);
        return;
    }
    if (RoundClearBanner.IsValid()) RoundClearBanner->SetVisibility(EVisibility::Collapsed);
    ChangePhase(EGamePhase::RoundClear);
    if (BoardManager->HasSavedPlayerFormation() || !BoardManager->LastFormationFailureReason.IsEmpty())
    {
        ManagedError(TEXT("Existing formation restoration failed"));
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("MANAGED_SETTLEMENT: round=%d outcome=%s timedOut=%d testAdvanceRegardless=1"), CurrentRound, *ManagedLastOutcome, TimedOut);
    if (!GrantManagedClearGold()) return;
    CallManaged(TEXT("NotifyCombatResolutionCompleted"), [Encounter](UFunction* F, uint8* P) { ManagedRoundBridge::Guid(F, P, TEXT("CompletedEncounterId"), Encounter); });
    CallManaged(TEXT("ConfirmProgressionToReward"), [Encounter](UFunction* F, uint8* P) { ManagedRoundBridge::Guid(F, P, TEXT("CompletedEncounterId"), Encounter); });
}
void AAutoBattleGameMode::ManagedFinalPolicyRequested(FGuid Encounter)
{
    if (!bManagedBridgeReady || Encounter != ManagedEncounterId || CurrentRound != 5) return;
    if(!RestoreManagedUnits()){ManagedError(TEXT("Final participant reset failed; progression blocked"));return;}
    // 최종 보상을 수령한 뒤 라운드 매니저가 Finished로 넘어간다.
    CallManaged(TEXT("ResolveFinalRoundPolicy"), [Encounter](UFunction* F, uint8* P) {
        ManagedRoundBridge::Guid(F, P, TEXT("CompletedEncounterId"), Encounter); ManagedRoundBridge::Bool(F, P, TEXT("bGrantFinalReward"), true);
    });
}
void AAutoBattleGameMode::ManagedRewardReceived()
{
    const FGuid RequestId = PendingShopRewardId;
    if (GetManagedRoundPhase() != 4 || !ManagedRequestMatches(TEXT("bAwaitingReward"), TEXT("RewardRequestId"), RequestId)) return;
    PendingShopRewardId.Invalidate();
    SetBattleHUDHiddenForResult(false);
    CallManaged(TEXT("NotifyRewardSelectionCompleted"), [RequestId](UFunction* F, uint8* P)
    {
        ManagedRoundBridge::Guid(F, P, TEXT("CompletedRewardRequestId"), RequestId);
    });
}
void AAutoBattleGameMode::ManagedPhaseChanged(uint8 Previous, uint8 Current)
{
    static const TCHAR* Names[] = {TEXT("Initializing"), TEXT("Preparation"), TEXT("Combat"), TEXT("ResolvingCombat"), TEXT("Reward"), TEXT("Finished")};
    UE_LOG(LogTemp, Display, TEXT("MANAGED_PHASE: round=%d %s -> %s outcome=%s"), CurrentRound, Previous < 6 ? Names[Previous] : TEXT("Invalid"), Current < 6 ? Names[Current] : TEXT("Invalid"), *ManagedLastOutcome);
    RefreshRoundDisplay();
    if (Current == 5)
    {
        if(!RestoreManagedUnits()){ManagedError(TEXT("Final reset failed"));return;}
        bManagedTestFinished = true;
        bEncounterReady = false;
        bPreparationActive = false;
        StatusMessage = TEXT("All rounds cleared!");
        ChangePhase(EGamePhase::Victory);
        SetBattleHUDHiddenForResult(true);
        RefreshRoundDisplay();
        UE_LOG(LogTemp, Display, TEXT("MANAGED_FINISHED: rounds=%d sameUnits=%d noR6=1"), CurrentRound, BattleUnits.Num());
    }
}
void AAutoBattleGameMode::ManagedAvailabilityChanged(bool Allowed, FText Message)
{
    if (GetManagedRoundPhase() == 1) StatusMessage = Allowed ? FString::Printf(TEXT("R%d ready. Press the existing START COMBAT button."), CurrentRound) : Message.ToString();
}
void AAutoBattleGameMode::ManagedConfigurationError(FText Message) { ManagedError(Message.ToString()); }
void AAutoBattleGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(RoundClearTimer);
    SetBattleHUDHiddenForResult(false);
    RemoveRoundDisplay();
    if (bEnableMainMapRoundTest && IsValid(ManagedRoundManager))
    {
        GetWorldTimerManager().ClearAllTimersForObject(ManagedRoundManager);
        ManagedRoundManager->Destroy();
    }
    Super::EndPlay(Reason);
}


FText AAutoBattleGameMode::GetRoundDisplayText() const
{
    return RoundDisplayLabel.IsValid() ? RoundDisplayLabel->GetText() : FText::GetEmpty();
}

void AAutoBattleGameMode::RefreshRoundDisplay()
{
    // Presentation only: called by the existing phase dispatcher, never by Tick.
    // The manager commits CurrentRoundNumber before entering Preparation.
    if (!bEnableMainMapRoundTest || !IsValid(ManagedRoundManager) || IsRunningCommandlet()) return;
    const auto RoundProperty = FindFProperty<FIntProperty>(ManagedRoundManager->GetClass(), TEXT("CurrentRoundNumber"));
    const int32 Phase = GetManagedRoundPhase();
    const int32 Round = RoundProperty ? RoundProperty->GetPropertyValue_InContainer(ManagedRoundManager) : 0;
    if (Round < 1 || Round > 5 || Phase < 1 || Phase > 5) return;
    UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!Viewport) return;
    if (RoundDisplayRoot.IsValid() && RoundDisplayViewport.Get() != Viewport) RemoveRoundDisplay();
    if (!RoundDisplayRoot.IsValid())
    {
        RoundDisplayRoot = SNew(SOverlay)
            .Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f, 80.f, 24.f, 0.f))
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.015f, 0.02f, 0.04f, 0.88f))
                .Padding(FMargin(12.f, 6.f))
                [
                    SAssignNew(RoundDisplayLabel, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
                    .ColorAndOpacity(FLinearColor::White)
                    .ShadowOffset(FVector2D(1.f, 1.f))
                    .ShadowColorAndOpacity(FLinearColor::Black)
                ]
            ]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SAssignNew(RoundClearBanner, SBorder)
                .Visibility(EVisibility::Collapsed)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.015f, 0.02f, 0.04f, 0.9f))
                .Padding(FMargin(28.f, 12.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [
                        SAssignNew(RoundClearLabel, STextBlock)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
                        .ColorAndOpacity(FLinearColor(1.f, 0.84f, 0.48f))
                    ]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 16.f, 0.f, 0.f)
                    [
                        SAssignNew(DefeatRestartButton, SButton)
                        .Visibility(EVisibility::Collapsed)
                        .Text(NSLOCTEXT("AutoBattleHUD", "Restart", "다시하기"))
                        .OnClicked_Lambda([WeakThis = TWeakObjectPtr<AAutoBattleGameMode>(this)]()
                        {
                            if (AAutoBattleGameMode* Mode = WeakThis.Get()) Mode->RequestBattleRestart();
                            return FReply::Handled();
                        })
                    ]
                ]
            ];
        RoundDisplayViewport = Viewport;
        Viewport->AddViewportWidgetContent(RoundDisplayRoot.ToSharedRef(), 100);
        UE_LOG(LogTemp, Display, TEXT("ROUND_DISPLAY_CREATED: one hit-test-invisible viewport widget"));
    }
    const FText Text = FText::FromString(FString::Printf(TEXT("Round %d / 5%s"), Round,
        Phase == 5 ? TEXT(" - Finished") : TEXT("")));
    RoundDisplayLabel->SetText(Text);
    if (bManagedTestFinished)
    {
        RoundClearLabel->SetText(NSLOCTEXT("AutoBattleHUD", "AllRoundsCleared", "전체 라운드 클리어!"));
        RoundClearLabel->SetColorAndOpacity(FLinearColor(1.f, 0.84f, 0.48f));
        RoundClearBanner->SetVisibility(EVisibility::SelfHitTestInvisible);
        DefeatRestartButton->SetVisibility(EVisibility::Visible);
    }
    else if (bManagedDefeated)
    {
        RoundClearLabel->SetText(FText::Format(NSLOCTEXT("AutoBattleHUD", "RoundDefeat", "{0}라운드 패배"), Round));
        RoundClearLabel->SetColorAndOpacity(FLinearColor(1.f, 0.35f, 0.3f));
        RoundClearBanner->SetVisibility(EVisibility::SelfHitTestInvisible);
        DefeatRestartButton->SetVisibility(bShowingRoundClear ? EVisibility::Collapsed : EVisibility::Visible);
    }
    else if (bShowingRoundClear)
    {
        RoundClearLabel->SetText(FText::Format(NSLOCTEXT("AutoBattleHUD", "RoundClear", "{0}라운드 클리어!"), Round));
        RoundClearBanner->SetVisibility(EVisibility::HitTestInvisible);
    }
    UE_LOG(LogTemp, Display, TEXT("ROUND_DISPLAY_UPDATED: managerRound=%d phase=%d text=%s"), Round, Phase, *Text.ToString());
}

void AAutoBattleGameMode::RemoveRoundDisplay()
{
    if (RoundDisplayRoot.IsValid())
        if (UGameViewportClient* Viewport = RoundDisplayViewport.Get()) Viewport->RemoveViewportWidgetContent(RoundDisplayRoot.ToSharedRef());
    RoundDisplayLabel.Reset();
    RoundClearLabel.Reset();
    DefeatRestartButton.Reset();
    RoundClearBanner.Reset();
    RoundDisplayRoot.Reset();
    RoundDisplayViewport.Reset();
}
