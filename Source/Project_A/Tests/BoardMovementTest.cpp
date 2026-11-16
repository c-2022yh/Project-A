#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "../BattleUnitBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace BoardMovementTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        ABoardManager* Board = nullptr;
        UDataTable* Table = nullptr;
        uint64 SavedFrame = GFrameCounter;
        int32 Sequence = 0;
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
            UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
            World->SetGameInstance(Instance); Context.OwningGameInstance = Instance;
            FURL URL; URL.AddOption(TEXT("game=/Script/Engine.GameModeBase")); World->SetGameMode(URL);
            Board = World->SpawnActor<ABoardManager>();
            Board->TileClass = ABoardTile::StaticClass(); Board->bAutoCalculateSpacing = false;
            Board->HorizontalSpacing = 200.f; Board->VerticalSpacing = 173.2f;
            // BeginPlay만 수동 호출하면 bActorInitialized가 설정되지 않아 EndPlay 검증이 무효가 된다.
            // 엔진의 정식 초기화/플레이 순서를 사용하고 게임 진행은 빈 GameModeBase로 격리한다.
            World->InitializeActorsForPlay(URL); World->BeginPlay();
            Table = NewObject<UDataTable>(World); Table->RowStruct = FUnitDefinition::StaticStruct();
        }
        ~FFixture()
        {
            World->BeginTearingDown(); World->EndPlay(EEndPlayReason::EndPlayInEditor);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
            GFrameCounter = SavedFrame;
        }
        ABattleUnitBase* Spawn(EBattleTeam Team, ABoardTile* Tile, float Damage = 10.f)
        {
            FUnitDefinition Data; Data.BaseStats.MaxHealth = 1000.f; Data.BaseStats.AttackDamage = Damage;
            Data.BaseStats.CriticalChance = 0.f; Data.BaseStats.AttackRange = 1; Data.BaseStats.MovementSpeed = 300.f;
            Data.BasicAttack.bUseAnimNotify = false; Data.BasicAttack.AnimationSecondsAtSpeedOne = 0.2f;
            const FName Name(*FString::Printf(TEXT("Unit%d"), ++Sequence)); Table->AddRow(Name, Data);
            FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name;
            ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
            FString Error;
            if (!Unit->InitializeUnit(Handle, Team, 1, Error) || !Unit->PlaceOnTile(Tile)) return nullptr;
            return Unit;
        }
        ABoardTile* Reservation(ABattleUnitBase* Unit) const
        {
            for (ABoardTile* Tile : Board->Tiles) if (IsValid(Tile) && Tile->ReservedUnit == Unit) return Tile;
            return nullptr;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoardPathTest, "ProjectA.Board.Pathfinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoardPathTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    TestEqual(TEXT("Existing 5 by 6 board preserved"), F.Board->Tiles.Num(), 30);
    TestEqual(TEXT("Interior tile has six neighbors"), F.Board->GetNeighbors(F.Board->GetTile(2,2)).Num(), 6);
    ABattleUnitBase* A = F.Spawn(EBattleTeam::Player, F.Board->GetTile(0,0));
    ABattleUnitBase* E = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5,4));
    if (!TestNotNull(TEXT("Player"), A) || !TestNotNull(TEXT("Enemy"), E)) return false;
    TArray<ABoardTile*> Path;
    TestTrue(TEXT("Shortest attack-range path exists"), F.Board->FindPathToAttackRange(A, E, Path));
    TestEqual(TEXT("Path stops at attack range"), Path.Num(), ABattleUnitBase::HexDistance(A->CurrentTile, E->CurrentTile) - 1);
    ABoardTile* Previous = A->CurrentTile;
    for (ABoardTile* Step : Path)
    {
        TestEqual(TEXT("Every step is adjacent"), ABattleUnitBase::HexDistance(Previous, Step), 1);
        TestTrue(TEXT("Path does not enter occupied enemy tile"), Step != E->CurrentTile);
        Previous = Step;
    }
    ABoardTile* Blocked = Path[0]; Blocked->bBlocksMovement = true;
    TestTrue(TEXT("Terrain obstacle has an alternate path"), F.Board->FindPathToAttackRange(A, E, Path));
    TestFalse(TEXT("Detour excludes blocked tile"), Path.Contains(Blocked));
    Blocked->bBlocksMovement = false;
    for (ABoardTile* Tile : F.Board->Tiles) if (Tile->Row == 2) Tile->bBlocksMovement = true;
    A->StartCombat(); E->StartCombat(); ABoardTile* Next = nullptr;
    TestTrue(TEXT("Terrain cut is unreachable"), F.Board->TryReserveNextStep(A, E, 1, Next) == EUnitPathResult::Unreachable);
    for (ABoardTile* Tile : F.Board->Tiles) Tile->bBlocksMovement = false;
    for (ABoardTile* Tile : F.Board->GetNeighbors(A->CurrentTile)) Tile->SetOccupiedUnit(F.World->SpawnActor<AActor>());
    TestTrue(TEXT("Units blocking exits are temporary blockage"), F.Board->TryReserveNextStep(A, E, 2, Next) == EUnitPathResult::TemporarilyBlocked);
    TestNull(TEXT("Failed search reserves nothing"), Next);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoardReservationTest, "ProjectA.Board.Reservations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoardReservationTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABoardTile* ATile = F.Board->GetTile(2,1); ABoardTile* BTile = F.Board->GetTile(1,2);
    ABoardTile* Shared = F.Board->GetTile(2,2); ABoardTile* Near = F.Board->GetTile(3,2); ABoardTile* ETile = F.Board->GetTile(4,2);
    for (ABoardTile* Tile : F.Board->Tiles) Tile->bBlocksMovement = true;
    for (ABoardTile* Tile : {ATile, BTile, Shared, Near, ETile}) Tile->bBlocksMovement = false;
    ABattleUnitBase* A = F.Spawn(EBattleTeam::Player, ATile);
    ABattleUnitBase* B = F.Spawn(EBattleTeam::Player, BTile);
    ABattleUnitBase* E = F.Spawn(EBattleTeam::Enemy, ETile);
    if (!A || !B || !E) return false;
    A->StartCombat(); B->StartCombat(); E->StartCombat();
    ABoardTile* Next = nullptr;
    TestTrue(TEXT("First requester reserves shared tile"), F.Board->TryReserveNextStep(A, E, 101, Next) == EUnitPathResult::ReservedStep && Next == Shared);
    TestTrue(TEXT("Duplicate request is idempotent"), F.Board->TryReserveNextStep(A, E, 101, Next) == EUnitPathResult::ReservedStep && Next == Shared);
    TestTrue(TEXT("Second requester cannot take same tile"), F.Board->TryReserveNextStep(B, E, 201, Next) == EUnitPathResult::TemporarilyBlocked);
    TestTrue(TEXT("Source remains occupied while destination reserved"), ATile->OccupiedUnit == A && !IsValid(Shared->OccupiedUnit));
    TestFalse(TEXT("Reserved tile is not available for preparation"), Shared->CanPlaceUnit());
    Shared->SetOccupiedUnit(B);
    TestNull(TEXT("Legacy setter cannot steal reservation"), Shared->OccupiedUnit);
    TestFalse(TEXT("Stale cancellation cannot clear reservation"), F.Board->CancelReservedMove(A, 100));
    TestTrue(TEXT("Correct cancellation"), F.Board->CancelReservedMove(A, 101));
    TestTrue(TEXT("New reservation succeeds"), F.Board->TryReserveNextStep(A, E, 102, Next) == EUnitPathResult::ReservedStep);
    TestFalse(TEXT("Old token cannot cancel new reservation"), F.Board->CancelReservedMove(A, 101));
    TestFalse(TEXT("Cannot commit before physically moving"), F.Board->CommitReservedMove(A, 102));
    F.Board->CancelReservedMove(A, 102);
    A->Tick(0.01f);
    TestTrue(TEXT("Native unit code begins movement without BP adapter"), A->ActionState == EUnitActionState::Moving);
    TestTrue(TEXT("Native movement reserved shared tile"), Shared->ReservedUnit == A);
    A->SetActorLocation(Shared->GetSurfaceLocation() + FVector(0,0,1.f)); A->Tick(0.001f);
    TestTrue(TEXT("Arrival commits occupancy"), A->CurrentTile == Shared && Shared->OccupiedUnit == A);
    TestTrue(TEXT("Arrival releases source and reservation"), ATile->CanPlaceUnit() && !Shared->IsReserved());
    // 다음 이동을 요청해 두고 시작하지 않으면 유예 시간 뒤 회수한다.
    TestTrue(TEXT("Reserve next corridor tile"), F.Board->TryReserveNextStep(A, E, 103, Next) == EUnitPathResult::ReservedStep);
    F.Board->UnstartedReservationTimeout = 0.1f;
    F.World->Tick(LEVELTICK_TimeOnly, 0.2f); ++GFrameCounter;
    F.Board->PruneInvalidReservations();
    TestFalse(TEXT("Unstarted reservation expires"), Near->IsReserved());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoardMoveLifecycleTest, "ProjectA.Board.MoveLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoardMoveLifecycleTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABoardTile* Source = F.Board->GetTile(0,0);
    ABattleUnitBase* A = F.Spawn(EBattleTeam::Player, Source);
    ABattleUnitBase* E = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5,4));
    if (!A || !E) return false;
    TestTrue(TEXT("Placement auto-links board"), A->BoardManager == F.Board);
    A->StartCombat(); E->StartCombat(); A->Tick(0.01f);
    TestTrue(TEXT("Starts native approach"), A->ActionState == EUnitActionState::Moving);
    const FVector Start = Source->GetSurfaceLocation() + FVector(0,0,1.f);
    A->Tick(0.1f);
    TestFalse(TEXT("Physically moving between tiles"), A->GetActorLocation().Equals(Start));
    TestTrue(TEXT("Logical source retained during transit"), A->CurrentTile == Source && Source->OccupiedUnit == A);
    FUnitStatModifier Stun; Stun.bStun = true; A->AddModifier(TEXT("Stun"), Stun, 1.f);
    TestTrue(TEXT("Stun returns to source"), A->GetActorLocation().Equals(Start) && A->ActionState == EUnitActionState::Idle);
    TestNull(TEXT("Stun releases reservation"), F.Reservation(A));
    A->RemoveEffect(TEXT("Stun")); A->Tick(0.4f);
    TestTrue(TEXT("Can move again after stun"), A->ActionState == EUnitActionState::Moving);
    FUnitDamageRequest Kill; Kill.Attacker = A; Kill.RawDamage = 100000.f;
    E->ReceiveDamage(Kill); A->Tick(0.1f);
    TestTrue(TEXT("Target death cancels pending step"), A->GetActorLocation().Equals(Start) && !F.Reservation(A));
    A->StopCombat(); E->StopCombat(); A->ResetForRound(); E->ResetForRound();
    A->StartCombat(); E->StartCombat(); A->Tick(0.01f);
    const int32 MoveId = A->ActiveActionId;
    TestTrue(TEXT("Board cancellation reaches physical unit"), F.Board->CancelReservedMove(A, MoveId));
    TestTrue(TEXT("Cancellation restores position and action"), A->GetActorLocation().Equals(Start) && A->ActionState == EUnitActionState::Idle);
    A->Tick(0.4f);
    TestTrue(TEXT("Moving before actor destruction"), A->ActionState == EUnitActionState::Moving);
    A->Destroy();
    TestNull(TEXT("Actor deletion releases reserved tile"), F.Reservation(A));
    TestTrue(TEXT("Actor deletion releases occupied source"), Source->CanPlaceUnit());
    const int32 Count = F.Board->Tiles.Num(); F.Board->GenerateBoard();
    TestEqual(TEXT("Occupied board regeneration rejected"), F.Board->Tiles.Num(), Count);
    TestTrue(TEXT("Regeneration keeps existing enemy tile valid"), IsValid(E->CurrentTile));
    ABattleUnitBase* Replacement = F.Spawn(EBattleTeam::Player, Source);
    if (!Replacement) return false;
    Replacement->StartCombat(); Replacement->Tick(0.01f);
    ABoardTile* Destination = F.Reservation(Replacement);
    if (!TestNotNull(TEXT("Replacement reserves a destination"), Destination)) return false;
    Destination->Destroy();
    TestTrue(TEXT("Deleted destination cancels movement"), Replacement->ActionState == EUnitActionState::Idle && Replacement->GetActorLocation().Equals(Start));
    F.Board->Destroy();
    TestFalse(TEXT("Board destruction stops units"), Replacement->bInCombat || E->bInCombat);
    TestNull(TEXT("Board destruction removes stale tile link"), Replacement->CurrentTile.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoardCombatIntegrationTest, "ProjectA.Board.CombatIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoardCombatIntegrationTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABattleUnitBase* A = F.Spawn(EBattleTeam::Player, F.Board->GetTile(0,0), 200.f);
    ABattleUnitBase* E = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5,4), 1.f);
    if (!A || !E) return false;
    A->StartCombat(); E->StartCombat();
    // 실제 C++ 생각/이동/타이머 공격을 구동한다. 그림/AnimBP를 검증하는 Play Mode 테스트는 아니다.
    for (int32 Frame = 0; Frame < 800 && E->IsAlive(); ++Frame)
    {
        A->Tick(0.05f); E->Tick(0.05f);
        TestTrue(TEXT("Live units never occupy one tile"), !A->CurrentTile || !E->CurrentTile || A->CurrentTile != E->CurrentTile);
        for (ABoardTile* Tile : F.Board->Tiles)
            TestFalse(TEXT("Reserved destination is not already occupied"), Tile->IsReserved() && IsValid(Tile->OccupiedUnit));
    }
    TestTrue(TEXT("Approach and basic attacks defeat enemy"), !E->IsAlive() && A->IsAlive());
    A->StopCombat(); E->StopCombat();
    TestTrue(TEXT("Survivor restores original preparation placement"), A->ResetForRound() && A->CurrentTile == F.Board->GetTile(0,0));
    TestTrue(TEXT("Dead unit restores original preparation placement"), E->ResetForRound() && E->CurrentTile == F.Board->GetTile(5,4));
    for (ABoardTile* Tile : F.Board->Tiles) TestFalse(TEXT("No reservation survives round reset"), Tile->IsReserved());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoardTargetFallbackTest, "ProjectA.Board.TargetFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoardTargetFallbackTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    for (ABoardTile* Tile : F.Board->Tiles) if (Tile->HexQ == 1) Tile->bBlocksMovement = true;
    ABoardTile* NearTile = F.Board->GetTile(0,2); NearTile->TeamType = EBoardTileTeam::Enemy;
    ABattleUnitBase* A = F.Spawn(EBattleTeam::Player, F.Board->GetTile(0,0));
    ABattleUnitBase* Near = F.Spawn(EBattleTeam::Enemy, NearTile);
    ABattleUnitBase* Far = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(3,1));
    if (!A || !Near || !Far) return false;
    A->StartCombat(); Near->StartCombat(); Far->StartCombat();
    A->Tick(0.01f); A->Tick(0.4f);
    TestTrue(TEXT("Unreachable closest enemy is replaced by reachable enemy"), A->CurrentTarget == Far);
    TestTrue(TEXT("Fallback target leads to a reserved move"), A->ActionState == EUnitActionState::Moving && F.Reservation(A));
    A->CancelBoardMove(A->ActiveActionId);
    for (ABoardTile* Tile : F.Board->Tiles) if (Tile->Row == 2) Tile->bBlocksMovement = true;
    for (int32 Retry = 0; Retry < 8; ++Retry)
    {
        A->Tick(0.4f);
        TestTrue(TEXT("All blocked targets cause idle bounded retries"), A->ActionState == EUnitActionState::Idle && !F.Reservation(A));
    }
    return true;
}
#endif
