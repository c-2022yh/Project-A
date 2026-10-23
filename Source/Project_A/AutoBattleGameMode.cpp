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
    // BoardManager BeginPlay must finish before the roster uses generated tiles.
    GetWorldTimerManager().SetTimerForNextTick(this, &AAutoBattleGameMode::InitializeEncounter);
}
void AAutoBattleGameMode::ChangePhase(EGamePhase Phase)
{
    CurrentPhase = Phase;
    if (Phase != EGamePhase::Combat)
        for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StopCombat();
    OnPhaseChanged.Broadcast(Phase);
}
void AAutoBattleGameMode::ClearEncounter()
{
    bEncounterReady = false;
    if (AAutoBattlePlayerController* PC = Cast<AAutoBattlePlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
        PC->ClearSelection();
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->Destroy();
    BattleUnits.Empty();
}
void AAutoBattleGameMode::InitializeEncounter()
{
    ClearEncounter();
    ChangePhase(EGamePhase::Preparation);
    RemainingPreparationTime = FMath::Max(0.0f, PreparationDuration);
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

    // Validate the entire roster before creating any units.
    TSet<ABoardTile*> Reserved;
    int32 Players = 0, Enemies = 0, DeployedPlayers = 0;
    for (const FUnitSpawnInfo& Info : UnitRoster)
    {
        if (Info.Team == EBattleTeam::Player) ++Players; else ++Enemies;
        if (Info.Team == EBattleTeam::Enemy && !Info.bStartOnBoard)
        {
            StatusMessage = TEXT("Setup: every enemy must have Start On Board enabled.");
            return;
        }
        if (!Info.bStartOnBoard) continue;
        ABoardTile* Tile = BoardManager->GetTile(Info.Row, Info.Column);
        const EBoardTileTeam Required = Info.Team == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
        if (!Tile || Tile->TeamType != Required || !Tile->CanPlaceUnit() || Reserved.Contains(Tile))
        {
            StatusMessage = FString::Printf(TEXT("Setup: invalid, occupied or wrong-team tile (%d, %d)."), Info.Row, Info.Column);
            return;
        }
        Reserved.Add(Tile);
        if (Info.Team == EBattleTeam::Player) ++DeployedPlayers;
    }
    if (Players == 0 || Enemies == 0 || DeployedPlayers > MaxBoardUnits)
    {
        StatusMessage = TEXT("Setup: need both teams and a valid player deployment limit.");
        return;
    }
    int32 WaitingCount = 0;
    for (const FUnitSpawnInfo& Info : UnitRoster) if (!Info.bStartOnBoard) ++WaitingCount;
    const FVector NearSurface = PlayerTiles[0]->GetSurfaceLocation();
    const FBoxSphereBounds NearBounds = PlayerTiles[0]->TileMesh->CalcBounds(PlayerTiles[0]->TileMesh->GetComponentTransform());
    const float BenchSpacing = FMath::Max(300.0f, BoardManager->EffectiveHorizontalSpacing * 0.65f);
    const FVector BenchCenter(BoardManager->GetActorLocation().X,
        NearBounds.Origin.Y - NearBounds.BoxExtent.Y - 240.0f, NearSurface.Z);
    int32 WaitingIndex = 0;
    for (const FUnitSpawnInfo& Info : UnitRoster)
    {
        UClass* Class = Info.UnitClass ? Info.UnitClass.Get() : ABattleUnitBase::StaticClass();
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABattleUnitBase* Unit = GetWorld()->SpawnActor<ABattleUnitBase>(Class, BoardManager->GetActorLocation(), FRotator::ZeroRotator, Params);
        if (!Unit)
        {
            ClearEncounter();
            StatusMessage = TEXT("Setup: failed to spawn a roster unit.");
            return;
        }
        BattleUnits.Add(Unit);
        Unit->TeamType = Info.Team;
        if (Info.bStartOnBoard)
        {
            if (!Unit->PlaceOnTile(BoardManager->GetTile(Info.Row, Info.Column)))
            {
                ClearEncounter();
                StatusMessage = TEXT("Setup: could not place a roster unit.");
                return;
            }
        }
        else
        {
            const FVector Position = BenchCenter + WaitingAreaOffset
                + FVector((WaitingIndex++ - (WaitingCount - 1) * 0.5f) * BenchSpacing, 0,
                    Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f);
            Unit->SetActorLocation(Position);
        }
    }
    bEncounterReady = true;
    StatusMessage = TEXT("Select a blue unit, then click an empty blue tile. Enter: start. Right click: cancel.");
}
void AAutoBattleGameMode::StartPreparationPhase()
{
    if (CurrentPhase == EGamePhase::Combat) return;
    RestartBattle();
}
void AAutoBattleGameMode::StartCombatPhase()
{
    if (!bEncounterReady || CurrentPhase != EGamePhase::Preparation) return;
    if (GetDeployedPlayerCount() == 0)
    {
        StatusMessage = TEXT("Place at least one player unit before starting.");
        return;
    }
    ElapsedCombatTime = 0;
    for (ABattleUnitBase* Unit : BattleUnits) if (IsValid(Unit)) Unit->StartCombat();
    ChangePhase(EGamePhase::Combat);
    StatusMessage = TEXT("Combat: deployment is locked.");
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
    if (CurrentPhase == EGamePhase::Preparation && PreparationDuration > 0)
    {
        RemainingPreparationTime = FMath::Max(0.0f, RemainingPreparationTime - DeltaSeconds);
        if (RemainingPreparationTime <= 0)
        {
            StartCombatPhase();
            if (CurrentPhase == EGamePhase::Preparation) RemainingPreparationTime = PreparationDuration;
        }
    }
    else if (CurrentPhase == EGamePhase::Combat)
    {
        ElapsedCombatTime += DeltaSeconds;
        // Evaluate after attacks through the game loop; both teams dead counts as defeat.
        if (GetAliveTeamCount(EBattleTeam::Player) == 0) HandleGameOver();
        else if (GetAliveTeamCount(EBattleTeam::Enemy) == 0) HandleRoundClear();
        else if (ElapsedCombatTime >= FMath::Max(1.0f, CombatTimeLimit))
        {
            HandleGameOver();
            StatusMessage = TEXT("Defeat: combat time limit reached. R: restart.");
        }
    }
}
void AAutoBattleGameMode::HandleRoundClear()
{
    if (CurrentPhase != EGamePhase::Combat) return;
    if (CurrentRound >= FMath::Max(1, MaxRound)) { HandleVictory(); return; }
    ChangePhase(EGamePhase::RoundClear);
    StatusMessage = TEXT("Round clear! Enter: next round. R: restart.");
}
void AAutoBattleGameMode::HandleGameOver()
{
    if (CurrentPhase != EGamePhase::Combat) return;
    ChangePhase(EGamePhase::GameOver);
    StatusMessage = TEXT("Defeat. R: restart.");
}
void AAutoBattleGameMode::HandleVictory()
{
    if (CurrentPhase != EGamePhase::Combat && CurrentPhase != EGamePhase::RoundClear) return;
    ChangePhase(EGamePhase::Victory);
    StatusMessage = TEXT("Victory! R: restart.");
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
