#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "../AutoBattleGameMode.h"
#include "../BattleUnitBase.h"
#include "../BenchTile.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamBoardIntegrationTest, "ProjectA.Integration.DeploymentAndCombat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTeamBoardIntegrationTest::RunTest(const FString& Parameters)
{
    // 실제 보드/유닛 API로 대기석 → 배치 → 교환 → 전투 → 복귀 경계를 확인한다.
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(Instance); Context.OwningGameInstance = Instance;
    FURL URL; URL.AddOption(TEXT("game=/Script/Project_A.AutoBattleGameMode"));
    World->SetGameMode(URL);
    ABoardManager* Board = World->SpawnActor<ABoardManager>();
    Board->TileClass = ABoardTile::StaticClass(); Board->bAutoCalculateSpacing = false;
    World->InitializeActorsForPlay(URL); World->BeginPlay();
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown(); World->EndPlay(EEndPlayReason::EndPlayInEditor);
        GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
    };
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(World->GetAuthGameMode());
    if (!TestNotNull(TEXT("Native game mode"), Mode)) return false;
    Mode->BoardManager = Board; Mode->MaxBoardUnits = 1; Mode->MaxRound = 5;
    UDataTable* Data = NewObject<UDataTable>(World); Data->RowStruct = FUnitDefinition::StaticStruct();
    FUnitDefinition Definition; Definition.BaseStats.MaxHealth = 1000.f;
    Data->AddRow(TEXT("Unit"), Definition);
    FDataTableRowHandle Row; Row.DataTable = Data; Row.RowName = TEXT("Unit");
    auto Spawn = [&](EBattleTeam Team)
    {
        ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
        FString Error;
        TestTrue(TEXT("Initialize unit"), Unit->InitializeUnit(Row, Team, 1, Error));
        TestTrue(TEXT("Register unit"), Mode->RegisterBattleUnit(Unit));
        return Unit;
    };
    TestEqual(TEXT("Seven physical bench slots"), Board->BenchSlots.Num(), 7);
    TestTrue(TEXT("Begin setup"), Mode->BeginEncounterSetup());
    ABattleUnitBase* A = Spawn(EBattleTeam::Player);
    ABattleUnitBase* B = Spawn(EBattleTeam::Player);
    ABattleUnitBase* Enemy = Spawn(EBattleTeam::Enemy);
    TestTrue(TEXT("Place enemy"), Enemy->PlaceOnTile(Board->GetTile(5, 4)));
    TestTrue(TEXT("Finish setup"), Mode->CompleteEncounterSetup());
    Mode->StartPreparationPhase();
    FString Reason;
    TestTrue(TEXT("Waiting occupancy valid"), Board->ValidateUnitPlacementState(A, Reason));
    ABenchTile* First = nullptr;
    for (ABenchTile* Slot : Board->BenchSlots) if (Slot->OccupiedUnit == A) First = Slot;
    if (!TestNotNull(TEXT("Ally bench slot"), First)) return false;
    TestTrue(TEXT("Waiting uses logical state"), A->Placement == EUnitPlacement::Waiting);
    ABoardTile* Target = Board->GetTile(0, 0);
    TestTrue(TEXT("Deploy ally"), Board->TryPlaceUnitOnTile(A, Target));
    TestTrue(TEXT("Foot origin matches combat movement"), A->GetActorLocation().Equals(Target->GetSurfaceLocation() + FVector(0, 0, 1), 0.1f));
    TestFalse(TEXT("Extra deployment blocked at limit"), Board->TryPlaceUnitOnTile(B, Board->GetTile(0, 1)));
    TestTrue(TEXT("Swap allowed at deployment limit"), Board->TryPlaceUnitOnTile(B, Target));
    TestTrue(TEXT("Swapped ally becomes waiting"), A->Placement == EUnitPlacement::Waiting && !A->bIsDeployed);
    TestTrue(TEXT("Swapped occupancy valid"), Board->ValidateUnitPlacementState(A, Reason));
    Board->SetInteractionLocked(TEXT("IntegrationTest"), true);
    TestFalse(TEXT("Locked board rejects moves"), Board->TryMoveUnitToBench(B, First));
    Board->SetInteractionLocked(TEXT("IntegrationTest"), false);
    TestTrue(TEXT("Save formation"), Board->SavePlayerFormation());
    Mode->StartCombatPhase();
    TestTrue(TEXT("Deployed unit enters combat"), B->bInCombat);
    TestFalse(TEXT("Bench unit stays out of combat"), A->bInCombat);
    TestFalse(TEXT("Combat prevents deployed recall"), Board->TryMoveUnitToBench(B, First));
    ABoardTile* Next = nullptr;
    TestTrue(TEXT("Merged board still reserves combat path"), Board->TryReserveNextStep(B, Enemy, 10, Next) == EUnitPathResult::ReservedStep);
    TestNotNull(TEXT("Reservation destination"), Next);
    FUnitDamageRequest Damage; Damage.Attacker = B; Damage.RawDamage = 100000.f; Damage.Type = EUnitDamageType::TrueDamage;
    TestTrue(TEXT("Enemy defeated through damage API"), Enemy->ReceiveDamage(Damage).bKilled);
    Mode->Tick(0.f);
    Mode->HandleRoundClear();
    TestTrue(TEXT("Survivor returns to saved formation"), B->CurrentTile == Target && Target->OccupiedUnit == B);
    TestTrue(TEXT("Restored occupancy valid"), Board->ValidateUnitPlacementState(B, Reason));
    TestFalse(TEXT("Result releases combat reservations"), Next && Next->IsReserved());
    return true;
}
#endif
