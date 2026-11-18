#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../Skills/HeroStrikeSkill.h"
#include "../Skills/HeroGrowthPassiveSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardTile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

namespace HeroStrikeTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        UDataTable* Table = nullptr;
        ABattleUnitBase* Hero = nullptr;
        ABattleUnitBase* Enemy = nullptr;
        FFixture(int32 Stars = 1, bool bCritical = false, bool bProjectile = false, float Speed = 1000.f,
            float BasicRatio = 1.f, float BasicFlat = 0.f, bool bGrowth = false)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Table = NewObject<UDataTable>(World); Table->RowStruct = FUnitDefinition::StaticStruct();
            FUnitDefinition Data;
            Data.BaseStats.MaxHealth = 500.f; Data.BaseStats.AttackDamage = 40.f;
            Data.BaseStats.AbilityPower = 100.f; Data.BaseStats.AttackSpeed = 0.75f;
            Data.BaseStats.AttackRange = bProjectile ? 4 : 1; Data.BaseStats.MovementSpeed = 0.f;
            Data.BaseStats.MaxMana = 50.f; Data.BaseStats.CriticalChance = bCritical ? 1.f : 0.f;
            Data.SkillClasses = {UHeroStrikeSkill::StaticClass()};
            if (bGrowth) Data.SkillClasses.Add(UHeroGrowthPassiveSkill::StaticClass());
            Data.BasicAttack.Delivery = bProjectile ? EUnitAttackDelivery::Projectile : EUnitAttackDelivery::Melee;
            Data.BasicAttack.ProjectileSpeed = Speed; Data.BasicAttack.ProjectileMaxLifetime = 20.f;
            Data.BasicAttack.AttackDamageRatio = BasicRatio; Data.BasicAttack.FlatDamage = BasicFlat;
            Hero = Spawn(TEXT("Hero"), Data, EBattleTeam::Player, 0);
            FUnitDefinition Target; Target.BaseStats.MaxHealth = 1000.f;
            Target.BaseStats.MovementSpeed = 0.f; Target.BaseStats.CriticalChance = 0.f;
            Enemy = Spawn(TEXT("Enemy"), Target, EBattleTeam::Enemy, 1);
            if (Hero && Enemy)
            {
                Hero->SetStarLevel(Stars); Hero->SetCombatRoster({Hero, Enemy}); Enemy->SetCombatRoster({Hero, Enemy});
                Hero->StartCombat(); Enemy->StartCombat(); Hero->CurrentHP = 100.f;
            }
        }
        ~FFixture()
        {
            World->BeginTearingDown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
        ABattleUnitBase* Spawn(FName Name, const FUnitDefinition& Data, EBattleTeam Team, int32 Q)
        {
            Table->AddRow(Name, Data);
            ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
            FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name; FString Error;
            if (!Unit->InitializeUnit(Handle, Team, 1, Error)) return nullptr;
            ABoardTile* Tile = World->SpawnActor<ABoardTile>(); Tile->HexQ = Q; Tile->HexR = 0;
            Tile->TeamType = Team == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
            Tile->SetActorLocation(FVector(Q * 500.f, 0.f, 0.f));
            return Unit->PlaceOnTile(Tile) ? Unit : nullptr;
        }
        UHeroStrikeSkill* Skill() const { return Cast<UHeroStrikeSkill>(Hero->GetEquippedSkills()[0]); }
        bool Arm()
        {
            Hero->GainMana(50.f);
            if (!Hero->TryCastSkill()) return false;
            const int32 Id = Hero->ActiveActionId;
            const bool bApplied = Hero->CommitSkillEffect(Id);
            Hero->FinishAction(Id);
            return bApplied && Hero->HasArmedBasicAttack(Skill());
        }
        bool Hit(ABattleUnitBase* Target = nullptr)
        {
            Hero->CurrentTarget = Target ? Target : Enemy;
            if (Hero->ActionState == EUnitActionState::Idle && !Hero->TryBasicAttack()) return false;
            const int32 Id = Hero->ActiveActionId;
            const bool bApplied = Hero->CommitAttackHit(Id);
            Hero->FinishAction(Id);
            return bApplied;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroStrikeEvolutionTest, "ProjectA.Unit.HeroStrike.EvolutionAfterPreparation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroStrikeEvolutionTest::RunTest(const FString& Parameters)
{
    using namespace HeroStrikeTests;
    FFixture F(1, false, false, 1000.f, 1.f, 0.f, true);
    if (!F.Hero || !F.Enemy) return false;
    auto* Growth = Cast<UHeroGrowthPassiveSkill>(F.Hero->GetEquippedSkills()[1]);
    if (!TestNotNull(TEXT("Growth skill"), Growth)) return false;
    Growth->RequiredParticipations = 1;

    TestTrue(TEXT("Arm normal strike"), F.Arm());
    TestTrue(TEXT("Normal strike hits"), F.Hit());
    TestEqual(TEXT("Normal strike uses 200% AD"), F.Enemy->CurrentHP, 920.f);
    FUnitDamageRequest Finisher;
    Finisher.Attacker = F.Hero;
    Finisher.RawDamage = 10000.f;
    Finisher.Type = EUnitDamageType::TrueDamage;
    TestTrue(TEXT("Kill reaches threshold"), F.Enemy->ReceiveDamage(Finisher).bKilled);
    TestTrue(TEXT("Growth is ready"), Growth->IsEvolutionReady());
    TestFalse(TEXT("Cannot evolve during combat"), F.Hero->PrepareSkillsForRound());
    F.Hero->StopCombat();
    F.Enemy->StopCombat();
    TestFalse(TEXT("Settlement stop preserves the pending evolution"), Growth->bEvolved);
    if (!TestTrue(TEXT("Hero resets into preparation"), F.Hero->ResetForRound())
        || !TestTrue(TEXT("Enemy resets into preparation"), F.Enemy->ResetForRound())) return false;
    TestTrue(TEXT("Preparation advances evolution"), Growth->bEvolved);
    TestFalse(TEXT("Repeated preparation does not evolve again"), F.Hero->PrepareSkillsForRound());

    F.Hero->StartCombat(); F.Enemy->StartCombat();
    F.Hero->CurrentHP = 100.f;
    TestTrue(TEXT("Arm evolved strike"), F.Arm());
    TestTrue(TEXT("Evolved strike hits"), F.Hit());
    TestEqual(TEXT("Evolved strike uses 300% AD"), F.Enemy->CurrentHP, 880.f);
    TestEqual(TEXT("Same 20% lifesteal uses evolved health damage"), F.Hero->CurrentHP, 124.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroStrikeDamageTest, "ProjectA.Unit.HeroStrike.DamageCriticalAndHealing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroStrikeDamageTest::RunTest(const FString& Parameters)
{
    using namespace HeroStrikeTests;
    for (int32 Stars : {1, 2})
    for (bool bCritical : {false, true})
    {
        FFixture F(Stars, bCritical);
        if (!F.Hero || !F.Enemy) return false;
        TestTrue(TEXT("Active arms next attack"), F.Arm());
        TestEqual(TEXT("Mana consumed at cast start"), F.Hero->CurrentMana, 0.f);
        TestTrue(TEXT("Empowered hit"), F.Hit());
        const float ExpectedDamage = (Stars == 1 ? 40.f : 72.f) * 2.f * (bCritical ? 1.5f : 1.f);
        TestTrue(TEXT("Total AD ratio and shared critical multiplier applied exactly once"), FMath::IsNearlyEqual(F.Enemy->CurrentHP, 1000.f - ExpectedDamage));
        TestTrue(TEXT("Only actual health damage heals"), FMath::IsNearlyEqual(F.Hero->CurrentHP, 100.f + ExpectedDamage * 0.2f));
        TestFalse(TEXT("Consumed on actual hit"), F.Hero->HasArmedBasicAttack(F.Skill()));
        TestFalse(TEXT("Class default has no runtime owner"), IsValid(GetDefault<UHeroStrikeSkill>()->OwnerUnit));
    }
    // 스킬의 200%는 평타 배율/고정값에 추가로 곱하는 값이 아니다.
    FFixture F(1, false, false, 1000.f, 1.5f, 100.f);
    TestTrue(TEXT("Armed with a different basic damage formula"), F.Arm());
    TestTrue(TEXT("First hit"), F.Hit());
    TestEqual(TEXT("AD 200% is the total empowered raw damage"), F.Enemy->CurrentHP, 920.f);
    TestEqual(TEXT("First hit heals 16"), F.Hero->CurrentHP, 116.f);
    F.Hero->Tick(2.f);
    TestTrue(TEXT("Next ordinary attack"), F.Hit());
    TestEqual(TEXT("Ordinary basic formula remains unchanged"), F.Enemy->CurrentHP, 760.f);
    TestEqual(TEXT("Ordinary attack does not lifesteal"), F.Hero->CurrentHP, 116.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroStrikeActualDamageTest, "ProjectA.Unit.HeroStrike.MitigationShieldAndOverkill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroStrikeActualDamageTest::RunTest(const FString& Parameters)
{
    using namespace HeroStrikeTests;
    {
        FFixture F;
        F.Enemy->Definition.BaseStats.Armor = 100.f; F.Enemy->RecalculateStats();
        FUnitStatModifier Reduction; Reduction.DamageReduction = 0.5f;
        F.Enemy->AddModifier(TEXT("Reduction"), Reduction, 20.f);
        F.Enemy->AddShield(TEXT("Shield"), 10.f, 20.f);
        TestTrue(TEXT("Arm"), F.Arm()); TestTrue(TEXT("Hit"), F.Hit());
        TestEqual(TEXT("80 raw -> 40 armor -> 20 reduction -> 10 health damage"), F.Enemy->CurrentHP, 990.f);
        TestEqual(TEXT("Heal excludes absorbed and mitigated damage"), F.Hero->CurrentHP, 102.f);
    }
    {
        FFixture F;
        F.Enemy->AddShield(TEXT("Shield"), 200.f, 20.f);
        TestTrue(TEXT("Arm against shield"), F.Arm()); TestTrue(TEXT("Hit shield"), F.Hit());
        TestEqual(TEXT("Shield absorbs all damage"), F.Enemy->CurrentHP, 1000.f);
        TestEqual(TEXT("No healing from shield damage"), F.Hero->CurrentHP, 100.f);
        TestFalse(TEXT("Shield hit still consumes enhancement"), F.Hero->HasArmedBasicAttack(F.Skill()));
    }
    {
        FFixture F;
        F.Enemy->CurrentHP = 5.f;
        TestTrue(TEXT("Arm killing blow"), F.Arm()); TestTrue(TEXT("Killing blow"), F.Hit());
        TestTrue(TEXT("Target died"), F.Enemy->bIsDead);
        TestEqual(TEXT("Only five health damage counts, not 80 raw"), F.Hero->CurrentHP, 101.f);
    }
    {
        FFixture F; F.Hero->CurrentHP = 499.f;
        TestTrue(TEXT("Arm near full health"), F.Arm()); TestTrue(TEXT("Hit near full health"), F.Hit());
        TestEqual(TEXT("Healing capped at max health"), F.Hero->CurrentHP, 500.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroStrikeRetentionTest, "ProjectA.Unit.HeroStrike.CancellationAndRecast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroStrikeRetentionTest::RunTest(const FString& Parameters)
{
    using namespace HeroStrikeTests;
    FFixture F;
    TestTrue(TEXT("Arm"), F.Arm());
    F.Hero->GainMana(50.f);
    TestFalse(TEXT("No recast while armed"), F.Hero->TryCastSkill());
    TestEqual(TEXT("Rejected recast preserves mana"), F.Hero->CurrentMana, 50.f);
    F.Hero->CurrentTarget = F.Enemy;
    TestTrue(TEXT("Begin basic"), F.Hero->TryBasicAttack());
    const int32 CancelledId = F.Hero->ActiveActionId;
    FUnitStatModifier Stun; Stun.bStun = true;
    F.Hero->AddModifier(TEXT("Stun"), Stun, 2.f);
    TestFalse(TEXT("Cancelled notify rejected"), F.Hero->CommitAttackHit(CancelledId));
    TestTrue(TEXT("Stun cancellation retains enhancement"), F.Hero->HasArmedBasicAttack(F.Skill()));
    F.Hero->Tick(30.f);
    TestTrue(TEXT("No arbitrary timeout"), F.Hero->HasArmedBasicAttack(F.Skill()));
    const int32 Id = F.Hero->ActiveActionId;
    TestTrue(TEXT("Next successful hit"), F.Hero->CommitAttackHit(Id));
    const float HP = F.Hero->CurrentHP;
    TestFalse(TEXT("Duplicate notify cannot damage/heal again"), F.Hero->CommitAttackHit(Id));
    TestEqual(TEXT("No duplicate healing"), F.Hero->CurrentHP, HP);
    F.Hero->FinishAction(Id);
    TestTrue(TEXT("Can cast again after consumption"), F.Hero->TryCastSkill());
    F.Hero->CommitSkillEffect(F.Hero->ActiveActionId); F.Hero->FinishAction(F.Hero->ActiveActionId);
    F.Hero->Tick(2.f);
    const int32 MissingTargetId = F.Hero->ActiveActionId;
    FUnitDamageRequest Lethal; Lethal.Attacker = F.Hero; Lethal.RawDamage = 10000.f;
    F.Enemy->ReceiveDamage(Lethal);
    TestFalse(TEXT("Dead target cannot be hit"), F.Hero->CommitAttackHit(MissingTargetId));
    TestTrue(TEXT("Dead target retains enhancement"), F.Hero->HasArmedBasicAttack(F.Skill()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroStrikeLifecycleTest, "ProjectA.Unit.HeroStrike.DeathAndRoundReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroStrikeLifecycleTest::RunTest(const FString& Parameters)
{
    using namespace HeroStrikeTests;
    FFixture F;
    TestTrue(TEXT("Arm before death"), F.Arm());
    FUnitDamageRequest Lethal; Lethal.Attacker = F.Enemy; Lethal.RawDamage = 10000.f;
    F.Hero->ReceiveDamage(Lethal);
    TestFalse(TEXT("Death removes queued enhancement immediately"), F.Hero->HasArmedBasicAttack(F.Skill()));
    F.Hero->StopCombat(); F.Enemy->StopCombat();
    TestTrue(TEXT("Hero restored"), F.Hero->ResetForRound());
    TestTrue(TEXT("Enemy restored"), F.Enemy->ResetForRound());
    F.Hero->StartCombat(); F.Enemy->StartCombat();
    TestTrue(TEXT("Arm in next round"), F.Arm());
    F.Hero->StopCombat(); F.Enemy->StopCombat();
    TestFalse(TEXT("Round stop clears enhancement"), F.Hero->HasArmedBasicAttack(F.Skill()));
    F.Hero->ResetForRound(); F.Enemy->ResetForRound();
    F.Hero->StartCombat(); F.Enemy->StartCombat(); F.Hero->CurrentHP = 100.f;
    TestTrue(TEXT("Ordinary hit after reset"), F.Hit());
    TestEqual(TEXT("No stale damage buff"), F.Enemy->CurrentHP, 960.f);
    TestEqual(TEXT("No stale lifesteal"), F.Hero->CurrentHP, 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroStrikeProjectileTest, "ProjectA.Unit.HeroStrike.ProjectileClaimAndMiss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroStrikeProjectileTest::RunTest(const FString& Parameters)
{
    using namespace HeroStrikeTests;
    {
        FFixture F(1, false, true, 100.f);
        TestTrue(TEXT("Arm ranged hit"), F.Arm()); TestTrue(TEXT("Launch first"), F.Hit());
        TestEqual(TEXT("No healing at launch"), F.Hero->CurrentHP, 100.f);
        F.Hero->Tick(1.4f);
        TestTrue(TEXT("Launch ordinary second shot while first is in flight"), F.Hit());
        FUnitStatModifier Stun; Stun.bStun = true; F.Hero->AddModifier(TEXT("Stun"), Stun, 10.f);
        F.Hero->Tick(3.6f);
        TestEqual(TEXT("First shot enhanced"), F.Enemy->CurrentHP, 920.f);
        TestEqual(TEXT("Arrival heals once"), F.Hero->CurrentHP, 116.f);
        F.Hero->Tick(1.4f);
        TestEqual(TEXT("Second shot did not reuse enhancement"), F.Enemy->CurrentHP, 880.f);
        TestEqual(TEXT("Second shot does not heal"), F.Hero->CurrentHP, 116.f);
    }
    {
        FFixture F(1, false, true);
        TestTrue(TEXT("Arm"), F.Arm()); TestTrue(TEXT("Launch"), F.Hit());
        FUnitDamageRequest Kill; Kill.Attacker = F.Hero; Kill.RawDamage = 10000.f;
        F.Enemy->ReceiveDamage(Kill); F.Hero->Tick(0.1f);
        TestTrue(TEXT("Lost target releases claim but keeps enhancement"), F.Hero->HasArmedBasicAttack(F.Skill()));
        FUnitDefinition Target; Target.BaseStats.MaxHealth = 1000.f;
        ABattleUnitBase* Next = F.Spawn(TEXT("Next"), Target, EBattleTeam::Enemy, 1);
        if (!TestNotNull(TEXT("New target"), Next)) return false;
        Next->StartCombat(); F.Hero->Tick(2.f);
        TestTrue(TEXT("New target receives enhancement"), F.Hit(Next));
        F.Hero->Tick(0.6f);
        TestEqual(TEXT("Miss did not consume damage enhancement"), Next->CurrentHP, 920.f);
        TestEqual(TEXT("Only eventual real hit heals"), F.Hero->CurrentHP, 116.f);
    }
    {
        FFixture F(1, false, true, 0.f);
        TestTrue(TEXT("Arm invalid launch"), F.Arm()); TestTrue(TEXT("Commit attempts launch"), F.Hit());
        TestTrue(TEXT("Failed launch retains enhancement"), F.Hero->HasArmedBasicAttack(F.Skill()));
        F.Hero->Definition.BasicAttack.ProjectileSpeed = 1000.f; F.Hero->Tick(2.f);
        TestTrue(TEXT("Retry valid launch"), F.Hit()); F.Hero->Tick(0.6f);
        TestEqual(TEXT("Retry uses retained enhancement"), F.Enemy->CurrentHP, 920.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroStrikeSettingsTest, "ProjectA.Unit.HeroStrike.SettingsAndTimerCast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroStrikeSettingsTest::RunTest(const FString& Parameters)
{
    using namespace HeroStrikeTests;
    FFixture F;
    F.Hero->GainMana(50.f);
    TestTrue(TEXT("Timer cast starts"), F.Hero->TryCastSkill());
    F.Hero->Tick(0.19f);
    TestFalse(TEXT("No enhancement before cast time"), F.Hero->HasArmedBasicAttack(F.Skill()));
    F.Hero->Tick(0.02f);
    TestTrue(TEXT("Timer grants enhancement without a montage"), F.Hero->HasArmedBasicAttack(F.Skill()));
    F.Hero->FinishAction(F.Hero->ActiveActionId);
    // 개별 스킬 인스턴스만 수정하고 공유 BP/CDO 기본값은 유지한다.
    F.Skill()->HealthDamageHealRatio = 1.f;
    TestTrue(TEXT("Hit with armed snapshot"), F.Hit());
    TestEqual(TEXT("Heal ratio was captured at arm time"), F.Hero->CurrentHP, 116.f);
    F.Skill()->AttackDamageRatio = 3.f; F.Skill()->HealthDamageHealRatio = 0.5f;
    F.Skill()->CastSeconds = 0.4f;
    TestEqual(TEXT("Typed cast property builds common config"), F.Skill()->BuildSettings().CastSeconds, 0.4f);
    TestTrue(TEXT("Arm edited ratio"), F.Arm());
    F.Hero->Tick(2.f); TestTrue(TEXT("Hit edited ratio"), F.Hit());
    TestEqual(TEXT("AD 300% is adjustable independently"), F.Enemy->CurrentHP, 800.f);
    TestEqual(TEXT("50% lifesteal is adjustable independently"), F.Hero->CurrentHP, 176.f);
    TestEqual(TEXT("Native defaults not changed by instance tuning"), GetDefault<UHeroStrikeSkill>()->AttackDamageRatio, 2.f);
    F.Skill()->AttackDamageRatio = 0.f; F.Hero->GainMana(50.f);
    TestFalse(TEXT("Invalid ratio does not consume mana"), F.Hero->TryCastSkill());
    TestEqual(TEXT("Mana preserved for invalid config"), F.Hero->CurrentMana, 50.f);
    return true;
}
#endif
