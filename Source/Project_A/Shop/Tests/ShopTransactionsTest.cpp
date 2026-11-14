#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "../RosterManager.h"
#include "../BenchManager.h"
#include "../ShopManager.h"
#include "../ShopTransactionManager.h"
#include "../ShopOdds.h"
#include "Engine/World.h"
#include "TestPlacement.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTransactionsTest, "ProjectA.Shop.Transactions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FShopTransactionsTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Options;
    Options.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).AllowAudioPlayback(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Options);
    if (!TestNotNull(TEXT("World"), World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    const FName UnitType(TEXT("Tank"));
    const FName ItemId(TEXT("Armor"));
    const FUnitPosition Front { EUnitLocation::Board, 0, 0 };
    const FUnitPosition Second { EUnitLocation::Board, 0, 1 };
    const FName OtherType(TEXT("Archer"));
    AShopManager* Shop = nullptr;
    ARosterManager* Roster = nullptr;
    AItemManager* Items = nullptr;
    UTestPlacement* Board = nullptr;
    AShopTransactionManager* Transactions = nullptr;
    const auto StartMatch = [&](int32 StartingGold)
    {
        Shop = World->SpawnActor<AShopManager>();
        Roster = World->SpawnActor<ARosterManager>();
        Items = World->SpawnActor<AItemManager>();
        if (!Shop || !Roster || !Items) return false;
        Board = NewObject<UTestPlacement>(Roster);
        Roster->Placement.SetObject(Board);
        Roster->Placement.SetInterface(Board);
        Shop->Catalog.Add({ UnitType, FText::FromName(UnitType), 1 });
        Shop->StartingGold = StartingGold;
        Shop->KillGold = 1;
        Shop->BonusKillGold = 2;
        Shop->BonusKillGoldChance = 1.0f;
        Items->KillItemChance = 1.0f;
        Items->ItemDefinitions = NewObject<UDataTable>(Items);
        Items->ItemDefinitions->RowStruct = FItemDefinition::StaticStruct();
        FItemDefinition Armor;
        Armor.Bonuses.Armor = 5;
        Items->ItemDefinitions->AddRow(ItemId, Armor);
        Shop->RewardPool = {
            { ERewardKind::Item, ItemId, 1, 1 },
            { ERewardKind::Unit, UnitType, 1, 1 },
            { ERewardKind::Unit, UnitType, 2, 1 }
        };
        Shop->Catalog.Add({ OtherType, FText::FromName(OtherType), 2 });
        Shop->RewardPool.Add({ ERewardKind::Unit, OtherType, 1, 1 });
        FShopRoundOdds FirstRound;
        FirstRound.CostWeights = { 1, 0 };
        Shop->ShopOdds = NewObject<UShopOdds>(Shop);
        Shop->ShopOdds->Rounds.Add(1, FirstRound);
        Transactions = World->SpawnActor<AShopTransactionManager>();
        if (!Transactions) return false;
        Transactions->Shop = Shop;
        Transactions->Roster = Roster;
        Transactions->Items = Items;
        return Transactions->IsConfigured();
    };

    if (!TestTrue(TEXT("Configure match"), StartMatch(20))) return false;
    TestEqual(TEXT("Gold remains unset before preparation"), Shop->GetGold(), 0);
    if (!TestTrue(TEXT("Preparation"), Transactions->EnterPreparation(1, 2))) return false;
    const int32 Revision = Shop->GetShopRevision();
    Board->bReject = true;
    TestEqual(TEXT("Rejected board change"), Transactions->RequestBuy(0, Revision), ETransactionResult::ChangeRejected);
    TestEqual(TEXT("Rejected purchase keeps gold"), Shop->GetGold(), 20);
    TestEqual(TEXT("Rejected purchase keeps offer"), Shop->GetOffers()[0].UnitId, UnitType);
    TestEqual(TEXT("Rejected purchase keeps roster"), Roster->GetUnits().Num(), 0);
    Board->bReject = false;

    Board->OnApply = [&]
    {
        TestEqual(TEXT("Nested purchase rejected"), Transactions->RequestBuy(0, Revision), ETransactionResult::Unavailable);
        TestEqual(TEXT("Nested reroll rejected"), Shop->RequestReroll(), ETransactionResult::Unavailable);
        TestEqual(TEXT("Purchase is not charged before placement succeeds"), Shop->GetGold(), 20);
    };
    if (!TestEqual(TEXT("Buy first copy"), Transactions->RequestBuy(0, Revision), ETransactionResult::Success)) return false;
    Board->OnApply = nullptr;
    const FGuid Kept = Roster->GetUnits()[0].Id;
    TestFalse(TEXT("First preparation cannot be repeated"), Transactions->EnterPreparation(1, 2));
    TestEqual(TEXT("Repeated preparation preserves gold"), Shop->GetGold(), 19);
    TestEqual(TEXT("Repeated preparation preserves units"), Roster->GetUnits().Num(), 1);
    TestEqual(TEXT("Offer becomes empty"), Shop->GetOffers()[0].UnitId, NAME_None);
    TestEqual(TEXT("Repeat purchase rejected"), Transactions->RequestBuy(0, Revision), ETransactionResult::InvalidSelection);
    TestEqual(TEXT("Place tank"), Transactions->RequestMove(Kept, Front), ETransactionResult::Success);
    TestTrue(TEXT("Combat"), Shop->BeginCombat());
    TestEqual(TEXT("Cannot sell deployed unit"), Transactions->RequestSell(Kept), ETransactionResult::WrongPhase);
    TestEqual(TEXT("Cannot move deployed unit"), Transactions->RequestMove(Kept, Second), ETransactionResult::WrongPhase);

    const FGuid Defeated = FGuid::NewGuid();
    TestTrue(TEXT("Kill reward"), Transactions->GrantKillReward(Defeated));
    TestFalse(TEXT("Kill reward once"), Transactions->GrantKillReward(Defeated));
    TestEqual(TEXT("Configured kill gold"), Shop->GetGold(), 22);
    TestEqual(TEXT("One drop"), Items->GetInventory().Num(), 1);
    TestEqual(TEXT("Equip blocked in combat"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::WrongPhase);
    TestEqual(TEXT("Combat buy 1"), Transactions->RequestBuy(1, Revision), ETransactionResult::Success);
    TestEqual(TEXT("Combat buy 2"), Transactions->RequestBuy(2, Revision), ETransactionResult::Success);
    TestEqual(TEXT("Merge waits for preparation"), Roster->GetUnits().Num(), 3);
    TestEqual(TEXT("Lock remaining stock"), Shop->RequestShopLock(true), ETransactionResult::Success);
    TestTrue(TEXT("Settlement"), Shop->BeginSettlement());
    TestEqual(TEXT("No reroll in settlement"), Shop->RequestReroll(), ETransactionResult::WrongPhase);
    TestTrue(TEXT("Round gold"), Shop->GrantRoundGold(4));
    TestFalse(TEXT("Round gold once"), Shop->GrantRoundGold(4));
    TestTrue(TEXT("Four reward choices"), Transactions->BeginRewardSelection(3));
    TestEqual(TEXT("Reward round applies next limit"), Shop->GetDeploymentLimit(), 3);
    TestEqual(TEXT("Cannot buy during reward"), Transactions->RequestBuy(3, Revision), ETransactionResult::WrongPhase);
    TestFalse(TEXT("Cannot start during reward"), Shop->BeginCombat());
    TestFalse(TEXT("Cannot skip reward"), Transactions->EnterPreparation(2, 3));

    const auto Rewards = Shop->GetRewards();
    if (!TestEqual(TEXT("Four cards"), Rewards.Num(), 4)) return false;
    const int32 RewardSlot = Rewards.IndexOfByPredicate([](const FRewardOffer& Reward) { return Reward.Kind == ERewardKind::Item; });
    const int32 RewardRevision = Shop->GetRewardRevision();
    Board->bReject = true;
    TestEqual(TEXT("Rejected equip"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::ChangeRejected);
    TestEqual(TEXT("Rejected equip keeps inventory"), Items->GetInventory().Num(), 1);
    TestEqual(TEXT("Rejected equip keeps loadout"), Items->GetEquipment(Kept).Num(), 0);
    Board->bReject = false;
    TestEqual(TEXT("Equip during reward"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::Success);
    TestEqual(TEXT("Take one reward"), Transactions->RequestReward(RewardSlot, RewardRevision), ETransactionResult::Success);
    TestEqual(TEXT("No duplicate reward"), Transactions->RequestReward(RewardSlot, RewardRevision), ETransactionResult::InvalidSelection);

    Board->bReject = true;
    TestFalse(TEXT("Rejected merge does not enter preparation"), Transactions->EnterPreparation(2, 3));
    TestEqual(TEXT("Rejected merge preserves all units"), Roster->GetUnits().Num(), 3);
    Board->bReject = false;
    TestTrue(TEXT("Enter next preparation"), Transactions->EnterPreparation(2, 3));
    FRosterUnit Upgraded;
    TestTrue(TEXT("Board unit retained"), Roster->GetUnit(Kept, Upgraded));
    TestEqual(TEXT("Placement retained"), Roster->GetPosition(Kept).Column, Front.Column);
    TestEqual(TEXT("Merged into one two-star"), Upgraded.Stars, 2);
    TestEqual(TEXT("Item inherited"), Items->GetEquipment(Upgraded.Id).Num(), 1);
    TestEqual(TEXT("Materials removed"), Roster->GetUnits().Num(), 1);
    TestEqual(TEXT("Locked shop retains its revision"), Shop->GetShopRevision(), Revision);
    TestEqual(TEXT("Locked empty slot stays empty"), Shop->GetOffers()[0].UnitId, NAME_None);
    TestEqual(TEXT("Duplicate equipment allowed"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::Success);

    Board->bReject = true;
    const int32 GoldBeforeSale = Shop->GetGold();
    TestEqual(TEXT("Rejected sale"), Transactions->RequestSell(Kept), ETransactionResult::ChangeRejected);
    TestEqual(TEXT("Rejected sale keeps gold"), Shop->GetGold(), GoldBeforeSale);
    TestEqual(TEXT("Rejected sale returns no equipment"), Items->GetInventory().Num(), 0);
    TestTrue(TEXT("Rejected sale keeps tile"), Roster->FindOccupant(Front) == Kept);
    Board->bReject = false;

    UBenchManager* Bench = Roster->GetBench();
    TestEqual(TEXT("Seven bench slots"), Bench->GetSlotCount(), 7);
    TestEqual(TEXT("Invalid bench slot rejected"), Transactions->RequestMoveToBench(Kept, 7), ETransactionResult::InvalidSelection);
    TestEqual(TEXT("Recall returns both items"), Transactions->RequestMoveToBench(Kept, 0), ETransactionResult::Success);
    TestEqual(TEXT("Bench reads board-owned occupancy"), Bench->GetOccupant(0), Kept);
    TestEqual(TEXT("Both equipment returned"), Items->GetInventory().Num(), 2);
    TestEqual(TEXT("Re-deploy"), Transactions->RequestMove(Kept, Front), ETransactionResult::Success);
    TestEqual(TEXT("Re-equip"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::Success);
    TestEqual(TEXT("Successful board sale"), Transactions->RequestSell(Kept), ETransactionResult::Success);
    TestEqual(TEXT("Board sale returns equipment"), Items->GetInventory().Num(), 2);
    TestEqual(TEXT("Board sale frees tile"), Roster->FindOccupant(Front), FGuid());
    TestEqual(TEXT("Successful refund once"), Shop->GetGold(), GoldBeforeSale + 1);
    TestEqual(TEXT("Successful reroll"), Shop->RequestReroll(), ETransactionResult::Success);
    TestEqual(TEXT("Reroll costs exactly two"), Shop->GetGold(), GoldBeforeSale - 1);
    TestEqual(TEXT("Old shop display rejected"), Transactions->RequestBuy(0, Revision), ETransactionResult::InvalidSelection);
    if (!TestTrue(TEXT("Fresh zero-gold match"), StartMatch(0))) return false;
    TestTrue(TEXT("Zero gold preparation"), Transactions->EnterPreparation(1, 2));
    TestEqual(TEXT("Insufficient gold"), Transactions->RequestBuy(0, Shop->GetShopRevision()), ETransactionResult::InsufficientGold);
    TestEqual(TEXT("Insufficient gold preserves offer"), Shop->GetOffers()[0].UnitId, UnitType);
    if (!TestTrue(TEXT("Fresh match for full bench"), StartMatch(20))) return false;
    Bench = Roster->GetBench();
    for (int32 Slot = 0; Slot < Bench->GetSlotCount(); ++Slot)
        TestEqual(TEXT("Starting unit on bench"), Transactions->GrantStartingUnit(Slot % 2 == 0 ? UnitType : OtherType), ETransactionResult::Success);

    TestTrue(TEXT("Preparation processes initial triples"), Transactions->EnterPreparation(1, 2));
    TestEqual(TEXT("Two independent triples merged atomically"), Roster->GetUnits().Num(), 3);
    TestTrue(TEXT("Combat allows filling bench"), Shop->BeginCombat());
    for (int32 Slot = 0; Slot < 4; ++Slot)
        TestEqual(TEXT("Fill bench"), Transactions->RequestBuy(Slot, Shop->GetShopRevision()), ETransactionResult::Success);

    TestEqual(TEXT("Bench reports full"), Bench->FindFreeSlot().Location, EUnitLocation::None);
    const FGuid First = Bench->GetOccupant(0);
    const FGuid SecondUnit = Bench->GetOccupant(1);
    if (!TestTrue(TEXT("First two slots occupied"), First.IsValid() && SecondUnit.IsValid())) return false;
    TestEqual(TEXT("Bench swap during combat"), Transactions->RequestMoveToBench(First, 1), ETransactionResult::Success);
    TestEqual(TEXT("First unit moves to second slot"), Bench->GetOccupant(1), First);
    TestEqual(TEXT("Second unit moves to first slot"), Bench->GetOccupant(0), SecondUnit);

    const int32 FilledGold = Shop->GetGold();
    TestEqual(TEXT("Full bench blocks purchase before pending merge"), Transactions->RequestBuy(4, Shop->GetShopRevision()), ETransactionResult::NoSpace);
    TestEqual(TEXT("Full bench keeps gold"), Shop->GetGold(), FilledGold);
    TestEqual(TEXT("Full bench keeps offer"), Shop->GetOffers()[4].UnitId, UnitType);
    TestTrue(TEXT("Settle full bench round"), Shop->BeginSettlement());
    TestTrue(TEXT("Open rewards with full bench"), Transactions->BeginRewardSelection(3));
    Board->AvailableBoardTiles = 0;
    const auto FullBenchRewards = Shop->GetRewards();
    const int32 UnitReward = FullBenchRewards.IndexOfByPredicate([](const FRewardOffer& Reward) { return Reward.Kind == ERewardKind::Unit; });
    TestEqual(TEXT("Full board and bench rejects reward"), Transactions->RequestReward(UnitReward, Shop->GetRewardRevision()), ETransactionResult::NoSpace);
    TestEqual(TEXT("All four offers remain after rejection"), Shop->GetRewards().Num(), 4);
    Board->AvailableBoardTiles = 1;
    TestEqual(TEXT("Reward prioritizes an open board tile"), Transactions->RequestReward(UnitReward, Shop->GetRewardRevision()), ETransactionResult::Success);
    TestTrue(TEXT("Reward occupies the board"), Roster->FindOccupant(Front).IsValid());
    TestEqual(TEXT("Unselected rewards disappear"), Shop->GetRewards().Num(), 0);

    if (!TestTrue(TEXT("Fresh match for equipment overflow"), StartMatch(20))) return false;
    Board->AvailableBoardTiles = 2;
    TestTrue(TEXT("Start overflow round"), Transactions->EnterPreparation(1, 2));
    const int32 OverflowRevision = Shop->GetShopRevision();
    TestEqual(TEXT("Buy first equipped material"), Transactions->RequestBuy(0, OverflowRevision), ETransactionResult::Success);
    TestEqual(TEXT("Buy second equipped material"), Transactions->RequestBuy(1, OverflowRevision), ETransactionResult::Success);
    const auto Materials = Roster->GetUnits();
    if (!TestEqual(TEXT("Two materials"), Materials.Num(), 2)) return false;
    TestEqual(TEXT("Place first material"), Transactions->RequestMove(Materials[0].Id, Front), ETransactionResult::Success);
    TestEqual(TEXT("Place second material"), Transactions->RequestMove(Materials[1].Id, Second), ETransactionResult::Success);
    TestTrue(TEXT("Begin combat for item drops"), Shop->BeginCombat());
    for (int32 Kill = 0; Kill < 4; ++Kill)
        TestTrue(TEXT("Drop item for merge"), Transactions->GrantKillReward(FGuid::NewGuid()));
    TestEqual(TEXT("Buy pending third material"), Transactions->RequestBuy(2, OverflowRevision), ETransactionResult::Success);
    TestTrue(TEXT("Settle overflow round"), Shop->BeginSettlement());
    TestTrue(TEXT("Open reward before overflow merge"), Transactions->BeginRewardSelection(3));
    for (const FRosterUnit& Material : Materials)
        for (int32 Slot = 0; Slot < 2; ++Slot)
            TestEqual(TEXT("Equip both board materials"), Transactions->RequestEquip(Material.Id, ItemId), ETransactionResult::Success);
    const int32 OverflowReward = Shop->GetRewards().IndexOfByPredicate([](const FRewardOffer& Reward)
    {
        return Reward.Kind == ERewardKind::Item;
    });
    TestEqual(TEXT("Grant one reward item"), Transactions->RequestReward(OverflowReward, Shop->GetRewardRevision()), ETransactionResult::Success);
    TestTrue(TEXT("Resolve equipment overflow"), Transactions->EnterPreparation(2, 3));
    TestTrue(TEXT("First deployed material keeps its slot"), Roster->GetUnit(Materials[0].Id, Upgraded));
    TestEqual(TEXT("Exactly three items inherited"), Items->GetEquipment(Upgraded.Id).Num(), 3);
    TestEqual(TEXT("Overflow item joins reward item"), Items->GetInventory().Num(), 2);
    TestEqual(TEXT("Other board slot freed"), Roster->FindOccupant(Second), FGuid());

    if (!TestTrue(TEXT("Fresh match for immediate merge"), StartMatch(20))) return false;
    TestTrue(TEXT("Start final preparation"), Transactions->EnterPreparation(1, 2));
    for (int32 Slot = 0; Slot < 3; ++Slot)
        TestEqual(TEXT("Preparation buy"), Transactions->RequestBuy(Slot, Shop->GetShopRevision()), ETransactionResult::Success);
    const auto Merged = Roster->GetUnits();
    if (!TestEqual(TEXT("Immediate bench merge"), Merged.Num(), 1)) return false;
    TestEqual(TEXT("Immediate merge stars"), Merged[0].Stars, 2);
    TestEqual(TEXT("Immediate merge stays on bench"), Roster->GetPosition(Merged[0].Id).Location, EUnitLocation::Bench);
    if (!TestTrue(TEXT("Fresh match for full-bench preparation merge"), StartMatch(20))) return false;
    Bench = Roster->GetBench();
    for (int32 Index = 0; Index < 2; ++Index)
        TestEqual(TEXT("Grant matching material"), Transactions->GrantStartingUnit(UnitType), ETransactionResult::Success);
    for (int32 Slot = 2; Slot < Bench->GetSlotCount(); ++Slot)
    {
        const FName Filler(*FString::Printf(TEXT("Filler%d"), Slot));
        Shop->Catalog.Add({ Filler, FText::FromName(Filler), 2 });
        TestEqual(TEXT("Grant filler"), Transactions->GrantStartingUnit(Filler), ETransactionResult::Success);
    }

    if (!TestTrue(TEXT("Enter full-bench preparation"), Transactions->EnterPreparation(1, 2))) return false;
    TestEqual(TEXT("Preparation starts with a full bench"), Bench->FindFreeSlot().Location, EUnitLocation::None);
    const FGuid FirstMaterial = Bench->GetOccupant(0);
    const FGuid SecondMaterial = Bench->GetOccupant(1);
    const int32 FullBenchRevision = Shop->GetShopRevision();

    Board->bReject = true;
    TestEqual(TEXT("Failed full-bench merge rejects purchase"), Transactions->RequestBuy(0, FullBenchRevision), ETransactionResult::ChangeRejected);
    TestEqual(TEXT("Failed merge preserves gold"), Shop->GetGold(), 20);
    TestEqual(TEXT("Failed merge preserves offer"), Shop->GetOffers()[0].UnitId, UnitType);
    TestEqual(TEXT("Failed merge preserves both materials"), Bench->GetOccupant(1), SecondMaterial);
    Board->bReject = false;

    TestEqual(TEXT("Buy third copy at full bench"), Transactions->RequestBuy(0, FullBenchRevision), ETransactionResult::Success);
    TestEqual(TEXT("Purchase charges once"), Shop->GetGold(), 19);
    TestEqual(TEXT("Purchased slot clears"), Shop->GetOffers()[0].UnitId, NAME_None);
    TestEqual(TEXT("Merge frees one bench slot"), Bench->FindFreeSlot().Column, 1);
    TestEqual(TEXT("Merge leaves six units"), Roster->GetUnits().Num(), 6);
    TestTrue(TEXT("First material retains its slot"), Bench->GetOccupant(0) == FirstMaterial);
    TestTrue(TEXT("Kept material is on the roster"), Roster->GetUnit(FirstMaterial, Upgraded));
    TestEqual(TEXT("Kept material is two-star"), Upgraded.Stars, 2);

    TestEqual(TEXT("Purchase fills the freed slot"), Transactions->RequestBuy(1, FullBenchRevision), ETransactionResult::Success);
    TestEqual(TEXT("Full bench without an immediate merge blocks purchase"), Transactions->RequestBuy(2, FullBenchRevision), ETransactionResult::NoSpace);
    TestEqual(TEXT("Blocked purchase keeps gold"), Shop->GetGold(), 18);
    TestEqual(TEXT("Blocked purchase keeps offer"), Shop->GetOffers()[2].UnitId, UnitType);
    return true;
}
#endif

