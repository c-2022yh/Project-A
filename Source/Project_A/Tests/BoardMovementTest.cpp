#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "../BattleUnitBase.h"
#include "../Skills/BlackfeatherArrowRainSkill.h"
#include "../Skills/AssassinShadowStrikeSkill.h"
#include "../Skills/DirectionalMeleeSkills.h"
#include "../Skills/ElementalistSkill.h"
#include "../Skills/RaiderAmbushSkill.h"
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
        ABattleUnitBase* Spawn(EBattleTeam Team, ABoardTile* Tile, float Damage = 10.f,
            TSubclassOf<UUnitSkillBase> Skill = nullptr, float MaxHealth = 1000.f)
        {
            FUnitDefinition Data; Data.BaseStats.MaxHealth = MaxHealth; Data.BaseStats.AttackDamage = Damage;
            Data.BaseStats.CriticalChance = 0.f; Data.BaseStats.AttackRange = 1; Data.BaseStats.MovementSpeed = 300.f;
            Data.BasicAttack.bUseAnimNotify = false; Data.BasicAttack.AnimationSecondsAtSpeedOne = 0.2f;
            if (Skill)
            {
                Data.SkillClasses.Add(Skill);
                Data.BaseStats.MaxMana = 80.f;
                Data.BaseStats.CriticalChance = 1.f;
            }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectionalMeleeSkillTest, "ProjectA.Unit.DirectionalMelee.LockedCastTiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDirectionalMeleeSkillTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    for (int32 Pattern = 0; Pattern < 3; ++Pattern)
    {
        const bool bSpear = Pattern == 1;
        const bool bDarkKnight = Pattern == 2;
        FFixture F;
        ABoardTile* Origin = F.Board->GetTile(2, 2);
        if (!TestNotNull(TEXT("Interior tile exists"), Origin)) return false;
        const int32 Q = Origin->HexQ;
        const int32 R = Origin->HexR;
        ABoardTile* Front = F.Board->GetTileByHex(Q + 1, R);
        ABoardTile* Left = F.Board->GetTileByHex(Q + 1, R - 1);
        ABoardTile* Right = F.Board->GetTileByHex(Q, R + 1);
        ABoardTile* Behind = F.Board->GetTileByHex(Q + 2, R);
        if (!Front || !Left || !Right || !Behind) { AddError(TEXT("Expected a complete test pattern")); return false; }
        Origin->TeamType = EBoardTileTeam::Player;
        for (ABoardTile* Tile : {Front, Left, Right, Behind}) Tile->TeamType = EBoardTileTeam::Enemy;
        UClass* Skill = bSpear ? USpearmanPiercingThrustSkill::StaticClass() : UBarbarianGroundSlamSkill::StaticClass();
        if (bDarkKnight) Skill = UDarkKnightJudgmentSkill::StaticClass();
        ABattleUnitBase* Caster = F.Spawn(EBattleTeam::Player, Origin, 100.f, Skill);
        ABattleUnitBase* FrontUnit = F.Spawn(EBattleTeam::Enemy, Front);
        ABattleUnitBase* LeftUnit = F.Spawn(EBattleTeam::Enemy, Left);
        ABattleUnitBase* RightUnit = F.Spawn(EBattleTeam::Enemy, Right);
        ABattleUnitBase* BackUnit = F.Spawn(EBattleTeam::Enemy, Behind);
        if (!Caster || !FrontUnit || !LeftUnit || !RightUnit || !BackUnit) { AddError(TEXT("Could not deploy test units")); return false; }
        RightUnit->CombatPolicy.StunDurationMultiplier = 0.5f;
        for (ABattleUnitBase* Unit : {Caster, FrontUnit, LeftUnit, RightUnit, BackUnit}) Unit->StartCombat();

        Caster->CurrentTarget = FrontUnit;
        Caster->GainMana(80.f);
        if (!TestTrue(TEXT("Adjacent primary target starts cast"), Caster->TryCastSkill())) return false;
        const int32 ActionId = Caster->ActiveActionId;
        Caster->CurrentTarget = LeftUnit;
        TestTrue(TEXT("Cast resolves the original tiles despite retargeting"), Caster->CommitSkillEffect(ActionId));
        TestFalse(TEXT("Duplicate impact is rejected"), Caster->CommitSkillEffect(ActionId));
        Caster->FinishAction(ActionId);
        TestEqual(TEXT("Primary tile takes primary skill damage without a crit"), FrontUnit->CurrentHP,
            bDarkKnight ? 880.f : (bSpear ? 780.f : 750.f));
        const float FlankHP = bDarkKnight ? 880.f : (bSpear ? 1000.f : 920.f);
        TestEqual(TEXT("Left flank"), LeftUnit->CurrentHP, FlankHP);
        TestEqual(TEXT("Right flank"), RightUnit->CurrentHP, FlankHP);
        TestEqual(TEXT("Behind primary tile"), BackUnit->CurrentHP, bSpear ? 860.f : 1000.f);
        if (bDarkKnight)
        {
            TestTrue(TEXT("Judgment stuns only its three occupied tiles"),
                FrontUnit->IsStunned() && LeftUnit->IsStunned() && RightUnit->IsStunned() && !BackUnit->IsStunned());
            RightUnit->Tick(0.75f);
            FrontUnit->Tick(0.75f);
            TestFalse(TEXT("Resistant enemy recovers after 0.75 seconds"), RightUnit->IsStunned());
            TestTrue(TEXT("Normal enemy stays stunned"), FrontUnit->IsStunned());
        }

        if (bSpear)
        {
            Caster->CurrentTarget = FrontUnit;
            Caster->GainMana(80.f);
            if (!TestTrue(TEXT("Repeat cast starts while front is occupied"), Caster->TryCastSkill())) return false;
            FUnitDamageRequest Fatal;
            Fatal.RawDamage = 10000.f;
            Fatal.Type = EUnitDamageType::TrueDamage;
            TestTrue(TEXT("Front tile empties before impact"), FrontUnit->ReceiveDamage(Fatal).bKilled);
            TestTrue(TEXT("Empty front does not cancel back tile impact"), Caster->CommitSkillEffect(Caster->ActiveActionId));
            TestEqual(TEXT("Back enemy takes another thrust"), BackUnit->CurrentHP, 720.f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssassinShadowStrikeTest, "ProjectA.Unit.AssassinShadowStrike.StealthAndLanding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAssassinShadowStrikeTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABoardTile* Start = F.Board->GetTile(0, 0);
    ABoardTile* NearTile = F.Board->GetTile(0, 1);
    NearTile->TeamType = EBoardTileTeam::Enemy;
    ABattleUnitBase* Assassin = F.Spawn(EBattleTeam::Player, Start, 80.f, UAssassinShadowStrikeSkill::StaticClass());
    ABattleUnitBase* Near = F.Spawn(EBattleTeam::Enemy, NearTile);
    ABattleUnitBase* Far = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 4));
    if (!Assassin || !Near || !Far) { AddError(TEXT("Could not deploy shadow strike test units")); return false; }
    for (ABattleUnitBase* Unit : {Assassin, Near, Far}) Unit->StartCombat();
    Near->CurrentTarget = Assassin;
    if (!TestTrue(TEXT("Existing swing starts before stealth"), Near->TryBasicAttack())) return false;
    Assassin->GainMana(Assassin->FinalStats.MaxMana);
    if (!TestTrue(TEXT("Assassin begins a cast"), Assassin->TryCastSkill())) return false;
    TestTrue(TEXT("Cannot select the stealthed unit for a new swing"),
        Assassin->IsCombatStealthed() && !Near->IsEnemyAttackTarget(Assassin));
    TestTrue(TEXT("An already started attack can still hit"), Near->CommitAttackHit(Near->ActiveActionId));
    TestEqual(TEXT("Stealth does not reduce damage"), Assassin->CurrentHP, 990.f);
    Assassin->Tick(0.24f);
    TestTrue(TEXT("Stealth and starting tile persist before cast completes"),
        Assassin->IsCombatStealthed() && Assassin->CurrentTile == Start);
    Assassin->Tick(0.02f);
    TestFalse(TEXT("Stealth ends on impact"), Assassin->IsCombatStealthed());
    TestEqual(TEXT("Only the farthest target takes 150 percent AD"), Far->CurrentHP, 880.f);
    TestEqual(TEXT("Nearer target is untouched"), Near->CurrentHP, 1000.f);
    TestTrue(TEXT("Assassin occupies a neighboring tile and targets its victim"),
        Assassin->CurrentTarget == Far && ABattleUnitBase::HexDistance(Assassin->CurrentTile, Far->CurrentTile) == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElementalistMeteorTest, "ProjectA.Unit.Elementalist.FormManaAndMeteor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElementalistMeteorTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABattleUnitBase* Caster = F.Spawn(EBattleTeam::Player, F.Board->GetTile(0, 0), 20.f, UElementalistSkill::StaticClass());
    ABattleUnitBase* Center = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 2));
    ABattleUnitBase* Inner = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 3));
    ABattleUnitBase* Outer = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 4));
    if (!Caster || !Center || !Inner || !Outer) { AddError(TEXT("Could not deploy meteor test units")); return false; }
    auto* Skill = Cast<UElementalistSkill>(Caster->GetEquippedSkills()[0]);
    if (!TestNotNull(TEXT("Elementalist skill"), Skill)) return false;
    TestEqual(TEXT("Default Frost max mana"), Caster->FinalStats.MaxMana, 45.f);
    TestTrue(TEXT("Choose Fire in preparation"), Skill->SetForm(EElementalForm::Fire));
    TestEqual(TEXT("Fire max mana"), Caster->FinalStats.MaxMana, 100.f);
    TestTrue(TEXT("Fire selection survives a round reset"), Caster->ResetForRound() && Skill->GetForm() == EElementalForm::Fire);
    if (!TestTrue(TEXT("Caster placed for combat"), Caster->PlaceOnTile(F.Board->GetTile(0, 0)))) return false;
    Caster->SetStarLevel(2);
    FUnitStatModifier AP;
    AP.AbilityPower = 100.f;
    Caster->ApplyUpgrade(TEXT("AP"), AP);
    for (ABattleUnitBase* Unit : {Caster, Center, Inner, Outer}) Unit->StartCombat();
    TestFalse(TEXT("Cannot change element midfight"), Skill->SetForm(EElementalForm::Frost));
    Caster->CurrentTarget = Center;
    Caster->GainMana(100.f);
    if (!TestTrue(TEXT("Begins meteor in combat"), Caster->TryCastSkill())) return false;
    TestFalse(TEXT("Meteor was already scheduled at cast start"), Caster->CommitSkillEffect(Caster->ActiveActionId));
    Caster->Tick(0.8f);
    TestTrue(TEXT("Caster can resume fighting before the meteor lands"), Caster->ActionState != EUnitActionState::Casting);
    TestEqual(TEXT("No early meteor damage"), Center->CurrentHP, 1000.f);

    FUnitDamageRequest Fatal;
    Fatal.RawDamage = 10000.f;
    Fatal.Type = EUnitDamageType::TrueDamage;
    TestTrue(TEXT("Caster dies with a pending meteor"), Caster->ReceiveDamage(Fatal).bKilled);
    Caster->Tick(0.6f);
    TestEqual(TEXT("Meteor still waits for the full delay"), Center->CurrentHP, 1000.f);
    Caster->Tick(0.1f);
    TestEqual(TEXT("The current target wins the tie and takes full 450 percent AP"), Center->CurrentHP, 550.f);
    TestEqual(TEXT("Distance one takes 65 percent"), Inner->CurrentHP, 707.5f);
    TestEqual(TEXT("Distance two takes 40 percent"), Outer->CurrentHP, 820.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlackfeatherArrowRainTest, "ProjectA.Unit.BlackfeatherArrowRain.FixedTileImpact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBlackfeatherArrowRainTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABattleUnitBase* Archer = F.Spawn(EBattleTeam::Player, F.Board->GetTile(0, 0), 65.f,
        UBlackfeatherArrowRainSkill::StaticClass());
    ABattleUnitBase* Lone = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(4, 0));
    ABattleUnitBase* Cluster = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 3));
    ABattleUnitBase* Escapes = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 4));
    if (!Archer || !Lone || !Cluster || !Escapes) { AddError(TEXT("Could not deploy arrow rain test units")); return false; }
    FUnitStatModifier Armor;
    Armor.Armor = 100.f;
    Cluster->ApplyUpgrade(TEXT("Armor"), Armor);
    for (ABattleUnitBase* Unit : {Archer, Lone, Cluster, Escapes}) Unit->StartCombat();
    Archer->CurrentTarget = Lone;
    Archer->GainMana(80.f);
    if (!TestTrue(TEXT("Can cast across the board"), Archer->TryCastSkill())) return false;
    const int32 CastId = Archer->ActiveActionId;
    TestTrue(TEXT("Arrow rain is launched"), Archer->CommitSkillEffect(CastId));
    Archer->FinishAction(CastId);
    TestEqual(TEXT("Target HP stays unchanged at launch"), Cluster->CurrentHP, 1000.f);

    Escapes->StopCombat();
    if (!TestTrue(TEXT("Second enemy changes its occupied tile before impact"),
        Escapes->ResetForRound() && Escapes->PlaceOnTile(F.Board->GetTile(3, 0)))) return false;
    Escapes->StartCombat();

    FUnitDamageRequest Fatal;
    Fatal.RawDamage = 10000.f;
    Fatal.Type = EUnitDamageType::TrueDamage;
    TestTrue(TEXT("Archer dies after launch"), Archer->ReceiveDamage(Fatal).bKilled);
    Archer->Tick(0.25f);
    TestEqual(TEXT("Halfway to impact still does no damage"), Cluster->CurrentHP, 1000.f);
    Archer->Tick(0.25f);
    TestTrue(TEXT("Highest count fixed region hits once after archer dies with armor reduction already active"),
        FMath::IsNearlyEqual(Cluster->CurrentHP, 1000.f - 117.f / 1.7f, 0.001f));
    TestEqual(TEXT("Armor reduced to 70"), Cluster->FinalStats.Armor, 70.f);
    TestEqual(TEXT("Lone basic attack target loses to the larger cluster"), Lone->CurrentHP, 1000.f);
    TestEqual(TEXT("Enemy that left the impact region is unharmed"), Escapes->CurrentHP, 1000.f);
    Archer->Tick(0.5f);
    TestTrue(TEXT("Impact does not repeat"),
        FMath::IsNearlyEqual(Cluster->CurrentHP, 1000.f - 117.f / 1.7f, 0.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaiderAmbushTest, "ProjectA.Unit.RaiderAmbush.CombatLanding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRaiderAmbushTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABoardTile* Start = F.Board->GetTile(0, 0);
    ABattleUnitBase* Raider = F.Spawn(EBattleTeam::Player, Start, 60.f, URaiderAmbushSkill::StaticClass());
    ABattleUnitBase* Blocked = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 4));
    ABattleUnitBase* Chosen = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(4, 1));
    ABattleUnitBase* Other = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(3, 3), 10.f, nullptr, 3000.f);
    if (!Raider || !Blocked || !Chosen || !Other) { AddError(TEXT("Could not deploy ambush test units")); return false; }
    for (ABattleUnitBase* Unit : {Raider, Blocked, Chosen, Other}) Unit->StartCombat();
    Raider->CurrentTarget = Other;
    Blocked->CurrentHP = 1.f;
    Chosen->CurrentHP = 350.f;
    Other->CurrentHP = 450.f;

    const auto SetBlocked = [&F](ABattleUnitBase* Target, bool bBlock)
    {
        for (ABoardTile* Tile : F.Board->GetNeighbors(Target->CurrentTile)) Tile->bBlocksMovement = bBlock;
    };
    SetBlocked(Blocked, true);
    SetBlocked(Chosen, true);
    SetBlocked(Other, true);
    Raider->GainMana(80.f);
    TestFalse(TEXT("No viable landing means no cast"), Raider->TryCastSkill());
    TestEqual(TEXT("No target preserves full mana"), Raider->CurrentMana, 80.f);
    SetBlocked(Chosen, false);
    SetBlocked(Other, false);
    SetBlocked(Blocked, true);
    if (!TestTrue(TEXT("Found at least one reachable enemy"), Raider->TryCastSkill())) return false;
    ABoardTile* InitialLanding = F.Board->FindCombatLandingTile(Raider, Chosen);
    if (!TestNotNull(TEXT("Initial landing is available"), InitialLanding)) return false;
    InitialLanding->bBlocksMovement = true;
    const int32 CastId = Raider->ActiveActionId;
    TestTrue(TEXT("Ambush commits"), Raider->CommitSkillEffect(CastId));
    TestFalse(TEXT("Duplicate commit rejected"), Raider->CommitSkillEffect(CastId));
    Raider->FinishAction(CastId);

    ABoardTile* Landed = Raider->CurrentTile;
    if (!TestNotNull(TEXT("Landed tile"), Landed)) return false;
    TestTrue(TEXT("Landing is adjacent to the enemy with the lowest reachable absolute HP"),
        ABattleUnitBase::HexDistance(Landed, Chosen->CurrentTile) == 1);
    TestTrue(TEXT("Landing rechecked on hit"), Landed != InitialLanding);
    TestTrue(TEXT("Occupancy transferred across the board"), !Start->OccupiedUnit && Landed->OccupiedUnit == Raider && !Landed->IsReserved());
    TestTrue(TEXT("Actor and tile locations match"), Raider->GetActorLocation().Equals(Landed->GetSurfaceLocation() + FVector(0, 0, 1.f)));
    TestEqual(TEXT("Only chosen enemy takes 250 percent AD"), Chosen->CurrentHP, 200.f);
    TestEqual(TEXT("Unreachable enemy excluded"), Blocked->CurrentHP, 1.f);
    TestEqual(TEXT("Lower health percentage does not win"), Other->CurrentHP, 450.f);
    TestEqual(TEXT("Ambush grants no basic attack mana"), Raider->CurrentMana, 0.f);
    TestTrue(TEXT("Surviving target becomes the next basic target"), Raider->CurrentTarget == Chosen);

    SetBlocked(Chosen, true);
    Raider->GainMana(80.f);
    if (!TestTrue(TEXT("Adjacent victim can be stabbed without a vacant tile"), Raider->TryCastSkill())) return false;
    TestTrue(TEXT("Adjacent ambush commits"), Raider->CommitSkillEffect(Raider->ActiveActionId));
    TestTrue(TEXT("Adjacent ambush does not relocate"), Raider->CurrentTile == Landed);
    TestEqual(TEXT("Second ambush deals the same damage"), Chosen->CurrentHP, 50.f);
    Raider->StopCombat();
    TestTrue(TEXT("Normal round reset restores original preparation tile"),
        Raider->ResetForRound() && Raider->CurrentTile == Start && Start->OccupiedUnit == Raider);
    return true;
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

    ABattleUnitBase* AngledEnemy = F.Spawn(EBattleTeam::Enemy, F.Board->GetTileByHex(1, 3));
    ABoardTile* Forward = F.Board->GetTileByHex(0, 1);
    ABoardTile* Side = F.Board->GetTileByHex(1, 0);
    if (!AngledEnemy || !Forward || !Side) { AddError(TEXT("Could not set up equal-length approach routes")); return false; }
    if (!TestTrue(TEXT("Angled enemy is reachable"), F.Board->FindPathToAttackRange(A, AngledEnemy, Path))) return false;
    TestEqual(TEXT("Approach does not add steps"), Path.Num(), 3);
    if (!Path.IsEmpty()) TestTrue(TEXT("Prefer the step that points toward the enemy"), Path[0] == Forward);
    Forward->bBlocksMovement = true;
    if (!TestTrue(TEXT("Side approach remains available"), F.Board->FindPathToAttackRange(A, AngledEnemy, Path))) return false;
    TestEqual(TEXT("Side approach is equally short"), Path.Num(), 3);
    if (!Path.IsEmpty()) TestTrue(TEXT("Use the side only when the forward step is blocked"), Path[0] == Side);
    Forward->bBlocksMovement = false;

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
    // 자동으로 이어진 이동을 취소한 뒤, 시작하지 않은 보드 예약의 만료를 따로 확인한다.
    TestTrue(TEXT("Board cancels the automatically started step"), F.Board->CancelReservedMove(A, A->ActiveActionId));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoardContinuousApproachTest, "ProjectA.Board.ContinuousApproach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBoardContinuousApproachTest::RunTest(const FString& Parameters)
{
    using namespace BoardMovementTests;
    FFixture F;
    ABattleUnitBase* Attacker = F.Spawn(EBattleTeam::Player, F.Board->GetTile(0, 0));
    ABattleUnitBase* FirstEnemy = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(3, 2));
    ABattleUnitBase* NextEnemy = F.Spawn(EBattleTeam::Enemy, F.Board->GetTile(5, 4));
    if (!Attacker || !FirstEnemy || !NextEnemy) return false;

    Attacker->CombatPolicy.RetargetInterval = 10.f;
    Attacker->SetActorRotation(FRotator(0.f, 180.f, 0.f));
    Attacker->StartCombat();
    FirstEnemy->StartCombat();
    NextEnemy->StartCombat();
    Attacker->Tick(0.01f);
    ABoardTile* FirstStep = F.Reservation(Attacker);
    if (!TestNotNull(TEXT("First step reserved"), FirstStep)) return false;
    const FVector FirstDirection = (FirstStep->GetSurfaceLocation() - Attacker->GetActorLocation()).GetSafeNormal2D();
    TestTrue(TEXT("Unit faces the step as soon as it is reserved"),
        Attacker->GetActorForwardVector().Equals(FirstDirection, 0.01f));

    Attacker->Tick(1.f);
    TestTrue(TEXT("Unit arrives at the first tile"), Attacker->CurrentTile == FirstStep);
    TestTrue(TEXT("Next step reserved on the arrival tick"),
        Attacker->ActionState == EUnitActionState::Moving && F.Reservation(Attacker) != nullptr);

    FUnitDamageRequest Kill;
    Kill.Attacker = Attacker;
    Kill.RawDamage = 100000.f;
    FirstEnemy->ReceiveDamage(Kill);
    Attacker->Tick(0.01f);
    TestTrue(TEXT("Next enemy selected as soon as the previous enemy dies"), Attacker->CurrentTarget == NextEnemy);
    TestTrue(TEXT("Approach resumes on the retarget tick"),
        Attacker->ActionState == EUnitActionState::Moving && F.Reservation(Attacker) != nullptr);
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
