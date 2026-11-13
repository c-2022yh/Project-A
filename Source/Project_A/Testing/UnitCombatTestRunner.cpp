#include "UnitCombatTestRunner.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnitCombatTest, Log, All);

AUnitCombatTestRunner::AUnitCombatTestRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("TestRoot")));
    UnitClass = TSoftClassPtr<ABattleUnitBase>(FSoftObjectPath(TEXT("/Game/BluePrints/Units/Test/BP_UnitBase.BP_UnitBase_C")));
    UnitDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Data/Units/DT_Units.DT_Units")));
    Enemy.Row = 5; Enemy.Column = 4;
}
void AUnitCombatTestRunner::BeginPlay()
{
    Super::BeginPlay();
    if (bAutoStartOnPlay) StartTest();
}
void AUnitCombatTestRunner::SetStatus(EUnitCombatTestState State, const FString& Message)
{
    TestState = State; Status = Message;
    UE_LOG(LogUnitCombatTest, Display, TEXT("%s: %s"), *GetName(), *Message);
    if (bShowScreenMessages && GEngine)
        GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()), 10.f,
            State == EUnitCombatTestState::Failed ? FColor::Red : FColor::Cyan, Message);
}
bool AUnitCombatTestRunner::ValidateIsolation(FString& Error) const
{
    const UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld()) Error = TEXT("Run this actor with Play in a dedicated test map.");
    else if (AllowedMapName.IsNone() || FName(*UGameplayStatics::GetCurrentLevelName(this, true)) != AllowedMapName)
        Error = TEXT("Map name does not match Allowed Map Name. Use the dedicated TestMap.");
    else if (!World->GetAuthGameMode() || World->GetAuthGameMode()->GetClass() != AGameModeBase::StaticClass())
        Error = TEXT("Set TestMap World Settings > GameMode Override to the native GameModeBase (not BP_AutoBattleGameMode).");
    else
    {
        int32 Count = 0;
        for (TActorIterator<AUnitCombatTestRunner> It(GetWorld()); It; ++It) if (IsValid(*It)) ++Count;
        if (Count != 1) Error = TEXT("Place exactly one UnitCombatTestRunner in the test world.");
    }
    return Error.IsEmpty();
}
bool AUnitCombatTestRunner::ResolveBoard(FString& Error)
{
    if (IsValid(Board))
    {
        if (Board->GetWorld() != GetWorld()) Error = TEXT("Assigned board belongs to another world.");
        return Error.IsEmpty();
    }
    int32 Count = 0;
    for (TActorIterator<ABoardManager> It(GetWorld()); It; ++It)
        if (IsValid(*It)) { Board = *It; ++Count; }
    if (Count != 1)
    {
        Board = nullptr;
        Error = TEXT("Assign Board, or place exactly one BP_BoardManager in TestMap.");
    }
    return Error.IsEmpty();
}
void AUnitCombatTestRunner::StartTest()
{
    if (TestState == EUnitCombatTestState::WaitingForBoard || TestState == EUnitCombatTestState::Preparing || TestState == EUnitCombatTestState::Running) return;
    FString Error;
    if (!ValidateIsolation(Error)) { Fail(Error); return; }
    CleanupOwnedUnits();
    if (!ResolveBoard(Error)) { Fail(Error); return; }
    ElapsedCombatTime = 0.f; bResultQueued = false;
    WaitStartedAt = GetWorld()->GetTimeSeconds();
    SetStatus(EUnitCombatTestState::WaitingForBoard, TEXT("Waiting for board generation..."));
    // BeginPlay 실행 순서에 의존하지 않고 다음 프레임부터 보드 생성 완료를 확인한다.
    GetWorldTimerManager().SetTimer(SetupTimer, this, &AUnitCombatTestRunner::TrySetup, 0.1f, true, 0.01f);
}
void AUnitCombatTestRunner::TrySetup()
{
    if (TestState != EUnitCombatTestState::WaitingForBoard) return;
    FString Error;
    if (!ValidateIsolation(Error)) { Fail(Error); return; }
    if (!IsValid(Board)) { Fail(TEXT("Board was removed while waiting for setup.")); return; }
    if (Board->Tiles.IsEmpty())
    {
        if (GetWorld()->GetTimeSeconds() - WaitStartedAt >= FMath::Max(0.1f, BoardWaitTimeout))
            Fail(TEXT("Board generation timed out. Check BP_BoardManager TileClass and tile mesh/spacing."));
        return;
    }
    GetWorldTimerManager().ClearTimer(SetupTimer);
    ABoardTile* AllyTile = Board->GetTile(Ally.Row, Ally.Column);
    ABoardTile* EnemyTile = Board->GetTile(Enemy.Row, Enemy.Column);
    if (!IsValid(AllyTile) || !IsValid(EnemyTile) || AllyTile == EnemyTile)
    { Fail(TEXT("Choose two different existing board coordinates: Ally and Enemy Row/Column.")); return; }
    if (AllyTile->TeamType != EBoardTileTeam::Player || EnemyTile->TeamType != EBoardTileTeam::Enemy)
    { Fail(TEXT("Ally must start on a player tile and Enemy on an enemy tile.")); return; }
    // 수동 배치한 유닛이나 다른 테스트를 임의로 삭제하거나 조작하지 않는다.
    Board->PruneInvalidReservations();
    for (ABoardTile* Tile : Board->Tiles)
        if (IsValid(Tile) && (IsValid(Tile->OccupiedUnit) || Tile->IsReserved()))
        { Fail(TEXT("Test board must be empty. Remove manually placed units; this runner spawns its own pair.")); return; }
    if (!AllyTile->CanPlaceUnit() || !EnemyTile->CanPlaceUnit())
    { Fail(TEXT("A start tile is blocked or reserved.")); return; }
    UClass* Class = UnitClass.LoadSynchronous();
    UDataTable* SourceData = UnitDataTable.LoadSynchronous();
    if (!Class || !Class->IsChildOf(ABattleUnitBase::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
    { Fail(TEXT("Unit Class must be BP_UnitBase or another concrete BattleUnitBase child.")); return; }
    if (!SourceData || SourceData->GetRowStruct() != FUnitDefinition::StaticStruct())
    { Fail(TEXT("Unit Data Table must use the UnitDefinition row structure.")); return; }
    RuntimeData = NewObject<UDataTable>(this, NAME_None, RF_Transient);
    RuntimeData->RowStruct = FUnitDefinition::StaticStruct();
    for (const FUnitCombatTestSide& Side : {Ally, Enemy})
    {
        const FUnitDefinition* Row = SourceData->FindRow<FUnitDefinition>(Side.RowName, TEXT("UnitCombatTestRunner"), false);
        if (!Row) { Fail(FString::Printf(TEXT("Unit row not found: %s"), *Side.RowName.ToString())); return; }
        FUnitDefinition Copy = *Row;
        if (bUseTimerAttackForTest) Copy.BasicAttack.bUseAnimNotify = false;
        // 원본 DT는 읽기만 한다. 에셋 참조는 유지하고 전투 상태는 각 유닛이 소유한다.
        RuntimeData->AddRow(Side.RowName, Copy);
    }
    SetStatus(EUnitCombatTestState::Preparing, TEXT("Spawning and placing the test pair..."));
    const auto SpawnSide = [&](const FUnitCombatTestSide& Side, EBattleTeam Team, ABoardTile* Tile) -> ABattleUnitBase*
    {
        FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABattleUnitBase* Unit = GetWorld()->SpawnActor<ABattleUnitBase>(Class, Tile->GetSurfaceLocation() + FVector(0,0,1), FRotator::ZeroRotator, Params);
        if (!Unit) { Error = TEXT("SpawnActor failed for the configured Unit Class."); return nullptr; }
        SpawnedUnits.Add(Unit);
        FDataTableRowHandle Data; Data.DataTable = RuntimeData; Data.RowName = Side.RowName;
        if (!Unit->InitializeUnit(Data, Team, Side.Stars, Error)) return nullptr;
        if (!Unit->PlaceOnTile(Tile)) { Error = TEXT("PlaceOnTile failed for a test unit."); return nullptr; }
        Unit->OnUnitDied.AddDynamic(this, &AUnitCombatTestRunner::HandleUnitDied);
        return Unit;
    };
    AllyUnit = SpawnSide(Ally, EBattleTeam::Player, AllyTile);
    if (!AllyUnit) { Fail(TEXT("Ally setup failed: ") + Error); return; }
    EnemyUnit = SpawnSide(Enemy, EBattleTeam::Enemy, EnemyTile);
    if (!EnemyUnit) { Fail(TEXT("Enemy setup failed: ") + Error); return; }
    const TArray<ABattleUnitBase*> Roster = {AllyUnit, EnemyUnit};
    AllyUnit->SetCombatRoster(Roster); EnemyUnit->SetCombatRoster(Roster);
    if (!FMath::IsFinite(StartDelay) || StartDelay <= 0.f) BeginCombat();
    else GetWorldTimerManager().SetTimer(StartTimer, this, &AUnitCombatTestRunner::BeginCombat, StartDelay, false);
}
void AUnitCombatTestRunner::BeginCombat()
{
    if (TestState != EUnitCombatTestState::Preparing) return;
    FString Error;
    if (!ValidateIsolation(Error)) { Fail(Error); return; }
    if (!IsValid(Board) || !IsValid(AllyUnit) || !IsValid(EnemyUnit) || !AllyUnit->IsAlive() || !EnemyUnit->IsAlive()
        || !IsValid(AllyUnit->CurrentTile) || !IsValid(EnemyUnit->CurrentTile))
    { Fail(TEXT("A test unit or the board was removed before combat started.")); return; }
    AllyUnit->StartCombat(); EnemyUnit->StartCombat();
    if (!AllyUnit->bInCombat || !EnemyUnit->bInCombat) { Fail(TEXT("StartCombat rejected the test pair.")); return; }
    SetStatus(EUnitCombatTestState::Running, bUseTimerAttackForTest
        ? TEXT("Test running: timer basic attacks (source DT unchanged).") : TEXT("Test running: original animation/Notify settings."));
}
void AUnitCombatTestRunner::HandleUnitDied(ABattleUnitBase* Unit)
{
    if (bCleaningUp || bResultQueued || TestState != EUnitCombatTestState::Running || (Unit != AllyUnit && Unit != EnemyUnit)) return;
    bResultQueued = true;
    // 피해·처치 트리거 처리가 끝난 뒤 정지한다. 사망 이벤트 안에서 전투를 즉시 중단하지 않는다.
    ResultTimer = GetWorldTimerManager().SetTimerForNextTick(this, &AUnitCombatTestRunner::ProcessResult);
}
void AUnitCombatTestRunner::ProcessResult()
{
    bResultQueued = false;
    if (TestState != EUnitCombatTestState::Running) return;
    const bool bAllyAlive = IsValid(AllyUnit) && AllyUnit->IsAlive();
    const bool bEnemyAlive = IsValid(EnemyUnit) && EnemyUnit->IsAlive();
    if (!bAllyAlive || !bEnemyAlive)
        FinishCombat(bAllyAlive ? TEXT("Test finished: ally survived.") : bEnemyAlive ? TEXT("Test finished: enemy survived.") : TEXT("Test finished: both units defeated."));
}
void AUnitCombatTestRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (TestState != EUnitCombatTestState::Running) return;
    if (!IsValid(Board) || !IsValid(AllyUnit) || !IsValid(EnemyUnit))
    { Fail(TEXT("A test actor was removed during combat.")); return; }
    if (!AllyUnit->IsAlive() || !EnemyUnit->IsAlive()) { HandleUnitDied(!AllyUnit->IsAlive() ? AllyUnit.Get() : EnemyUnit.Get()); return; }
    ElapsedCombatTime += FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
    if (ElapsedCombatTime >= FMath::Max(1.f, CombatTimeout))
        FinishCombat(TEXT("Test time limit reached. If units approach but never hit, check attack Notify/timing settings."));
}
void AUnitCombatTestRunner::FinishCombat(const FString& Message)
{
    ClearTimers();
    // 테스트가 생성한 유닛만 정지한다. 다른 유닛 목록이나 GameMode는 건드리지 않는다.
    for (const TWeakObjectPtr<ABattleUnitBase>& Weak : SpawnedUnits)
        if (ABattleUnitBase* Unit = Weak.Get()) Unit->StopCombat();
    SetStatus(EUnitCombatTestState::Finished, Message);
}
void AUnitCombatTestRunner::ClearTimers()
{
    if (!GetWorld()) return;
    GetWorldTimerManager().ClearTimer(SetupTimer); GetWorldTimerManager().ClearTimer(StartTimer); GetWorldTimerManager().ClearTimer(ResultTimer);
    bResultQueued = false;
}
void AUnitCombatTestRunner::CleanupOwnedUnits()
{
    if (bCleaningUp) return;
    TGuardValue<bool> Guard(bCleaningUp, true);
    ClearTimers();
    const TArray<TWeakObjectPtr<ABattleUnitBase>> Owned = MoveTemp(SpawnedUnits);
    for (const TWeakObjectPtr<ABattleUnitBase>& Weak : Owned)
    {
        if (ABattleUnitBase* Unit = Weak.Get())
        {
            Unit->OnUnitDied.RemoveDynamic(this, &AUnitCombatTestRunner::HandleUnitDied);
            Unit->StopCombat(); Unit->Destroy();
        }
    }
    AllyUnit = nullptr; EnemyUnit = nullptr; RuntimeData = nullptr;
}
void AUnitCombatTestRunner::Fail(const FString& Message)
{
    CleanupOwnedUnits(); SetStatus(EUnitCombatTestState::Failed, Message);
}
void AUnitCombatTestRunner::StopTest()
{
    CleanupOwnedUnits(); SetStatus(EUnitCombatTestState::Idle, TEXT("Test stopped. Owned units removed."));
}
void AUnitCombatTestRunner::RestartTest() { StopTest(); StartTest(); }
void AUnitCombatTestRunner::EndPlay(const EEndPlayReason::Type Reason)
{
    CleanupOwnedUnits(); Super::EndPlay(Reason);
}
