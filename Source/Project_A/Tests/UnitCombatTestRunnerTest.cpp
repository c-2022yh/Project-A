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
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitRunnerManualLineupTest, "ProjectA.TestRunner.ManualLineup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitRunnerManualLineupTest::RunTest(const FString& Parameters)
{
    using namespace UnitRunnerTests;
    FFixture F;
    AUnitCombatTestRunner* Runner = F.Runner();
    ABoardTile* Ally = F.Board->GetTile(0, 0);
    ABoardTile* Extra = F.Board->GetTile(0, 1);
    ABoardTile* Enemy = F.Board->GetTile(5, 4);
    if (!Ally || !Extra || !Enemy) { AddError(TEXT("Test board did not generate")); return false; }
    TestTrue(TEXT("Panel rows come from the configured table"), Runner->GetSpawnRows() == TArray<FName>{TEXT("Strong"), TEXT("Weak")});
    TestFalse(TEXT("Empty board cannot start"), Runner->StartLineup());
    TestFalse(TEXT("Unknown DT row creates no actor"), Runner->SpawnOnTile(TEXT("Missing"), 1, Ally));
    if (!TestTrue(TEXT("Place two-star ally from row picker"), Runner->SpawnOnTile(TEXT("Strong"), 2, Ally))
        || !TestTrue(TEXT("Place extra ally"), Runner->SpawnOnTile(TEXT("Weak"), 1, Extra))) return false;
    TestEqual(TEXT("Chosen star level used"), Runner->AllyUnit->StarLevel, 2);
    TestTrue(TEXT("Ally occupies the selected square"), Ally->OccupiedUnit == Runner->AllyUnit);
    TestFalse(TEXT("Cannot overwrite an occupied square"), Runner->SpawnOnTile(TEXT("Weak"), 1, Ally));
    TestTrue(TEXT("Remove owned ally from chosen tile"), Runner->RemoveFromTile(Extra));
    TestFalse(TEXT("Single team cannot start"), Runner->StartLineup());
    if (!TestTrue(TEXT("Enemy square chooses the other team"), Runner->SpawnOnTile(TEXT("Weak"), 1, Enemy))) return false;
    TestEqual(TEXT("Enemy actor team"), Runner->EnemyUnit->TeamType, EBattleTeam::Enemy);
    TestTrue(TEXT("Source DT remains unchanged"), F.Data->FindRow<FUnitDefinition>(TEXT("Strong"), TEXT("test"))->BasicAttack.bUseAnimNotify);
    TestTrue(TEXT("Fight uses selected lineup without respawn"), Runner->StartLineup());
    TestEqual(TEXT("Both selected actors remain"), F.Units(), 2);
    TestFalse(TEXT("Cannot place while combat runs"), Runner->SpawnOnTile(TEXT("Strong"), 1, Extra));
    TestTrue(TEXT("Selected teams fight"), F.Until(Runner, EUnitCombatTestState::Finished));
    Runner->StopTest();
    TestTrue(TEXT("Clear resets the board and allows editing again"), Runner->CanEditLineup() && F.EmptyBoard() && F.Units() == 0);
    return true;
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
// 실제 에셋의 뼈/모션은 읽기만 하고 몽타주와 DT는 메모리에서 만든다.
// 렌더링 검증 대신 소환 대기와 전투 종료 시 사망 몽타주 보존을 검사한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitRunnerAnimationTest, "ProjectA.TestRunner.SpawnAndDeathMontages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitRunnerAnimationTest::RunTest(const FString& Parameters)
{
    using namespace UnitRunnerTests;
    FFixture F;
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Kaykit/Mage/Mage.Mage"));
    UAnimSequence* Spawn = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Art/Kaykit/Animations/Rig_Medium_GeneralSpawn_Ground.Rig_Medium_GeneralSpawn_Ground"));
    UAnimSequence* Death = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Art/Kaykit/Animations/Rig_Medium_GeneralDeath_A.Rig_Medium_GeneralDeath_A"));
    if (!TestNotNull(TEXT("Mage mesh"), Mesh) || !TestNotNull(TEXT("Spawn sequence"), Spawn) || !TestNotNull(TEXT("Death sequence"), Death)) return false;
    FUnitDefinition Data = *F.Data->FindRow<FUnitDefinition>(TEXT("Weak"), TEXT("animation test"));
    Data.Visuals.SkeletalMesh = Mesh;
    Data.Animations.AnimClass = UAnimInstance::StaticClass();
    Data.Animations.SpawnMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Spawn, TEXT("DefaultSlot"));
    Data.Animations.DeathMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Death, TEXT("DefaultSlot"));
    Data.Visuals.MainHand.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Kaykit/Weapons/sword_1handed.sword_1handed"));
    Data.Visuals.MainHand.SocketName = TEXT("Weapon_R");
    Data.Visuals.OffHand.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Kaykit/Weapons/shield_badge.shield_badge"));
    Data.Visuals.OffHand.SocketName = TEXT("Weapon_L");
    Data.BaseStats.AttackDamage = 0.f;
    F.Data->AddRow(TEXT("Animated"), Data);
    AUnitCombatTestRunner* Runner = F.Runner();
    Runner->Ally.RowName = Runner->Enemy.RowName = TEXT("Animated");
    Runner->StartTest();
    TestTrue(TEXT("Spawn enters preparation"), F.Until(Runner, EUnitCombatTestState::Preparing));
    if (!IsValid(Runner->EnemyUnit)) return false;
    UAnimInstance* Anim = Runner->EnemyUnit->Body->GetAnimInstance();
    if (!TestNotNull(TEXT("Animation instance"), Anim)) return false;
    TestTrue(TEXT("Spawn montage is playing"), Anim->Montage_IsPlaying(Data.Animations.SpawnMontage));
    F.Tick(0.1f);
    TestTrue(TEXT("Spawn delays combat despite StartDelay zero"), Runner->TestState == EUnitCombatTestState::Preparing);
    TestTrue(TEXT("Combat starts after spawn"), F.Until(Runner, EUnitCombatTestState::Running));
    FUnitDamageRequest Hit; Hit.Attacker = Runner->AllyUnit; Hit.RawDamage = 10000.f; Hit.Type = EUnitDamageType::TrueDamage;
    Runner->EnemyUnit->ReceiveDamage(Hit);
    TestTrue(TEXT("Death montage starts"), Anim->Montage_IsPlaying(Data.Animations.DeathMontage));
    FAnimMontageInstance* DeathInstance = Anim->GetActiveInstanceForMontage(Data.Animations.DeathMontage);
    if (!TestNotNull(TEXT("Death instance"), DeathInstance)) return false;
    TestFalse(TEXT("Death holds last pose per instance"), DeathInstance->bEnableAutoBlendOut);
    TestTrue(TEXT("Source montage auto blend setting unchanged"), Data.Animations.DeathMontage->bEnableAutoBlendOut);
    F.Tick();
    TestTrue(TEXT("Team defeat finishes test"), Runner->TestState == EUnitCombatTestState::Finished);
    TestTrue(TEXT("StopCombat preserves playing death montage"), Anim->Montage_IsPlaying(Data.Animations.DeathMontage));
    F.Tick(0.2f);
    TestTrue(TEXT("Death montage continues beyond stop blend-out"), Anim->Montage_IsPlaying(Data.Animations.DeathMontage));
    // 전투 종료 후에도 사망 연출 시간이 흐르고, 모션 종료 직후에는 아직 페이드하지 않는다.
    for (float Elapsed = 0.25f; Elapsed < Death->GetPlayLength() + 0.2f; Elapsed += 0.05f) F.Tick();
    TestEqual(TEXT("Death pose is visible during hold"), Runner->EnemyUnit->DeathOpacity, 1.f);
    TestFalse(TEXT("Corpse remains visible during hold"), Runner->EnemyUnit->IsHidden());
    Runner->EnemyUnit->StopCombat();
    TestTrue(TEXT("Repeated stop keeps held death montage active"), Anim->Montage_IsActive(Data.Animations.DeathMontage));
    F.Tick(0.5f);
    const float Opacity = Runner->EnemyUnit->DeathOpacity;
    TestTrue(TEXT("Corpse fades after hold even outside combat"), Opacity > 0.f && Opacity < 1.f);
    for (UMeshComponent* Part : {static_cast<UMeshComponent*>(Runner->EnemyUnit->Body.Get()), static_cast<UMeshComponent*>(Runner->EnemyUnit->MainHand.Get()), static_cast<UMeshComponent*>(Runner->EnemyUnit->OffHand.Get())})
    {
        UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0));
        if (!TestNotNull(TEXT("Body and weapons have fade instances"), Material)) return false;
        TestTrue(TEXT("Body and weapons fade together"), FMath::IsNearlyEqual(Material->K2_GetScalarParameterValue(TEXT("UnitOpacity")), Opacity));
    }
    UMaterialInstanceDynamic* SurvivorMaterial = Cast<UMaterialInstanceDynamic>(Runner->AllyUnit->Body->GetMaterial(0));
    if (!TestNotNull(TEXT("Survivor has separate material"), SurvivorMaterial)) return false;
    TestEqual(TEXT("Survivor opacity unchanged"), SurvivorMaterial->K2_GetScalarParameterValue(TEXT("UnitOpacity")), 1.f);
    for (int32 Index = 0; Index < 20; ++Index) F.Tick();
    TestTrue(TEXT("Fully faded corpse hidden without destroying actor"), IsValid(Runner->EnemyUnit) && Runner->EnemyUnit->IsHidden());
    TestEqual(TEXT("Fade completes at zero"), Runner->EnemyUnit->DeathOpacity, 0.f);
    TestTrue(TEXT("Dead unit restores for next round"), Runner->EnemyUnit->ResetForRound());
    TestFalse(TEXT("Round reset shows unit"), Runner->EnemyUnit->IsHidden());
    TestEqual(TEXT("Round reset restores opacity"), Runner->EnemyUnit->DeathOpacity, 1.f);
    TestFalse(TEXT("Round reset clears death pose"), Runner->EnemyUnit->Body->GetAnimInstance()->Montage_IsActive(Data.Animations.DeathMontage));
    for (int32 Index = 0; Index < 40; ++Index) F.Tick();
    TestFalse(TEXT("Old death fade cannot hide restored unit"), Runner->EnemyUnit->IsHidden());
    Runner->EnemyUnit->DeathPoseHoldSeconds = Runner->EnemyUnit->DeathFadeSeconds = 0.f;
    Runner->EnemyUnit->Definition.Animations.DeathMontage = nullptr;
    Runner->EnemyUnit->StartCombat();
    Hit.DamageId = FGuid::NewGuid(); Runner->EnemyUnit->ReceiveDamage(Hit); F.Tick();
    TestTrue(TEXT("Missing montage and zero timings hide immediately on next tick"), Runner->EnemyUnit->IsHidden());
    Runner->bPlaySpawnAnimations = false;
    Runner->RestartTest();
    TestTrue(TEXT("Spawn animation can be disabled"), F.Until(Runner, EUnitCombatTestState::Running));
    TestFalse(TEXT("Disabled spawn is not playing"), Runner->EnemyUnit->Body->GetAnimInstance()->Montage_IsPlaying(Data.Animations.SpawnMontage));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitRunnerMultiTest, "ProjectA.TestRunner.MultipleUnits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitRunnerMultiTest::RunTest(const FString& Parameters)
{
    using namespace UnitRunnerTests;
    FFixture F; AUnitCombatTestRunner* Runner = F.Runner();
    // 첫 사망 후에도 남은 팀원이 전투를 계속하는지 타이머 공격 없이 직접 확인한다.
    Runner->bUseTimerAttackForTest = false;
    FUnitCombatTestSide Extra; Extra.RowName = TEXT("Strong"); Extra.Row = 0; Extra.Column = 1; Extra.Stars = 2;
    Runner->AdditionalAllies.Add(Extra);
    Extra.RowName = TEXT("Weak"); Extra.Row = 5; Extra.Column = 3; Extra.Stars = 1;
    Runner->AdditionalEnemies.Add(Extra);
    Runner->StartTest();
    TestTrue(TEXT("Two versus two starts"), F.Until(Runner, EUnitCombatTestState::Running));
    TestEqual(TEXT("Four units spawned"), F.Units(), 4);
    ABattleUnitBase* RemainingEnemy = Cast<ABattleUnitBase>(F.Board->GetTile(5,3)->OccupiedUnit);
    ABattleUnitBase* ExtraAlly = Cast<ABattleUnitBase>(F.Board->GetTile(0,1)->OccupiedUnit);
    if (!TestNotNull(TEXT("Additional enemy placed"), RemainingEnemy) || !TestNotNull(TEXT("Additional ally placed"), ExtraAlly)) return false;
    TestEqual(TEXT("Additional ally stars applied"), ExtraAlly->StarLevel, 2);
    TestTrue(TEXT("Additional units enter combat"), ExtraAlly->bInCombat && RemainingEnemy->bInCombat);
    FUnitDamageRequest Hit; Hit.Attacker = Runner->AllyUnit; Hit.RawDamage = 10000.f; Hit.Type = EUnitDamageType::TrueDamage;
    Runner->EnemyUnit->ReceiveDamage(Hit); F.Tick();
    TestTrue(TEXT("First death does not finish team battle"), Runner->TestState == EUnitCombatTestState::Running && RemainingEnemy->bInCombat);
    Hit.DamageId = FGuid::NewGuid(); RemainingEnemy->ReceiveDamage(Hit); F.Tick();
    TestTrue(TEXT("Last enemy death finishes team battle"), Runner->TestState == EUnitCombatTestState::Finished);
    TestFalse(TEXT("Additional units stop too"), ExtraAlly->bInCombat || RemainingEnemy->bInCombat);
    TestTrue(TEXT("All team tiles released"), F.EmptyBoard());
    Runner->RestartTest();
    TestTrue(TEXT("Multiple unit restart works"), F.Until(Runner, EUnitCombatTestState::Running));
    TestEqual(TEXT("Restart retains only four units"), F.Units(), 4);
    Runner->StopTest(); TestEqual(TEXT("Stop cleans all units"), F.Units(), 0);
    Runner->AdditionalAllies[0].Row = Runner->Ally.Row;
    Runner->AdditionalAllies[0].Column = Runner->Ally.Column;
    Runner->StartTest(); F.Until(Runner, EUnitCombatTestState::Running);
    TestTrue(TEXT("Duplicate start tile fails"), Runner->TestState == EUnitCombatTestState::Failed);
    TestEqual(TEXT("Duplicate tile leaves no units"), F.Units(), 0);
    Runner->AdditionalAllies[0].Column = 1;
    Runner->AdditionalEnemies[0].RowName = TEXT("Missing");
    Runner->StartTest(); F.Until(Runner, EUnitCombatTestState::Running);
    TestTrue(TEXT("Invalid additional row fails"), Runner->TestState == EUnitCombatTestState::Failed);
    TestEqual(TEXT("Invalid additional row leaves no units"), F.Units(), 0);
    return true;
}
// 현재 DT에 실제 지정된 몸체/머티리얼 오버라이드/무기도 페이드 가능한지 검증한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitDeathMaterialWiringTest, "ProjectA.Unit.DeathFadeMaterialWiring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitDeathMaterialWiringTest::RunTest(const FString& Parameters)
{
    UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Units.DT_Units"));
    if (!TestNotNull(TEXT("Current unit table"), Table)) return false;
    const auto CheckMaterial = [&](UMaterialInterface* Material, const FString& Label)
    {
        float Value = 0.f;
        TestTrue(Label + TEXT(" supports UnitOpacity"), Material
            && Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("UnitOpacity")), Value));
        TestEqual(Label + TEXT(" defaults to visible"), Value, 1.f);
    };
    for (FName Name : {FName(TEXT("Knight")), FName(TEXT("Mage"))})
    {
        const FUnitDefinition* Row = Table->FindRow<FUnitDefinition>(Name, TEXT("Death material wiring"), false);
        if (!TestNotNull(Name.ToString() + TEXT(" row"), Row)) return false;
        USkeletalMesh* Mesh = Row->Visuals.SkeletalMesh;
        if (!TestNotNull(Name.ToString() + TEXT(" body mesh"), Mesh)) return false;
        for (int32 Index = 0; Index < Mesh->GetMaterials().Num(); ++Index)
        {
            UMaterialInterface* Material = Row->Visuals.MaterialOverrides.IsValidIndex(Index) && Row->Visuals.MaterialOverrides[Index]
                ? Row->Visuals.MaterialOverrides[Index].Get() : Mesh->GetMaterials()[Index].MaterialInterface.Get();
            CheckMaterial(Material, FString::Printf(TEXT("%s body %d"), *Name.ToString(), Index));
        }
        int32 HandIndex = 0;
        for (const FUnitWeaponVisual& Hand : {Row->Visuals.MainHand, Row->Visuals.OffHand})
        {
            if (Hand.Mesh)
                for (int32 Index = 0; Index < Hand.Mesh->GetStaticMaterials().Num(); ++Index)
                    CheckMaterial(Hand.Mesh->GetMaterial(Index), FString::Printf(TEXT("%s hand %d material %d"), *Name.ToString(), HandIndex, Index));
            ++HandIndex;
        }
    }
    return true;
}
#endif
