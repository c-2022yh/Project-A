#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../Testing/UnitCombatTestRunner.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace UnitRunnerTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        ABoardManager* Board = nullptr;
        UDataTable* Data = nullptr;
        uint64 SavedFrame = GFrameCounter;
        explicit FFixture(bool bProductionMode = false)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
            UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
            World->SetGameInstance(Instance); Context.OwningGameInstance = Instance;
            FURL URL;
            URL.AddOption(bProductionMode ? TEXT("game=/Script/Project_A.AutoBattleGameMode") : TEXT("game=/Script/Engine.GameModeBase"));
            World->SetGameMode(URL);
            Board = World->SpawnActor<ABoardManager>(); Board->TileClass = ABoardTile::StaticClass();
            Board->bAutoCalculateSpacing = false; Board->HorizontalSpacing = 200.f; Board->VerticalSpacing = 173.2f;
            World->InitializeActorsForPlay(URL); World->BeginPlay();
            Data = NewObject<UDataTable>(World); Data->RowStruct = FUnitDefinition::StaticStruct();
            FUnitDefinition Strong; Strong.BaseStats.MaxHealth = 500.f; Strong.BaseStats.AttackDamage = 200.f;
            Strong.BaseStats.MovementSpeed = 1000.f; Strong.BaseStats.CriticalChance = 0.f;
            Data->AddRow(TEXT("Strong"), Strong);
            FUnitDefinition Weak = Strong; Weak.BaseStats.MaxHealth = 100.f; Weak.BaseStats.AttackDamage = 1.f;
            Data->AddRow(TEXT("Weak"), Weak);
        }
        ~FFixture()
        {
            World->BeginTearingDown(); World->EndPlay(EEndPlayReason::EndPlayInEditor);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false); GFrameCounter = SavedFrame;
        }
        AUnitCombatTestRunner* Runner(bool bAutoStart = false)
        {
            AUnitCombatTestRunner* Actor = World->SpawnActorDeferred<AUnitCombatTestRunner>(AUnitCombatTestRunner::StaticClass(), FTransform::Identity);
            Actor->bAutoStartOnPlay = bAutoStart; Actor->bShowScreenMessages = false;
            Actor->AllowedMapName = FName(*UGameplayStatics::GetCurrentLevelName(World, true));
            Actor->Board = Board; Actor->UnitClass = TSoftClassPtr<ABattleUnitBase>(ABattleUnitBase::StaticClass());
            Actor->UnitDataTable = TSoftObjectPtr<UDataTable>(Data);
            Actor->Ally.RowName = TEXT("Strong"); Actor->Enemy.RowName = TEXT("Weak");
            Actor->StartDelay = 0.f; Actor->CombatTimeout = 10.f;
            Actor->FinishSpawning(FTransform::Identity); return Actor;
        }
        void Tick(float Delta = 0.05f) { World->Tick(LEVELTICK_All, Delta); ++GFrameCounter; }
        bool Until(AUnitCombatTestRunner* Runner, EUnitCombatTestState State, int32 Frames = 400)
        {
            for (int32 Index = 0; Index < Frames && Runner->TestState != State && Runner->TestState != EUnitCombatTestState::Failed; ++Index) Tick();
            return Runner->TestState == State;
        }
        int32 Units() const
        {
            int32 Count = 0; for (TActorIterator<ABattleUnitBase> It(World); It; ++It) if (IsValid(*It)) ++Count;
            return Count;
        }
        bool EmptyBoard() const
        {
            for (ABoardTile* Tile : Board->Tiles) if (IsValid(Tile->OccupiedUnit) || Tile->IsReserved()) return false;
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitRunnerFlowTest, "ProjectA.TestRunner.FlowAndIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitRunnerFlowTest::RunTest(const FString& Parameters)
{
    using namespace UnitRunnerTests;
    FFixture F;
    const AGameModeBase* OriginalMode = F.World->GetAuthGameMode();
    AUnitCombatTestRunner* Runner = F.Runner(true);
    Runner->StartTest(); // 자동 시작과 중복 호출해도 한 쌍만 생성해야 한다.
    TestTrue(TEXT("Runner reaches running state"), F.Until(Runner, EUnitCombatTestState::Running));
    if (!IsValid(Runner->AllyUnit) || !IsValid(Runner->EnemyUnit)) return false;
    TestEqual(TEXT("Exactly two owned actors"), F.Units(), 2);
    TestTrue(TEXT("Team and board initialization"), Runner->AllyUnit->TeamType == EBattleTeam::Player && Runner->EnemyUnit->TeamType == EBattleTeam::Enemy
        && Runner->AllyUnit->BoardManager == F.Board && Runner->EnemyUnit->BoardManager == F.Board);
    TestFalse(TEXT("Runtime copy uses timer attacks"), Runner->AllyUnit->Definition.BasicAttack.bUseAnimNotify);
    TestTrue(TEXT("Source data table remains unchanged"), F.Data->FindRow<FUnitDefinition>(TEXT("Strong"), TEXT("test"))->BasicAttack.bUseAnimNotify);
    TestTrue(TEXT("Pair approaches and finishes battle"), F.Until(Runner, EUnitCombatTestState::Finished));
    TestTrue(TEXT("Ally survives stronger matchup"), Runner->AllyUnit->IsAlive() && !Runner->EnemyUnit->IsAlive());
    TestFalse(TEXT("Finish stops both units"), Runner->AllyUnit->bInCombat || Runner->EnemyUnit->bInCombat);
    TestTrue(TEXT("Finish clears reservations and occupancy"), F.EmptyBoard());
    TestTrue(TEXT("Existing game mode unchanged"), F.World->GetAuthGameMode() == OriginalMode);
    TestEqual(TEXT("Board tiles unchanged"), F.Board->Tiles.Num(), 30);
    Runner->StopTest(); TestEqual(TEXT("Stop deletes only test pair"), F.Units(), 0);
    F.Tick(); TestEqual(TEXT("No delayed respawn after stop"), F.Units(), 0);
    Runner->RestartTest(); TestTrue(TEXT("Restart runs again"), F.Until(Runner, EUnitCombatTestState::Running));
    TestEqual(TEXT("Restart does not accumulate actors"), F.Units(), 2);
    Runner->Destroy(); TestEqual(TEXT("Runner destruction cleans owned actors"), F.Units(), 0);
    TestTrue(TEXT("Runner destruction clears its board usage"), F.EmptyBoard());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitRunnerSafetyTest, "ProjectA.TestRunner.SafetyAndFailureCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitRunnerSafetyTest::RunTest(const FString& Parameters)
{
    using namespace UnitRunnerTests;
    {
        FFixture F;
        AUnitCombatTestRunner* Runner = F.Runner();
        Runner->AllowedMapName = TEXT("WrongMap"); Runner->StartTest();
        TestTrue(TEXT("Wrong map is rejected"), Runner->TestState == EUnitCombatTestState::Failed);
        TestEqual(TEXT("Wrong map spawns nothing"), F.Units(), 0);
        Runner->AllowedMapName = FName(*UGameplayStatics::GetCurrentLevelName(F.World, true));
        Runner->Enemy.Stars = 3; Runner->StartTest();
        F.Until(Runner, EUnitCombatTestState::Running);
        TestTrue(TEXT("Invalid enemy setup fails"), Runner->TestState == EUnitCombatTestState::Failed);
        TestEqual(TEXT("Partial spawn is cleaned"), F.Units(), 0);
        TestTrue(TEXT("Partial placement is cleaned"), F.EmptyBoard());
        Runner->Enemy.Stars = 1;
        ABattleUnitBase* Existing = F.World->SpawnActor<ABattleUnitBase>();
        FDataTableRowHandle Handle; Handle.DataTable = F.Data; Handle.RowName = TEXT("Strong"); FString Error;
        Existing->InitializeUnit(Handle, EBattleTeam::Player, 1, Error); Existing->PlaceOnTile(F.Board->GetTile(0,0));
        Runner->StartTest(); F.Until(Runner, EUnitCombatTestState::Running);
        TestTrue(TEXT("Occupied board is rejected"), Runner->TestState == EUnitCombatTestState::Failed);
        TestTrue(TEXT("Existing actor remains intact"), IsValid(Existing) && Existing->IsAlive() && Existing->CurrentTile == F.Board->GetTile(0,0));
        Existing->Destroy();
        AUnitCombatTestRunner* Other = F.Runner(); Runner->StartTest();
        TestTrue(TEXT("Duplicate runner rejected"), Runner->TestState == EUnitCombatTestState::Failed);
        TestTrue(TEXT("Other runner remains idle"), Other->TestState == EUnitCombatTestState::Idle);
        TestEqual(TEXT("Duplicate runner spawns nothing"), F.Units(), 0); Other->Destroy();
        Runner->StartTest(); F.Board->Destroy(); F.Until(Runner, EUnitCombatTestState::Running);
        TestTrue(TEXT("Board removal while waiting fails cleanly"), Runner->TestState == EUnitCombatTestState::Failed);
        TestEqual(TEXT("Board removal spawns nothing"), F.Units(), 0);
    }
    {
        FFixture F(true);
        AUnitCombatTestRunner* Runner = F.Runner(); Runner->StartTest();
        TestTrue(TEXT("Production game mode is rejected"), Runner->TestState == EUnitCombatTestState::Failed);
        TestTrue(TEXT("Failure explains mode requirement"), Runner->Status.Contains(TEXT("GameModeBase")));
        TestEqual(TEXT("Production mode receives no test units"), F.Units(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitRunnerNotifyTest, "ProjectA.TestRunner.NotifyModeAndTimeout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitRunnerNotifyTest::RunTest(const FString& Parameters)
{
    using namespace UnitRunnerTests;
    FFixture F; AUnitCombatTestRunner* Runner = F.Runner();
    Runner->bUseTimerAttackForTest = false; Runner->CombatTimeout = 6.f; Runner->StartTest();
    TestTrue(TEXT("Data-driven mode starts"), F.Until(Runner, EUnitCombatTestState::Running));
    if (!IsValid(Runner->AllyUnit) || !IsValid(Runner->EnemyUnit)) return false;
    TestTrue(TEXT("Notify mode retains original flag"), Runner->AllyUnit->Definition.BasicAttack.bUseAnimNotify);
    TestTrue(TEXT("Missing Notify test ends by timeout"), F.Until(Runner, EUnitCombatTestState::Finished));
    TestTrue(TEXT("Timeout reports diagnostic"), Runner->Status.Contains(TEXT("time limit")));
    TestEqual(TEXT("No synthetic hits in Notify mode"), Runner->AllyUnit->CurrentHP, Runner->AllyUnit->FinalStats.MaxHealth);
    TestEqual(TEXT("Enemy receives no timer fallback damage"), Runner->EnemyUnit->CurrentHP, Runner->EnemyUnit->FinalStats.MaxHealth);
    TestFalse(TEXT("Timeout stops combat"), Runner->AllyUnit->bInCombat || Runner->EnemyUnit->bInCombat);
    return true;
}
#endif
