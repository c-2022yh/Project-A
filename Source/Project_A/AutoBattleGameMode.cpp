#include "AutoBattleGameMode.h"
#include "AutoBattlePlayerController.h"
#include "AutoBattleHUD.h"
#include "BoardManager.h"
#include "BoardTile.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AAutoBattleGameMode::AAutoBattleGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    PlayerControllerClass = AAutoBattlePlayerController::StaticClass();
    HUDClass = AAutoBattleHUD::StaticClass();
    DefaultPawnClass = nullptr;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        FUnitSpawnInfo Player;
        UnitRoster.Add(Player);
        FUnitSpawnInfo Enemy;
        Enemy.Team = EBattleTeam::Enemy;
        Enemy.bStartOnBoard = true;
        Enemy.Row = 4;
        Enemy.Column = Index + 1;
        UnitRoster.Add(Enemy);
    }
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

    StatusMessage = TEXT("Board ready. Spawn units and start preparation in the GameMode graph.");
    ReceiveBoardReady();
}

bool AAutoBattleGameMode::SpawnEncounterUnits()
{
    if (!IsValid(BoardManager) || CurrentPhase != EGamePhase::Preparation || bEncounterReady || bEncounterSetupOpen || !BattleUnits.IsEmpty())
    {
        StatusMessage = TEXT("Setup: spawn units once after Board Ready, before preparation.");
        return false;
    }
    const TArray<ABoardTile*> PlayerTiles = BoardManager->GetPlayerTiles();
    if (PlayerTiles.IsEmpty() || BoardManager->GetEnemyTiles().IsEmpty())
    {
        StatusMessage = TEXT("Setup: both player and enemy tile areas are required.");
        return false;
    }
    // 유닛을 생성하기 전에 전체 생성 목록의 설정이 올바른지 검사한다.
    TSet<ABoardTile*> Reserved;
    int32 Players = 0, Enemies = 0, DeployedPlayers = 0;
    for (const FUnitSpawnInfo& Info : UnitRoster)
    {
        if (Info.Team == EBattleTeam::Player) ++Players; else ++Enemies;
        if (Info.Team == EBattleTeam::Enemy && !Info.bStartOnBoard)
        {
            StatusMessage = TEXT("Setup: every enemy must have Start On Board enabled.");
            return false;
        }
        if (!Info.bStartOnBoard) continue;
        ABoardTile* Tile = BoardManager->GetTile(Info.Row, Info.Column);
        const EBoardTileTeam Required = Info.Team == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
        if (!Tile || Tile->TeamType != Required || !Tile->CanPlaceUnit() || Reserved.Contains(Tile))
        {
            StatusMessage = FString::Printf(TEXT("Setup: invalid, occupied or wrong-team tile (%d, %d)."), Info.Row, Info.Column);
            return false;
        }
        Reserved.Add(Tile);
        if (Info.Team == EBattleTeam::Player) ++DeployedPlayers;
    }
    if (Players == 0 || Enemies == 0 || DeployedPlayers > MaxBoardUnits)
    {
        StatusMessage = TEXT("Setup: need both teams and a valid player deployment limit.");
        return false;
    }
    if (!BeginEncounterSetup()) return false;
    int32 WaitingCount = 0;
    for (const FUnitSpawnInfo& Info : UnitRoster) if (!Info.bStartOnBoard) ++WaitingCount;
    int32 WaitingIndex = 0;
    for (const FUnitSpawnInfo& Info : UnitRoster)
    {
        UClass* Class = Info.UnitClass ? Info.UnitClass.Get() : ABattleUnitBase::StaticClass();
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABattleUnitBase* Unit = GetWorld()->SpawnActor<ABattleUnitBase>(Class, BoardManager->GetActorLocation(), FRotator::ZeroRotator, Params);
        if (!Unit)
        {
            CancelEncounterSetup();
            StatusMessage = TEXT("Setup: failed to spawn a roster unit.");
            return false;
        }
        Unit->TeamType = Info.Team;
        if (!RegisterBattleUnit(Unit))
        {
            Unit->Destroy();
            const FString Error = StatusMessage;
            CancelEncounterSetup();
            StatusMessage = Error;
            return false;
        }
        const bool bPlaced = Info.bStartOnBoard
            ? Unit->PlaceOnTile(BoardManager->GetTile(Info.Row, Info.Column))
            : MoveUnitToWaitingArea(Unit, WaitingIndex++, WaitingCount);
        if (!bPlaced)
        {
            CancelEncounterSetup();
            StatusMessage = TEXT("Setup: could not position a roster unit.");
            return false;
        }
    }
    if (!CompleteEncounterSetup())
    {
        const FString Error = StatusMessage;
        CancelEncounterSetup();
        StatusMessage = Error;
        return false;
    }
    return true;
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
    if (IsValid(Unit->CurrentTile)) Unit->CurrentTile->ClearTile();
    Unit->bIsDeployed = false;
    Unit->StopCombat();
    const FVector NearSurface = PlayerTiles[0]->GetSurfaceLocation();
    const FBoxSphereBounds NearBounds = PlayerTiles[0]->TileMesh->CalcBounds(PlayerTiles[0]->TileMesh->GetComponentTransform());
    const float Spacing = FMath::Max(300.0f, BoardManager->EffectiveHorizontalSpacing * 0.65f);
    const FVector Center(BoardManager->GetActorLocation().X,
        NearBounds.Origin.Y - NearBounds.BoxExtent.Y + 120.0f, NearSurface.Z);
    const FVector Position = Center + WaitingAreaOffset
        + FVector((WaitingIndex - (WaitingCount - 1) * 0.5f) * Spacing, 0,
            Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f);
    Unit->SetActorLocation(Position);
    return true;
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
    ClearEncounter();
    RemainingPreparationTime = 0;
    StatusMessage = TEXT("Setup cancelled. Registered units were removed.");
}
void AAutoBattleGameMode::StartPreparationPhase()
{
    if (!bEncounterReady || CurrentPhase != EGamePhase::Preparation) return;
    RestartPreparationCountdown();
    StatusMessage = TEXT("Select an ally, then click an empty player tile. Enter: start. Right click: cancel.");
    ChangePhase(EGamePhase::Preparation);
}
void AAutoBattleGameMode::RestartPreparationCountdown()
{
    if (!bEncounterReady || CurrentPhase != EGamePhase::Preparation) return;
    RemainingPreparationTime = FMath::Max(0.0f, PreparationDuration);
    bPreparationActive = true;
    bPreparationExpiryReported = false;
}
void AAutoBattleGameMode::RequestCombatStart()
{
    if (!bEncounterReady) return;
    if (CurrentPhase == EGamePhase::Preparation && !bPreparationActive) return;
    if (CurrentPhase != EGamePhase::Preparation && CurrentPhase != EGamePhase::RoundClear) return;
    ReceiveCombatStartRequested();
}
void AAutoBattleGameMode::RequestBattleRestart()
{
    ReceiveRestartRequested();
}
void AAutoBattleGameMode::ReceiveBoardReady_Implementation()
{
    if (SpawnEncounterUnits()) StartPreparationPhase();
}
void AAutoBattleGameMode::ReceivePreparationExpired_Implementation()
{
    RequestCombatStart();
}
void AAutoBattleGameMode::ReceiveCombatStartRequested_Implementation()
{
    if (CurrentPhase == EGamePhase::RoundClear) AdvanceRound();
    else StartCombatPhase();
}
void AAutoBattleGameMode::ReceiveRestartRequested_Implementation()
{
    RestartBattle();
}
void AAutoBattleGameMode::ReceiveBattleResultDetected_Implementation(EGamePhase Result)
{
    switch (Result)
    {
        case EGamePhase::Victory: HandleVictory(); break;
        case EGamePhase::RoundClear: HandleRoundClear(); break;
        case EGamePhase::GameOver: HandleGameOver(); break;
        default: break;
    }
}
void AAutoBattleGameMode::ReportBattleResult(EGamePhase Result)
{
    if (CurrentPhase != EGamePhase::Combat || bBattleResultPending) return;
    bBattleResultPending = true;
    PendingBattleResult = Result;
    // BP에 결과를 알리기 전에 공격을 멈춘다. 결과 화면이 늦게 표시되어도 전투가 계속되지 않게 한다.
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StopCombat();
    ReceiveBattleResultDetected(Result);
}
void AAutoBattleGameMode::StartCombatPhase()
{
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
    if (CurrentPhase == EGamePhase::Preparation && bPreparationActive && !bPreparationExpiryReported && PreparationDuration > 0)
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
        else if (ElapsedCombatTime >= FMath::Max(1.0f, CombatTimeLimit))
        {
            StatusMessage = TEXT("Defeat: combat time limit reached. R: restart.");
            ReportBattleResult(EGamePhase::GameOver);
        }
    }
}
void AAutoBattleGameMode::HandleRoundClear()
{
    if (CurrentPhase != EGamePhase::Combat || (bBattleResultPending && PendingBattleResult == EGamePhase::GameOver)) return;
    if (CurrentRound >= FMath::Max(1, MaxRound)) { HandleVictory(); return; }
    StatusMessage = TEXT("Round clear! Enter: next round. R: restart.");
    ChangePhase(EGamePhase::RoundClear);
}
void AAutoBattleGameMode::HandleGameOver()
{
    if (CurrentPhase != EGamePhase::Combat || (bBattleResultPending && PendingBattleResult != EGamePhase::GameOver)) return;
    if (!StatusMessage.StartsWith(TEXT("Defeat:"))) StatusMessage = TEXT("Defeat. R: restart.");
    ChangePhase(EGamePhase::GameOver);
}
void AAutoBattleGameMode::HandleVictory()
{
    if (CurrentPhase != EGamePhase::Combat && CurrentPhase != EGamePhase::RoundClear) return;
    if (bBattleResultPending && PendingBattleResult == EGamePhase::GameOver) return;
    StatusMessage = TEXT("Victory! R: restart.");
    ChangePhase(EGamePhase::Victory);
}
void AAutoBattleGameMode::AdvanceRound()
{
    if (CurrentPhase != EGamePhase::RoundClear || CurrentRound >= MaxRound) return;
    ++CurrentRound;
    InitializeEncounter();
}
void AAutoBattleGameMode::RestartBattle()
{
    CurrentRound = 1;
    InitializeEncounter();
}
