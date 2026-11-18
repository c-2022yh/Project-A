#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../Skills/UnitSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardTile.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DataTable.h"

namespace UnitSkillTests
{
    struct FFixture
    {
        UWorld* World;
        UDataTable* Table;
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Table = NewObject<UDataTable>(World); Table->RowStruct = FUnitDefinition::StaticStruct();
        }
        ~FFixture() { World->BeginTearingDown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        static FUnitDefinition Data()
        {
            FUnitDefinition Result; Result.BaseStats.MaxHealth = 1000.f; Result.BaseStats.AttackDamage = 10.f;
            Result.BaseStats.CriticalChance = 0.f; Result.BaseStats.AttackRange = 4;
            Result.BaseStats.MovementSpeed = 0.f; return Result;
        }
        ABattleUnitBase* Spawn(FName Name, const FUnitDefinition& Data, EBattleTeam Team, int32 Q)
        {
            Table->AddRow(Name, Data);
            ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
            FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name;
            FString Error; if (!Unit->InitializeUnit(Handle, Team, 1, Error)) return nullptr;
            ABoardTile* Tile = World->SpawnActor<ABoardTile>(); Tile->HexQ = Q; Tile->HexR = 0;
            Tile->TeamType = Team == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
            Tile->SetActorLocation(FVector(Q * 500.f, 0.f, 0.f)); return Unit->PlaceOnTile(Tile) ? Unit : nullptr;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSkillCompositionTest, "ProjectA.Unit.Skills.CompositionAndIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitSkillCompositionTest::RunTest(const FString& Parameters)
{
    using namespace UnitSkillTests;
    FFixture F; FUnitDefinition Data = FFixture::Data();
    Data.SkillClasses = {UKnightShieldSkill::StaticClass(), UHitGrowthPassiveSkill::StaticClass()};
    ABattleUnitBase* A = F.Spawn(TEXT("A"), Data, EBattleTeam::Player, 0);
    ABattleUnitBase* B = F.Spawn(TEXT("B"), Data, EBattleTeam::Player, 1);
    ABattleUnitBase* Enemy = F.Spawn(TEXT("Enemy"), FFixture::Data(), EBattleTeam::Enemy, 2);
    if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B) || !TestNotNull(TEXT("Enemy"), Enemy)) return false;
    const TArray<UUnitSkillBase*> ASkills = A->GetEquippedSkills(), BSkills = B->GetEquippedSkills();
    TestEqual(TEXT("Active and passive equipped together"), ASkills.Num(), 2);
    TestTrue(TEXT("Each unit owns a separate instance"), ASkills[0] != BSkills[0] && ASkills[1] != BSkills[1]);
    TestTrue(TEXT("Instance Outer is unit"), ASkills[0]->GetOuter() == A && ASkills[0]->OwnerUnit == A);
    TestTrue(TEXT("Defaults do not have an owner"), GetDefault<UHitGrowthPassiveSkill>()->OwnerUnit == nullptr);
    A->StartCombat(); B->StartCombat(); Enemy->StartCombat();
    const float BaseAttack = A->FinalStats.AttackDamage;
    // 평타를 실제로 적중시켜 이벤트→패시브→성장 경로를 검사한다.
    for (int32 Hit = 0; Hit < 3; ++Hit)
    {
        if (Hit > 0) A->Tick(1.1f);
        A->CurrentTarget = Enemy;
        if (A->ActionState == EUnitActionState::Idle) A->TryBasicAttack();
        const int32 Id = A->ActiveActionId;
        TestTrue(TEXT("Basic hit commits"), A->CommitAttackHit(Id)); A->FinishAction(Id);
    }
    TestEqual(TEXT("Passive received all three hits"), ASkills[1]->Counter, 3);
    TestEqual(TEXT("Growth applied once"), A->FinalStats.AttackDamage, BaseAttack + 1.f);
    TestEqual(TEXT("Other unit counter isolated"), BSkills[1]->Counter, 0);
    TestEqual(TEXT("Class default counter unchanged"), GetDefault<UHitGrowthPassiveSkill>()->Counter, 0);
    A->GainMana(1000.f);
    TestTrue(TEXT("Active can coexist with passive"), A->TryCastSkill());
    TestEqual(TEXT("Mana consumed at cast start"), A->CurrentMana, 0.f);
    const int32 CastId = A->ActiveActionId;
    FUnitStatModifier Stun; Stun.bStun = true; A->AddModifier(TEXT("Stun"), Stun, 2.f);
    TestTrue(TEXT("Ordinary cast survives stun"), A->CommitSkillEffect(CastId));
    TestEqual(TEXT("Shield applied once"), A->CurrentShield, 30.f);
    TestFalse(TEXT("Duplicate notify rejected"), A->CommitSkillEffect(CastId));
    A->FinishAction(CastId);
    TestFalse(TEXT("Cannot start another cast while stunned"), A->TryCastSkill());
    A->StopCombat(); B->StopCombat(); Enemy->StopCombat();
    TestEqual(TEXT("Round clears passive counter"), ASkills[1]->Counter, 0);
    TestEqual(TEXT("Permanent growth survives stop"), A->PermanentGrowth.AttackDamage, 1.f);
    TestTrue(TEXT("Reset succeeds"), A->ResetForRound());
    TestEqual(TEXT("Reset keeps permanent growth"), A->FinalStats.AttackDamage, BaseAttack + 1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSkillValidationTest, "ProjectA.Unit.Skills.ClassValidationAndPassiveFilter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitSkillValidationTest::RunTest(const FString& Parameters)
{
    using namespace UnitSkillTests;
    FFixture F; FUnitDefinition Data = FFixture::Data();
    ABattleUnitBase* Unit = F.World->SpawnActor<ABattleUnitBase>();
    FDataTableRowHandle Handle; Handle.DataTable = F.Table; Handle.RowName = TEXT("Validation"); FString Error;
    const auto Initialize = [&]() { F.Table->AddRow(Handle.RowName, Data); return Unit->InitializeUnit(Handle, EBattleTeam::Player, 1, Error); };
    Data.SkillClasses = {UKnightShieldSkill::StaticClass(), UMageFireballSkill::StaticClass()};
    TestFalse(TEXT("Two actives rejected"), Initialize());
    Data.SkillClasses = {UUnitActiveSkill::StaticClass()}; TestFalse(TEXT("Abstract skill rejected"), Initialize());
    Data.SkillClasses = {nullptr}; TestFalse(TEXT("Null skill rejected"), Initialize());
    Data.SkillClasses = {UHitGrowthPassiveSkill::StaticClass(), UHitGrowthPassiveSkill::StaticClass()};
    TestFalse(TEXT("Duplicate IDs rejected"), Initialize());
    Data.SkillClasses = {UHitGrowthPassiveSkill::StaticClass()}; TestTrue(TEXT("Passive-only unit accepted"), Initialize());
    TestFalse(TEXT("Passive-only unit has no mana"), Unit->UsesMana());
    UUnitPassiveSkill* Passive = CastChecked<UUnitPassiveSkill>(Unit->GetEquippedSkills()[0]);
    FUnitDamageRequest Context; FUnitDamageResult Result; Context.Source = EUnitDamageSource::Triggered;
    TestFalse(TEXT("Triggered damage cannot activate passive by default"), Passive->ShouldActivate(EUnitTrigger::BasicAttackHit, Context, Result));
    TestEqual(TEXT("Filtered event does not count"), Passive->Counter, 0);
    Context.Source = EUnitDamageSource::BasicAttack;
    Context.TriggerChain.Add(Passive->Settings.SkillId);
    Passive->bAllowTriggeredEvents = true;
    TestFalse(TEXT("Same skill cannot reenter its own chain"), Passive->ShouldActivate(EUnitTrigger::BasicAttackHit, Context, Result));
    TestEqual(TEXT("Cycle event does not count"), Passive->Counter, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSkillBehaviorParityTest, "ProjectA.Unit.Skills.BehaviorBaseline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitSkillBehaviorParityTest::RunTest(const FString& Parameters)
{
    using namespace UnitSkillTests;
    UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/Units/DT_Units.DT_Units"));
    if (!TestNotNull(TEXT("DT"), Table)) return false;
    const auto Run = [this](TSubclassOf<UUnitSkillBase> Class, int32 Stars)
    {
        FFixture F; FUnitDefinition Data = FFixture::Data();
        Data.SkillClasses.Add(Class);
        ABattleUnitBase* A = F.Spawn(TEXT("Caster"), Data, EBattleTeam::Player, 0);
        ABattleUnitBase* B = F.Spawn(TEXT("Target"), FFixture::Data(), EBattleTeam::Enemy, 1);
        ABattleUnitBase* C = F.Spawn(TEXT("AreaTarget"), FFixture::Data(), EBattleTeam::Enemy, 2);
        if (!A || !B || !C) { AddError(TEXT("Parity fixture initialization failed")); return TArray<float>(); }
        A->SetStarLevel(Stars); A->SetCombatRoster({A, B, C});
        A->StartCombat(); B->StartCombat(); C->StartCombat(); A->CurrentTarget = B;
        A->GainMana(1000.f);
        TestTrue(TEXT("Configured active starts"), A->TryCastSkill());
        const int32 Id = A->ActiveActionId;
        TestTrue(TEXT("Configured active commits"), A->CommitSkillEffect(Id)); A->FinishAction(Id);
        if (Class->GetDefaultObject<UUnitSkillBase>()->Settings.bProjectile) A->Tick(0.6f);
        else if (Class->GetDefaultObject<UUnitSkillBase>()->IsA<URogueWeakPointSkill>())
        {
            A->TryBasicAttack(); A->CommitAttackHit(A->ActiveActionId); A->FinishAction(A->ActiveActionId);
        }
        return TArray<float>{A->CurrentShield, A->FinalStats.AttackSpeed, B->CurrentHP, C->CurrentHP, A->CurrentMana};
    };
    for (FName Name : {FName(TEXT("Knight")), FName(TEXT("Mage")), FName(TEXT("Archer")), FName(TEXT("Rogue"))})
    {
        const FUnitDefinition* Row = Table->FindRow<FUnitDefinition>(Name, TEXT("Parity"));
        if (!Row || Row->SkillClasses.Num() != 1) { AddError(TEXT("Migrated skill class missing")); continue; }
        for (int32 Stars : {1, 2})
        {
            // DT 정리 전 확인한 결과. 검사 유닛은 AD=10/18, 기본 공속=1, 적 HP=1000이다.
            // 보호막/공속/주 대상 HP/범위 대상 HP/남은 마나 순서로 비교한다.
            TArray<float> Expected;
            if (Name == TEXT("Knight")) Expected = {Stars == 1 ? 30.f : 54.f, 1.f, 1000.f, 1000.f, 0.f};
            else if (Name == TEXT("Mage")) Expected = {0.f, 1.f, Stars == 1 ? 900.f : 676.f, Stars == 1 ? 900.f : 676.f, 0.f};
            else if (Name == TEXT("Archer")) Expected = {0.f, Stars == 1 ? 1.5f : 1.8f, 1000.f, 1000.f, 0.f};
            else Expected = {0.f, 1.f, Stars == 1 ? 980.f : 964.f, 1000.f, 10.f};
            const TArray<float> Migrated = Run(Row->SkillClasses[0], Stars);
            if (!TestEqual(TEXT("Same observed values"), Migrated.Num(), Expected.Num())) continue;
            for (int32 Index = 0; Index < Migrated.Num(); ++Index)
                TestTrue(*FString::Printf(TEXT("%s %d-star outcome[%d] preserved"), *Name.ToString(), Stars, Index), FMath::IsNearlyEqual(Migrated[Index], Expected[Index]));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSkillProjectileLifetimeTest, "ProjectA.Unit.Skills.ProjectileSnapshotAndCasterDeath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitSkillProjectileLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace UnitSkillTests;
    FFixture F; FUnitDefinition Data = FFixture::Data(); Data.SkillClasses = {UMageFireballSkill::StaticClass()};
    ABattleUnitBase* A = F.Spawn(TEXT("Mage"), Data, EBattleTeam::Player, 0);
    ABattleUnitBase* B = F.Spawn(TEXT("Enemy"), FFixture::Data(), EBattleTeam::Enemy, 1);
    if (!A || !B) return false;
    A->StartCombat(); B->StartCombat(); A->CurrentTarget = B; A->GainMana(1000.f);
    TestTrue(TEXT("Cast starts"), A->TryCastSkill());
    TestTrue(TEXT("Projectile launched"), A->CommitSkillEffect(A->ActiveActionId));
    TestEqual(TEXT("No damage at launch"), B->CurrentHP, 1000.f);
    // 발사된 탄은 설정/능력치 변경과 시전자 사망에 영향받지 않는다.
    A->GetEquippedSkills()[0]->Settings.Effects[0].BaseValue = 9999.f;
    FUnitDamageRequest Lethal; Lethal.Attacker = B; Lethal.RawDamage = 10000.f;
    A->ReceiveDamage(Lethal); TestTrue(TEXT("Caster died"), A->bIsDead);
    A->Tick(0.6f);
    TestEqual(TEXT("Launched skill survives caster death with original damage"), B->CurrentHP, 900.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitSkillAssetTest, "ProjectA.Unit.Skills.MigratedAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitSkillAssetTest::RunTest(const FString& Parameters)
{
    using namespace UnitSkillTests;
    FFixture F;
    UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/Units/DT_Units.DT_Units"));
    if (!TestNotNull(TEXT("DT_Units"), Table)) return false;
    for (FName Name : {FName(TEXT("Knight")), FName(TEXT("Mage")), FName(TEXT("Archer")), FName(TEXT("Rogue"))})
    {
        const FUnitDefinition* Row = Table->FindRow<FUnitDefinition>(Name, TEXT("SkillAssetTest"));
        if (!TestNotNull(*Name.ToString(), Row)) continue;
        TestTrue(TEXT("DT stores one skill class"), Row->SkillClasses.Num() == 1);
        TestNull(TEXT("Legacy skill field removed from schema"), FUnitDefinition::StaticStruct()->FindPropertyByName(TEXT("Skills")));
        ABattleUnitBase* Unit = F.World->SpawnActor<ABattleUnitBase>();
        FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name; FString Error;
        TestTrue(*FString::Printf(TEXT("%s initializes: %s"), *Name.ToString(), *Error), Unit->InitializeUnit(Handle, EBattleTeam::Player, 1, Error));
        TestEqual(TEXT("Exactly one runtime skill"), Unit->GetEquippedSkills().Num(), 1);
        TestTrue(TEXT("Active unit uses mana"), Unit->UsesMana());
    }
    return true;
}
#endif
