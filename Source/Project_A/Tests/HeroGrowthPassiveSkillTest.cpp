#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../Skills/HeroGrowthPassiveSkill.h"
#include "../Skills/HeroStrikeSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardTile.h"
#include "UnitSkillTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

namespace HeroGrowthTests
{
    struct FFixture
    {
        UWorld* World;
        UDataTable* Table;
        ABattleUnitBase* Hero;
        ABattleUnitBase* Ally;
        ABattleUnitBase* Enemy;
        static FUnitDefinition Data(bool bHero = false)
        {
            FUnitDefinition Result; Result.BaseStats.MaxHealth = bHero ? 500.f : 1000.f;
            Result.BaseStats.AttackDamage = bHero ? 40.f : 10.f;
            Result.BaseStats.MaxMana = 50.f; Result.BaseStats.CriticalChance = 0.f; Result.BaseStats.MovementSpeed = 0.f;
            if (bHero) Result.SkillClasses = {UHeroStrikeSkill::StaticClass(), UHeroGrowthPassiveSkill::StaticClass()};
            return Result;
        }
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Table = NewObject<UDataTable>(World); Table->RowStruct = FUnitDefinition::StaticStruct();
            Hero = Spawn(TEXT("Hero"), Data(true), EBattleTeam::Player, 0);
            Ally = Spawn(TEXT("Ally"), Data(), EBattleTeam::Player, -1);
            Enemy = Spawn(TEXT("Enemy"), Data(), EBattleTeam::Enemy, 1);
            if (Hero && Ally && Enemy)
            {
                const TArray<ABattleUnitBase*> Units = {Hero, Ally, Enemy};
                for (ABattleUnitBase* Unit : Units) { Unit->SetCombatRoster(Units); Unit->StartCombat(); }
            }
        }
        ~FFixture() { World->BeginTearingDown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        ABattleUnitBase* Spawn(FName Name, const FUnitDefinition& Row, EBattleTeam Team, int32 Q)
        {
            Table->AddRow(Name, Row);
            ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
            FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name; FString Error;
            if (!Unit->InitializeUnit(Handle, Team, 1, Error)) return nullptr;
            ABoardTile* Tile = World->SpawnActor<ABoardTile>(); Tile->HexQ = Q; Tile->HexR = 0;
            Tile->TeamType = Team == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
            Tile->SetActorLocation(FVector(Q * 500.f, 0.f, 0.f)); return Unit->PlaceOnTile(Tile) ? Unit : nullptr;
        }
        static UHeroGrowthPassiveSkill* Growth(ABattleUnitBase* Unit)
        {
            for (UUnitSkillBase* Skill : Unit->GetEquippedSkills())
                if (UHeroGrowthPassiveSkill* Result = Cast<UHeroGrowthPassiveSkill>(Skill)) return Result;
            return nullptr;
        }
        static FUnitDamageResult Damage(ABattleUnitBase* Attacker, ABattleUnitBase* Target, float Amount,
            EUnitDamageSource Source = EUnitDamageSource::BasicAttack)
        {
            FUnitDamageRequest Request; Request.Attacker = Attacker; Request.RawDamage = Amount;
            Request.Type = EUnitDamageType::TrueDamage; Request.Source = Source;
            return Target->ReceiveDamage(Request);
        }
        bool Restart()
        {
            const TArray<ABattleUnitBase*> Units = {Hero, Ally, Enemy};
            for (ABattleUnitBase* Unit : Units) Unit->StopCombat();
            for (ABattleUnitBase* Unit : Units) if (!Unit->ResetForRound()) return false;
            for (ABattleUnitBase* Unit : Units) Unit->StartCombat();
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroGrowthAssistTest, "ProjectA.Unit.HeroGrowth.AssistsAndDeduplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroGrowthAssistTest::RunTest(const FString& Parameters)
{
    using namespace HeroGrowthTests;
    FFixture F; UHeroGrowthPassiveSkill* Growth = FFixture::Growth(F.Hero);
    if (!TestNotNull(TEXT("Growth instance"), Growth)) return false;
    FUnitDamageRequest Hit; Hit.Attacker = F.Hero; Hit.RawDamage = 10.f; Hit.Type = EUnitDamageType::TrueDamage;
    TestTrue(TEXT("First contribution applies"), F.Enemy->ReceiveDamage(Hit).bApplied);
    TestFalse(TEXT("Same damage request rejected"), F.Enemy->ReceiveDamage(Hit).bApplied);
    FFixture::Damage(F.Hero, F.Enemy, 10.f); FFixture::Damage(F.Hero, F.Enemy, 10.f);
    TestEqual(TEXT("Hits alone do not grant participation"), Growth->TotalParticipations, 0);
    FFixture::Damage(F.Ally, F.Enemy, 10000.f);
    TestEqual(TEXT("Ally finishing a contributed enemy grants exactly one"), Growth->TotalParticipations, 1);
    FFixture::Damage(F.Hero, F.Enemy, 10000.f);
    F.Enemy->OnUnitDied.Broadcast(F.Enemy);
    TestEqual(TEXT("Repeated damage/death signal cannot count again"), Growth->TotalParticipations, 1);
    TestTrue(TEXT("Next round restores same actors"), F.Restart());
    FFixture::Damage(F.Hero, F.Enemy, 10000.f);
    TestEqual(TEXT("Own killing blow is registered before death"), Growth->TotalParticipations, 2);
    TestEqual(TEXT("Ordinary Kill trigger does not also advance transient counter"), Growth->Counter, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroGrowthEligibilityTest, "ProjectA.Unit.HeroGrowth.DamageEligibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroGrowthEligibilityTest::RunTest(const FString& Parameters)
{
    using namespace HeroGrowthTests;
    {
        FFixture F; FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("No contribution means no credit"), FFixture::Growth(F.Hero)->TotalParticipations, 0);
    }
    {
        FFixture F; F.Enemy->AddShield(TEXT("Shield"), 100.f, 20.f);
        const FUnitDamageResult Result = FFixture::Damage(F.Hero, F.Enemy, 10.f);
        TestEqual(TEXT("Only shield damaged"), Result.HealthDamage, 0.f);
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Real shield damage is a contribution"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
    }
    {
        FFixture F;
        TestFalse(TEXT("Zero raw damage is rejected"), FFixture::Damage(F.Hero, F.Enemy, 0.f).bApplied);
        FUnitStatModifier Invulnerable; Invulnerable.DamageReduction = 1.f;
        F.Enemy->AddModifier(TEXT("Invulnerable"), Invulnerable, 20.f);
        TestEqual(TEXT("No real damage through full reduction"), FFixture::Damage(F.Hero, F.Enemy, 10.f).HealthDamage, 0.f);
        F.Enemy->RemoveEffect(TEXT("Invulnerable")); FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Zero actual damage did not register"), FFixture::Growth(F.Hero)->TotalParticipations, 0);
    }
    {
        FFixture F; FFixture::Damage(F.Hero, F.Ally, 10.f); FFixture::Damage(F.Enemy, F.Ally, 10000.f);
        TestEqual(TEXT("Friendly damage cannot grant credit"), FFixture::Growth(F.Hero)->TotalParticipations, 0);
    }
    for (EUnitDamageSource Source : {EUnitDamageSource::Skill, EUnitDamageSource::Triggered, EUnitDamageSource::DamageOverTime})
    {
        FFixture F; FFixture::Damage(F.Hero, F.Enemy, 10.f, Source);
        FFixture::Damage(nullptr, F.Enemy, 10000.f);
        TestEqual(TEXT("Actual skill/proc/DoT damage grants credit even without a final attacker"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
    }
    {
        FFixture First, Second;
        FFixture::Damage(First.Hero, Second.Enemy, 10.f); FFixture::Damage(Second.Ally, Second.Enemy, 10000.f);
        TestEqual(TEXT("Different worlds cannot share contribution"), FFixture::Growth(First.Hero)->TotalParticipations, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroGrowthRoundsTest, "ProjectA.Unit.HeroGrowth.RoundRetentionAndGenerations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroGrowthRoundsTest::RunTest(const FString& Parameters)
{
    using namespace HeroGrowthTests;
    {
        FFixture F; UHeroGrowthPassiveSkill* Growth = FFixture::Growth(F.Hero);
        FFixture::Damage(F.Hero, F.Enemy, 10000.f);
        TestTrue(TEXT("Round restart"), F.Restart());
        TestTrue(TEXT("Same owned skill instance survives round transition"), Growth == FFixture::Growth(F.Hero));
        TestEqual(TEXT("Progress survives stop/reset/start"), Growth->TotalParticipations, 1);
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Old round's contribution did not carry over"), Growth->TotalParticipations, 1);
        TestTrue(TEXT("Another restart"), F.Restart());
        FFixture::Damage(F.Hero, F.Enemy, 10.f); FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Same actor's new life can grant new credit"), Growth->TotalParticipations, 2);
    }
    {
        FFixture F; FFixture::Damage(F.Hero, F.Enemy, 10.f);
        F.Hero->StopCombat(); F.Hero->ResetForRound(); F.Hero->StartCombat();
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Contributor's old combat generation cannot claim a later death"), FFixture::Growth(F.Hero)->TotalParticipations, 0);
    }
    {
        FFixture F; FFixture::Damage(F.Hero, F.Enemy, 10.f);
        F.Enemy->StopCombat(); F.Enemy->ResetForRound(); F.Enemy->StartCombat();
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Victim reset discards old life contributions"), FFixture::Growth(F.Hero)->TotalParticipations, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroGrowthDeathTimingTest, "ProjectA.Unit.HeroGrowth.DeathTimingAndDeadContributor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroGrowthDeathTimingTest::RunTest(const FString& Parameters)
{
    using namespace HeroGrowthTests;
    {
        FFixture F; FFixture::Damage(F.Hero, F.Enemy, 10.f);
        UUnitSkillTestDeathObserver* Observer = NewObject<UUnitSkillTestDeathObserver>(F.World);
        Observer->UnitToStop = F.Hero;
        F.Enemy->OnUnitDied.AddDynamic(Observer, &UUnitSkillTestDeathObserver::OnDeath);
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestFalse(TEXT("External death listener stopped the contributor"), F.Hero->bInCombat);
        TestEqual(TEXT("Credit committed before external round-stop callback"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
    }
    {
        FFixture F; FFixture::Damage(F.Hero, F.Enemy, 10.f); FFixture::Damage(F.Enemy, F.Hero, 10000.f);
        TestTrue(TEXT("Contributor died"), F.Hero->bIsDead);
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Dead contributor keeps credit while same combat continues"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
        TestTrue(TEXT("Dead hero recovers"), F.Restart());
        TestEqual(TEXT("Death and recovery preserve progress"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
    }
    {
        FFixture F; FFixture::Damage(F.Hero, F.Enemy, 10.f); F.Hero->StopCombat();
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("Leaving combat removes entitlement to later deaths"), FFixture::Growth(F.Hero)->TotalParticipations, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroGrowthCompositionTest, "ProjectA.Unit.HeroGrowth.InstanceIsolationAndActiveComposition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroGrowthCompositionTest::RunTest(const FString& Parameters)
{
    using namespace HeroGrowthTests;
    {
        FFixture F; F.Enemy->CurrentHP = 50.f; F.Hero->CurrentHP = 100.f;
        F.Hero->GainMana(50.f); TestTrue(TEXT("Hero active starts with passive equipped"), F.Hero->TryCastSkill());
        const int32 CastId = F.Hero->ActiveActionId;
        F.Hero->CommitSkillEffect(CastId); F.Hero->FinishAction(CastId);
        F.Hero->CurrentTarget = F.Enemy; TestTrue(TEXT("Basic starts"), F.Hero->TryBasicAttack());
        TestTrue(TEXT("Empowered killing hit"), F.Hero->CommitAttackHit(F.Hero->ActiveActionId));
        TestEqual(TEXT("Active lifesteal preserved"), F.Hero->CurrentHP, 110.f);
        TestEqual(TEXT("Passive credited same active-enhanced attack exactly once"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
    }
    {
        FFixture F; FUnitDefinition Row = FFixture::Data(true); Row.SkillClasses = {UHeroGrowthPassiveSkill::StaticClass()};
        ABattleUnitBase* Other = F.Spawn(TEXT("Other"), Row, EBattleTeam::Player, -2);
        if (!TestNotNull(TEXT("Other hero"), Other)) return false;
        Other->StartCombat();
        TestFalse(TEXT("Passive-only hero does not use mana"), Other->UsesMana());
        FFixture::Damage(F.Hero, F.Enemy, 10.f); FFixture::Damage(Other, F.Enemy, 10.f);
        FFixture::Damage(F.Ally, F.Enemy, 10000.f);
        TestEqual(TEXT("First hero credited"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
        TestEqual(TEXT("Second hero credited independently"), FFixture::Growth(Other)->TotalParticipations, 1);
        ABattleUnitBase* Next = F.Spawn(TEXT("NextEnemy"), FFixture::Data(), EBattleTeam::Enemy, 2);
        if (!TestNotNull(TEXT("Next enemy"), Next)) return false;
        Next->StartCombat(); FFixture::Damage(Other, Next, 10.f); FFixture::Damage(F.Ally, Next, 10000.f);
        TestEqual(TEXT("Other progress stays independent"), FFixture::Growth(Other)->TotalParticipations, 2);
        TestEqual(TEXT("First hero did not gain unearned progress"), FFixture::Growth(F.Hero)->TotalParticipations, 1);
        TestEqual(TEXT("Shared class defaults stay unchanged"), GetDefault<UHeroGrowthPassiveSkill>()->TotalParticipations, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHeroGrowthReadinessTest, "ProjectA.Unit.HeroGrowth.ConfigurationAndFreshIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHeroGrowthReadinessTest::RunTest(const FString& Parameters)
{
    using namespace HeroGrowthTests;
    FFixture F; UHeroGrowthPassiveSkill* Growth = FFixture::Growth(F.Hero);
    TestFalse(TEXT("Unset threshold does not invent a design value"), Growth->IsEvolutionReady());
    Growth->RequiredParticipations = 1;
    TestFalse(TEXT("No credit, not ready"), Growth->IsEvolutionReady());
    FFixture::Damage(F.Hero, F.Enemy, 10000.f);
    TestTrue(TEXT("Configured threshold can be queried"), Growth->IsEvolutionReady());
    TestEqual(TEXT("Readiness does not apply evolved stats"), F.Hero->Definition.BaseStats.MaxHealth, 500.f);
    TestEqual(TEXT("Readiness does not alter active ratio"), CastChecked<UHeroStrikeSkill>(F.Hero->GetEquippedSkills()[0])->AttackDamageRatio, 2.f);
    Growth->RequiredParticipations = 0;
    TestFalse(TEXT("Zero disables readiness, preserving count"), Growth->IsEvolutionReady());
    TestEqual(TEXT("Editing threshold does not reset progress"), Growth->TotalParticipations, 1);
    F.Hero->StopCombat();
    FDataTableRowHandle Handle; Handle.DataTable = F.Table; Handle.RowName = TEXT("Hero"); FString Error;
    TestTrue(TEXT("Explicit initialization creates a fresh identity"), F.Hero->InitializeUnit(Handle, EBattleTeam::Player, 1, Error));
    TestEqual(TEXT("Fresh skill starts at zero, not copied from prior identity"), FFixture::Growth(F.Hero)->TotalParticipations, 0);
    return true;
}
#endif
