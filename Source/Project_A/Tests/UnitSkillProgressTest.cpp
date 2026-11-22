#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../BattleUnitBase.h"
#include "../Skills/HeroGrowthPassiveSkill.h"
#include "../Skills/HeroStrikeSkill.h"
#include "../Shop/RosterManager.h"
#include "../Shop/ShopManager.h"
#include "../Shop/ShopTransactionManager.h"
#include "../Shop/ShopOdds.h"
#include "../Shop/Tests/TestPlacement.h"
#include "../Shop/Tests/ShopTestGameMode.h"
#include "../Shop/BenchManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

namespace SkillProgressTests
{
    FUnitSkillProgress State(int32 Count, bool bEvolved = false)
    {
        FUnitSkillProgress Result;
        Result.SkillId = TEXT("HeroGrowth"); Result.SkillClass = UHeroGrowthPassiveSkill::StaticClass();
        Result.Counters.Add(TEXT("Participations"), Count);
        if (bEvolved) Result.Flags.Add(TEXT("Evolved"));
        return Result;
    }
    UHeroGrowthPassiveSkill* Growth(ABattleUnitBase* Unit)
    {
        for (UUnitSkillBase* Skill : Unit->GetEquippedSkills())
            if (auto* Result = Cast<UHeroGrowthPassiveSkill>(Skill)) return Result;
        return nullptr;
    }
    struct FFixture
    {
        UWorld* World;
        UDataTable* Table;
        ARosterManager* Roster;
        AShopManager* Shop;
        AShopTransactionManager* Transactions;
        UTestPlacement* Placement;
        AShopTestGameMode* Mode;
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            Context.OwningGameInstance = NewObject<UGameInstance>(GEngine);
            World->SetGameInstance(Context.OwningGameInstance);
            FURL URL;
            URL.AddOption(TEXT("game=/Script/Project_A.ShopTestGameMode"));
            World->SetGameMode(URL);
            World->InitializeActorsForPlay(URL);
            Mode = Cast<AShopTestGameMode>(World->GetAuthGameMode());
            Table = NewObject<UDataTable>(World); Table->RowStruct = FUnitDefinition::StaticStruct();
            FUnitDefinition Data;
            Data.SkillClasses = { UHeroStrikeSkill::StaticClass(), UHeroGrowthPassiveSkill::StaticClass() };
            Table->AddRow(TEXT("Hero"), Data);
            Roster = World->SpawnActor<ARosterManager>(); Shop = World->SpawnActor<AShopManager>();
            Placement = NewObject<UTestPlacement>(Roster);
            Roster->Placement.SetObject(Placement); Roster->Placement.SetInterface(Placement);
            Shop->Catalog.Add({TEXT("Hero"), FText::FromString(TEXT("Hero")), 1}); Shop->StartingGold = 20;
            Shop->ShopOdds = NewObject<UShopOdds>(Shop);
            FShopRoundOdds Odds; Odds.CostWeights = {1};
            Shop->ShopOdds->Rounds.Add(1, Odds); Shop->ShopOdds->Rounds.Add(2, Odds);
            Transactions = World->SpawnActor<AShopTransactionManager>();
            Transactions->Shop = Shop; Transactions->Roster = Roster; Transactions->Items = World->SpawnActor<AItemManager>();
        }
        ~FFixture() { World->BeginTearingDown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
        bool Prepare()
        {
            if (!Mode) return false;
            Mode->Match = { EShopPhase::Preparation, 1, 2 };
            return Transactions->EnterPreparation();
        }
        ABattleUnitBase* Spawn(int32 Count, bool bEvolved = false)
        {
            auto* Unit = World->SpawnActor<ABattleUnitBase>();
            FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = TEXT("Hero"); FString Error;
            if (!Unit->InitializeUnit(Handle, EBattleTeam::Player, 1, Error) || !Unit->RestoreSkillProgress({State(Count, bEvolved)})) return nullptr;
            return Unit;
        }
        ABattleUnitBase* Attach(FGuid Id, int32 Count, bool bEvolved = false)
        {
            auto* Unit = Spawn(Count, bEvolved); Placement->BattleUnits.Add(Id, Unit); return Unit;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressRestoreTest, "ProjectA.Unit.SkillProgress.RestoreValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressRestoreTest::RunTest(const FString& Parameters)
{
    using namespace SkillProgressTests;
    FFixture F; auto* Original = F.Spawn(7, true); auto* Copy = F.Spawn(0);
    if (!TestNotNull(TEXT("Original initialized"), Original) || !TestNotNull(TEXT("Copy initialized"), Copy)) return false;
    TArray<FUnitSkillProgress> Saved;
    TestTrue(TEXT("Capture progress"), Original->CaptureSkillProgress(Saved));
    TestTrue(TEXT("Restore to fresh actor"), Copy->RestoreSkillProgress(Saved));
    auto* Skill = Growth(Copy);
    TestEqual(TEXT("Count survives recreation"), Skill->TotalParticipations, 7);
    TestTrue(TEXT("Evolution record survives recreation"), Skill->bEvolved);
    TestTrue(TEXT("Older snapshot accepted monotonically"), Copy->RestoreSkillProgress({State(2)}));
    TestEqual(TEXT("Older snapshot cannot lower count"), Skill->TotalParticipations, 7);
    TestTrue(TEXT("Older snapshot cannot undo evolution"), Skill->bEvolved);
    auto Invalid = State(8); Invalid.SkillId = TEXT("Unknown");
    TestFalse(TEXT("Whole batch rejects unknown skill"), Copy->RestoreSkillProgress({State(99), Invalid}));
    TestEqual(TEXT("No partial restore"), Skill->TotalParticipations, 7);
    TestFalse(TEXT("Duplicate skill rejected"), Copy->RestoreSkillProgress({State(8), State(9)}));
    TestFalse(TEXT("Negative counter rejected"), Copy->RestoreSkillProgress({State(-1)}));
    Invalid = State(8); Invalid.SkillClass = UHeroStrikeSkill::StaticClass();
    TestFalse(TEXT("Wrong progress class rejected"), Copy->RestoreSkillProgress({Invalid}));
    Invalid = State(8); Invalid.Flags.Add(TEXT("Unknown"));
    TestFalse(TEXT("Unknown flag rejected"), Copy->RestoreSkillProgress({Invalid}));
    Copy->bInCombat = true;
    TestFalse(TEXT("Cannot restore during combat"), Copy->RestoreSkillProgress({State(9)}));
    Copy->StopCombat();
    TestTrue(TEXT("Round reset"), Copy->ResetForRound());
    TestEqual(TEXT("Reset preserves count"), Skill->TotalParticipations, 7);
    TestTrue(TEXT("Reset preserves evolved record"), Skill->bEvolved);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressDeferredMergeTest, "ProjectA.Shop.SkillProgress.DeferredMerge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressDeferredMergeTest::RunTest(const FString& Parameters)
{
    using namespace SkillProgressTests;
    FFixture F;
    for (int32 I = 0; I < 3; ++I)
        if (!TestEqual(TEXT("Grant material"), F.Transactions->GrantStartingUnit(TEXT("Hero")), ETransactionResult::Success)) return false;
    const auto Units = F.Roster->GetUnits();
    auto* Kept = F.Attach(Units[0].Id, 4); auto* Consumed = F.Attach(Units[1].Id, 7, true); F.Attach(Units[2].Id, 2);
    if (!TestNotNull(TEXT("Kept actor"), Kept) || !TestNotNull(TEXT("Consumed actor"), Consumed)) return false;
    FRosterUnitChange Move; Move.Edit = ERosterUnitEdit::Move; Move.Unit = Units[0];
    Move.Destination = {EUnitLocation::Board, 0, 0};
    if (!TestNotNull(TEXT("Shop match state owner"), F.Mode)) return false;
    F.Mode->Match = { EShopPhase::Preparation, 1, 2 };
    TestTrue(TEXT("Keep deployed identity"), F.Roster->GetBench()->ApplyChanges({Move}, F.Mode->Match));
    F.Placement->bReject = true;
    TestFalse(TEXT("Rejected batch blocks deferred merge"), F.Prepare());
    TestEqual(TEXT("Rejected merge retains three units"), F.Roster->GetUnits().Num(), 3);
    TestEqual(TEXT("Rejected merge leaves kept progress alone"), Growth(Kept)->TotalParticipations, 4);
    TestFalse(TEXT("Rejected merge leaves evolution alone"), Growth(Kept)->bEvolved);
    TestEqual(TEXT("Rejected merge leaves star alone"), Kept->StarLevel, 1);
    F.Placement->bReject = false;
    TestTrue(TEXT("Deferred merge commits"), F.Prepare());
    const auto Merged = F.Roster->GetUnits();
    if (!TestEqual(TEXT("One merged unit"), Merged.Num(), 1)) return false;
    TestEqual(TEXT("Board identity retained"), Merged[0].Id, Units[0].Id);
    TestEqual(TEXT("Two stars in roster"), Merged[0].Stars, 2);
    TestEqual(TEXT("Two stars on actor"), Kept->StarLevel, 2);
    TestEqual(TEXT("7,4,2 becomes 7, not 13"), Growth(Kept)->TotalParticipations, 7);
    TestTrue(TEXT("Consumed evolved record inherited"), Growth(Kept)->bEvolved);
    TestEqual(TEXT("Snapshot contains maximum"), Merged[0].SkillProgress[0].Counters.FindChecked(TEXT("Participations")), 7);
    TestTrue(TEXT("Deployed position preserved"), F.Roster->GetPosition(Units[0].Id).Location == EUnitLocation::Board);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressPurchaseMergeTest, "ProjectA.Shop.SkillProgress.PurchaseMergeAndCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressPurchaseMergeTest::RunTest(const FString& Parameters)
{
    using namespace SkillProgressTests;
    FFixture F;
    if (!TestTrue(TEXT("Preparation"), F.Prepare())) return false;
    const int32 Revision = F.Shop->GetShopRevision();
    for (int32 Slot = 0; Slot < 2; ++Slot)
        if (!TestEqual(TEXT("Buy material"), F.Transactions->RequestBuy(Slot, Revision), ETransactionResult::Success)) return false;
    const auto Units = F.Roster->GetUnits();
    auto* Kept = F.Attach(Units[0].Id, 4); F.Attach(Units[1].Id, 7);
    if (!TestNotNull(TEXT("Kept actor"), Kept)) return false;
    F.Placement->bReject = true;
    TestEqual(TEXT("Rejected purchase merge"), F.Transactions->RequestBuy(2, Revision), ETransactionResult::ChangeRejected);
    TestEqual(TEXT("Gold unchanged on rejection"), F.Shop->GetGold(), 18);
    TestEqual(TEXT("Count unchanged on rejection"), Growth(Kept)->TotalParticipations, 4);
    TestEqual(TEXT("Offer unchanged"), F.Shop->GetOffers()[2].UnitId, FName(TEXT("Hero")));
    F.Placement->bReject = false;
    TestEqual(TEXT("Purchase merge"), F.Transactions->RequestBuy(2, Revision), ETransactionResult::Success);
    TestEqual(TEXT("Purchase charged once"), F.Shop->GetGold(), 17);
    TestEqual(TEXT("Purchase merges live count"), Growth(Kept)->TotalParticipations, 7);
    TestTrue(TEXT("New combat growth"), Kept->RestoreSkillProgress({State(9)}));
    FRosterUnit Snapshot;
    if (!TestTrue(TEXT("Read the latest live record"), F.Roster->GetUnit(Units[0].Id, Snapshot))) return false;
    TestEqual(TEXT("Roster reads the live count before a move or merge"), Snapshot.SkillProgress[0].Counters.FindChecked(TEXT("Participations")), 9);
    Kept->RestoreSkillProgress({State(10, true)});
    TestTrue(TEXT("Cache before destruction"), F.Roster->CacheUnitSkillProgress(Units[0].Id));
    F.Placement->BattleUnits.Remove(Units[0].Id); Kept->Destroy();
    FRosterUnit Cached;
    TestTrue(TEXT("Cached record available without actor"), F.Roster->GetUnit(Units[0].Id, Cached));
    auto* Recreated = F.Spawn(0);
    TestTrue(TEXT("Restore cached record to replacement"), Recreated->RestoreSkillProgress(Cached.SkillProgress));
    TestEqual(TEXT("Recreated count"), Growth(Recreated)->TotalParticipations, 10);
    TestTrue(TEXT("Recreated evolution record"), Growth(Recreated)->bEvolved);
    return true;
}
#endif
