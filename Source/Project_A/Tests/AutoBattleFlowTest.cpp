#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "../AutoBattleGameMode.h"
#include "../AutoBattlePlayerController.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutoBattleFlowTest, "ProjectA.Battle.SingleEncounter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutoBattleFlowTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues IV;
    IV.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(true).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
        ERHIFeatureLevel::Num, &IV);
    if (!TestNotNull(TEXT("Test world created"), World)) return false;
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(GameInstance);
    Context.OwningGameInstance = GameInstance;
    const uint64 SavedFrameCounter = GFrameCounter;
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        GFrameCounter = SavedFrameCounter;
    };
    FURL URL;
    URL.AddOption(TEXT("game=/Script/Project_A.AutoBattleGameMode"));
    World->SetGameMode(URL);
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(World->GetAuthGameMode());
    if (!TestNotNull(TEXT("GameMode created"), Mode)) return false;
    Mode->PreparationDuration = 0;
    Mode->CombatTimeLimit = 60;

    AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>();
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Floor->SetActorScale3D(FVector(60, 60, 1));
    Floor->SetActorLocation(FVector(0, 0, -50));
    ABoardManager* Board = World->SpawnActor<ABoardManager>();
    Board->TileClass = ABoardTile::StaticClass();
    Board->bAutoCalculateSpacing = false;
    Board->HorizontalSpacing = 500;
    Board->VerticalSpacing = 430;
    World->InitializeActorsForPlay(URL);
    World->BeginPlay();
    const auto TickWorld = [World](float Delta)
    {
        World->Tick(LEVELTICK_All, Delta);
        // 엔진의 FTestWorldWrapper와 동일하게, 모의 프레임마다 프레임 카운터를 갱신한다.
        ++GFrameCounter;
    };
    TickWorld(0.02f);
    if (!TestTrue(TEXT("Roster ready after board BeginPlay"), Mode->bEncounterReady)) return false;
    TestEqual(TEXT("Thirty tiles"), Board->Tiles.Num(), 30);
    TestEqual(TEXT("Player half"), Board->GetPlayerTiles().Num(), 15);
    TestEqual(TEXT("Enemy half"), Board->GetEnemyTiles().Num(), 15);
    TestEqual(TEXT("Six roster units"), Mode->BattleUnits.Num(), 6);
    TestTrue(TEXT("Preparation is explicitly active"), Mode->bPreparationActive);
    // BP의 SpawnActor -> 진영 설정 -> 등록 -> 타일 배치 또는 대기 -> 생성 완료 흐름을 검증한다.
    // 이 테스트의 유닛 구성에는 C++의 UnitRoster 생성 반복을 사용하지 않는다.
    Mode->CancelEncounterSetup();
    TestEqual(TEXT("Cancel setup removes registered units"), Mode->BattleUnits.Num(), 0);
    TestTrue(TEXT("Manual encounter setup begins"), Mode->BeginEncounterSetup());
    TestFalse(TEXT("Nested setup rejected"), Mode->BeginEncounterSetup());
    TestFalse(TEXT("Native spawn cannot mix with BP setup"), Mode->SpawnEncounterUnits());
    TestFalse(TEXT("Null registration rejected"), Mode->RegisterBattleUnit(nullptr));
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ABattleUnitBase* ManualEnemy = World->SpawnActor<ABattleUnitBase>(ABattleUnitBase::StaticClass(), FVector(0, 0, 500), FRotator::ZeroRotator, SpawnParams);
    if (!TestNotNull(TEXT("BP-style enemy spawned"), ManualEnemy)) return false;
    ManualEnemy->TeamType = EBattleTeam::Enemy;
    TestTrue(TEXT("Spawned enemy registered"), Mode->RegisterBattleUnit(ManualEnemy));
    TestFalse(TEXT("Duplicate registration rejected"), Mode->RegisterBattleUnit(ManualEnemy));
    TestFalse(TEXT("Unplaced enemy prevents completion"), Mode->CompleteEncounterSetup());
    TestFalse(TEXT("Enemy cannot be placed on player tile"), ManualEnemy->PlaceOnTile(Board->GetTile(0, 0)));
    TestTrue(TEXT("Manual enemy placed"), ManualEnemy->PlaceOnTile(Board->GetTile(4, 1)));
    TestFalse(TEXT("Enemy-only setup cannot complete"), Mode->CompleteEncounterSetup());
    ABattleUnitBase* ManualPlayer = World->SpawnActor<ABattleUnitBase>(ABattleUnitBase::StaticClass(), FVector(0, 0, 500), FRotator::ZeroRotator, SpawnParams);
    if (!TestNotNull(TEXT("BP-style player spawned"), ManualPlayer)) return false;
    ManualPlayer->TeamType = EBattleTeam::Player;
    TestTrue(TEXT("Spawned player registered"), Mode->RegisterBattleUnit(ManualPlayer));
    TestFalse(TEXT("Invalid waiting index rejected"), Mode->MoveUnitToWaitingArea(ManualPlayer, 1, 1));
    TestTrue(TEXT("Manual player moved to waiting area"), Mode->MoveUnitToWaitingArea(ManualPlayer, 0, 1));
    TestFalse(TEXT("Waiting unit remains undeployed"), ManualPlayer->bIsDeployed);
    TestTrue(TEXT("Registered encounter completed"), Mode->CompleteEncounterSetup());
    TestTrue(TEXT("Completion marks units ready"), Mode->bEncounterReady);
    TestFalse(TEXT("Completion does not start preparation implicitly"), Mode->bPreparationActive);
    TestFalse(TEXT("Registration after completion rejected"), Mode->RegisterBattleUnit(ManualPlayer));
    TestFalse(TEXT("Completion cannot run twice"), Mode->CompleteEncounterSetup());
    Mode->StartPreparationPhase();
    TestTrue(TEXT("Manual encounter enters preparation"), Mode->bPreparationActive);
    TestTrue(TEXT("Manual player deploys"), ManualPlayer->PlaceOnTile(Board->GetTile(0, 0)));
    ManualPlayer->AttackDamage = 100;
    ManualPlayer->MaxHP = ManualPlayer->CurrentHP = 500;
    Mode->RequestCombatStart();
    TestTrue(TEXT("Registered units enter combat"), Mode->CurrentPhase == EGamePhase::Combat);
    TestFalse(TEXT("Registration during combat rejected"), Mode->RegisterBattleUnit(ManualPlayer));
    Mode->CancelEncounterSetup();
    TestEqual(TEXT("Setup cancellation does not destroy an active battle"), Mode->BattleUnits.Num(), 2);
    for (int32 Frame = 0; Frame < 600 && Mode->CurrentPhase == EGamePhase::Combat; ++Frame) TickWorld(1.0f / 30.0f);
    TestTrue(TEXT("BP-style registered units fight to victory"), Mode->CurrentPhase == EGamePhase::Victory);
    Mode->RequestBattleRestart();
    TestEqual(TEXT("Native fallback still creates full roster after manual battle"), Mode->BattleUnits.Num(), 6);
    TestTrue(TEXT("Native fallback still starts preparation"), Mode->bPreparationActive);

    ABattleUnitBase* OriginalRosterUnit = Mode->BattleUnits[0];
    Mode->StartPreparationPhase();
    TestTrue(TEXT("Starting preparation does not respawn the roster"), Mode->BattleUnits[0] == OriginalRosterUnit);
    TestFalse(TEXT("Duplicate spawn rejected"), Mode->SpawnEncounterUnits());
    TestEqual(TEXT("Duplicate spawn preserves roster size"), Mode->BattleUnits.Num(), 6);
    for (const FName EventName : {FName(TEXT("ReceiveBoardReady")), FName(TEXT("ReceivePreparationExpired")),
        FName(TEXT("ReceiveCombatStartRequested")), FName(TEXT("ReceiveBattleResultDetected")), FName(TEXT("ReceiveRestartRequested"))})
    {
        UFunction* Event = Mode->FindFunction(EventName);
        if (TestNotNull(*FString::Printf(TEXT("Blueprint flow event %s is reflected"), *EventName.ToString()), Event))
            TestTrue(TEXT("Flow event supports Blueprint override"), Event->HasAllFunctionFlags(FUNC_BlueprintEvent | FUNC_Native));
    }

    TestEqual(TEXT("No player deployed initially"), Mode->GetDeployedPlayerCount(), 0);
    TestEqual(TEXT("Enemies predeployed"), Mode->GetAliveTeamCount(EBattleTeam::Enemy), 3);
    Mode->RequestCombatStart();
    TestTrue(TEXT("Empty deployment cannot start"), Mode->CurrentPhase == EGamePhase::Preparation);

    TArray<ABattleUnitBase*> Players;
    for (ABattleUnitBase* Unit : Mode->BattleUnits) if (Unit->TeamType == EBattleTeam::Player) Players.Add(Unit);
    if (!TestEqual(TEXT("Three waiting players"), Players.Num(), 3)) return false;
    ABattleUnitBase* Unit = Players[0];
    TestTrue(TEXT("Initial placement"), Unit->PlaceOnTile(Board->GetTile(0, 0)));
    TestTrue(TEXT("Reposition"), Unit->PlaceOnTile(Board->GetTile(0, 1)));
    TestTrue(TEXT("Old tile freed"), Board->GetTile(0, 0)->CanPlaceUnit());
    TestTrue(TEXT("New tile links back"), Board->GetTile(0, 1)->OccupiedUnit == Unit && Unit->CurrentTile == Board->GetTile(0, 1));
    TestTrue(TEXT("Placement uses top surface"), FMath::IsNearlyEqual(Unit->GetActorLocation().Z,
        Board->GetTile(0, 1)->GetSurfaceLocation().Z + Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1, 0.1));
    TestFalse(TEXT("Player cannot use enemy tile"), Unit->PlaceOnTile(Board->GetTile(5, 0)));
    TestFalse(TEXT("Other unit cannot overwrite occupant"), Players[1]->PlaceOnTile(Board->GetTile(0, 1)));
    TestTrue(TEXT("Place second"), Players[1]->PlaceOnTile(Board->GetTile(0, 2)));
    TestTrue(TEXT("Place third"), Players[2]->PlaceOnTile(Board->GetTile(0, 3)));
    const float OriginalHP = Unit->CurrentHP;
    Unit->ReceiveDamage(-100);
    TestEqual(TEXT("Negative damage rejected"), Unit->CurrentHP, OriginalHP);
    for (ABattleUnitBase* Player : Players) { Player->AttackDamage = 100; Player->MaxHP = 500; Player->CurrentHP = 500; }
    const FVector Start = Unit->GetActorLocation();
    Mode->RequestCombatStart();
    TestTrue(TEXT("Combat started"), Mode->CurrentPhase == EGamePhase::Combat);
    TestFalse(TEXT("Combat placement locked"), Unit->PlaceOnTile(Board->GetTile(0, 0)));
    for (int32 Frame = 0; Frame < 1200 && Mode->CurrentPhase == EGamePhase::Combat; ++Frame)
        TickWorld(1.0f / 30.0f);
    TestTrue(TEXT("Units physically moved"), FVector::Dist2D(Start, Unit->GetActorLocation()) > 50);
    TestTrue(TEXT("Automatic combat reaches victory"), Mode->CurrentPhase == EGamePhase::Victory);
    TestFalse(TEXT("Native result fallback clears pending result"), Mode->bBattleResultPending);
    for (ABattleUnitBase* Survivor : Mode->BattleUnits)
        if (IsValid(Survivor)) TestFalse(TEXT("Result stops combat"), Survivor->bInCombat);

    Mode->RequestBattleRestart();
    TestTrue(TEXT("Restart returns to preparation"), Mode->CurrentPhase == EGamePhase::Preparation && Mode->bEncounterReady);
    TestEqual(TEXT("Restart clears deployment"), Mode->GetDeployedPlayerCount(), 0);
    TestEqual(TEXT("Restart creates fresh roster"), Mode->BattleUnits.Num(), 6);
    for (ABattleUnitBase* Candidate : Mode->BattleUnits)
        if (Candidate->TeamType == EBattleTeam::Player) { Candidate->PlaceOnTile(Board->GetTile(0, 0)); break; }
    Mode->RequestCombatStart();
    for (ABattleUnitBase* Candidate : Mode->BattleUnits)
        if (Candidate->TeamType == EBattleTeam::Player && Candidate->bIsDeployed) Candidate->Die();
    TickWorld(0.02f);
    TestTrue(TEXT("Undeployed bench does not prevent defeat"), Mode->CurrentPhase == EGamePhase::GameOver);

    Mode->RequestBattleRestart();
    Mode->PreparationDuration = 0.05f;
    Mode->RemainingPreparationTime = 0.05f;
    TickWorld(0.1f);
    TestTrue(TEXT("Empty timer expiry stays in preparation"), Mode->CurrentPhase == EGamePhase::Preparation);
    for (ABattleUnitBase* Candidate : Mode->BattleUnits)
        if (Candidate->TeamType == EBattleTeam::Player) { Candidate->PlaceOnTile(Board->GetTile(0, 0)); break; }
    TickWorld(0.1f);
    TestTrue(TEXT("Timer starts a deployed team"), Mode->CurrentPhase == EGamePhase::Combat);
    return true;
}
#endif
