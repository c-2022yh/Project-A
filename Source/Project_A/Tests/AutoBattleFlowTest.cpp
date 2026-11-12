#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "../BattleUnitBase.h"
#include "../BoardTile.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitDataInitializationTest, "ProjectA.Unit.DataInitialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnitDataInitializationTest::RunTest(const FString& Parameters)
{
    // 화면 대신 실제 Actor와 DT를 사용해 데이터 교체와 배치 계약을 검사한다.
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("World"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };
    UDataTable* Table = NewObject<UDataTable>(World);
    Table->RowStruct = FUnitDefinition::StaticStruct();
    FUnitDefinition Knight;
    Knight.BaseStats.MaxHealth = 200.f;
    Knight.BaseStats.AttackDamage = 30.f;
    Knight.StarHealthMultipliers = {1.f, 2.f, 3.f};
    Knight.Visuals.VisualTransform.SetScale3D(FVector(2.f));
    Table->AddRow(TEXT("Knight"), Knight);
    FUnitDefinition Mage;
    Mage.BaseStats.MaxHealth = 80.f;
    Mage.BaseStats.AbilityPower = 40.f;
    Mage.BaseStats.CriticalChance = 3.f;
    Table->AddRow(TEXT("Mage"), Mage);
    ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
    if (!TestNotNull(TEXT("Unit"), Unit)) return false;
    FDataTableRowHandle Handle;
    Handle.DataTable = Table;
    Handle.RowName = TEXT("Knight");
    FString Error;
    TestTrue(TEXT("Knight initialized"), Unit->InitializeUnit(Handle, EBattleTeam::Player, 2, Error));
    TestEqual(TEXT("Star health"), Unit->FinalStats.MaxHealth, 400.f);
    Unit->RecalculateStats();
    TestEqual(TEXT("Recalculation does not stack"), Unit->FinalStats.MaxHealth, 400.f);
    TestEqual(TEXT("Source row remains unchanged"), Table->FindRow<FUnitDefinition>(TEXT("Knight"), TEXT("test"))->BaseStats.MaxHealth, 200.f);
    TestTrue(TEXT("Visual correction applied"), Unit->VisualRoot->GetRelativeScale3D().Equals(FVector(2.f)));
    TestTrue(TEXT("Actor scale unaffected"), Unit->GetActorScale3D().Equals(FVector::OneVector));
    // 이전 외형을 일부러 남긴 뒤 빈 외형 행을 적용해 잔여값이 지워지는지 확인한다.
    Unit->MainHand->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Unit->Body->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
    Handle.RowName = TEXT("Mage");
    TestTrue(TEXT("Same actor changes to Mage"), Unit->InitializeUnit(Handle, EBattleTeam::Enemy, 1, Error));
    TestEqual(TEXT("Mage health resets"), Unit->CurrentHP, 80.f);
    TestEqual(TEXT("Chance clamped"), Unit->FinalStats.CriticalChance, 1.f);
    TestNull(TEXT("Old weapon cleared"), Unit->MainHand->GetStaticMesh());
    TestEqual(TEXT("Old material override cleared"), Unit->Body->OverrideMaterials.Num(), 0);
    TestTrue(TEXT("Old visual scale cleared"), Unit->VisualRoot->GetRelativeScale3D().Equals(FVector::OneVector));
    TestTrue(TEXT("Team changed"), Unit->TeamType == EBattleTeam::Enemy);
    Handle.RowName = TEXT("Missing");
    TestFalse(TEXT("Missing row rejected"), Unit->InitializeUnit(Handle, EBattleTeam::Player, 1, Error));
    TestEqual(TEXT("Failure preserves identity"), Unit->UnitID, FName(TEXT("Mage")));
    TestTrue(TEXT("Failure explains reason"), !Error.IsEmpty());
    TestFalse(TEXT("Invalid stars rejected"), Unit->SetStarLevel(4));
    ABoardTile* Tile = World->SpawnActor<ABoardTile>();
    Tile->TeamType = EBoardTileTeam::Enemy;
    Tile->SetActorLocation(FVector(20, 30, 40));
    TestTrue(TEXT("Existing board accepts new actor"), Unit->PlaceOnTile(Tile));
    TestTrue(TEXT("Tile links back"), Tile->OccupiedUnit == Unit && Unit->CurrentTile == Tile);
    TestEqual(TEXT("Actor origin uses floor"), Unit->GetActorLocation().Z, 41.0);
    Handle.RowName = TEXT("Knight");
    TestFalse(TEXT("Definition swap while deployed rejected"), Unit->InitializeUnit(Handle, EBattleTeam::Player, 1, Error));
    Unit->StartCombat();
    TestTrue(TEXT("Combat lifecycle connected"), Unit->bInCombat);
    TestFalse(TEXT("Combat placement locked"), Unit->PlaceOnTile(Tile));
    Unit->SetTeamType(EBattleTeam::Player);
    TestTrue(TEXT("Team setter cannot invalidate an occupied tile"), Unit->TeamType == EBattleTeam::Enemy);
    Unit->StopCombat();
    Unit->Destroy();
    // 파괴된 점유자가 배치를 막지 않아야 한다.
    TestTrue(TEXT("Destroyed occupant does not block tile"), Tile->CanPlaceUnit());
    return true;
}
#endif
