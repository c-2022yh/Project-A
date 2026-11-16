#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "../RosterManager.h"
#include "../BenchManager.h"
#include "../ShopManager.h"
#include "../ItemManager.h"
#include "../ShopTransactionManager.h"
#include "../ShopOdds.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "TestPlacement.h"
#include "ShopTestGameMode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopTransactionsTest, "ProjectA.Shop.Transactions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FShopTransactionsTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Options;
    Options.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).AllowAudioPlayback(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, false, ERHIFeatureLevel::Num, &Options);
    if (!TestNotNull(TEXT("World"), World)) return false;
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    Context.OwningGameInstance = NewObject<UGameInstance>(GEngine);
    World->SetGameInstance(Context.OwningGameInstance);
    ON_SCOPE_EXIT
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };
    FURL URL;
    URL.AddOption(TEXT("game=/Script/Project_A.ShopTestGameMode"));
    World->SetGameMode(URL);
    World->InitializeActorsForPlay(URL);
    auto* Mode = Cast<AShopTestGameMode>(World->GetAuthGameMode());
    if (!TestNotNull(TEXT("Authoritative test GameMode"), Mode)) return false;

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
        Mode->Match = {};
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

    const auto Prepare = [&](int32 Round, int32 Limit)
    {
        Mode->Match = { EShopPhase::Preparation, Round, Limit };
        return Transactions->EnterPreparation();
    };
    const auto PlaceOnBoard = [&](FGuid UnitId, FUnitPosition Destination)
    {
        FRosterUnit Unit;
        if (!Mode->Match.CanManageBoard() || !Roster->GetUnit(UnitId, Unit))
            return ETransactionResult::WrongPhase;
        FRosterUnitChange Move;
        Move.Edit = ERosterUnitEdit::Move;
        Move.Unit = Unit;
        Move.Destination = Destination;
        return Roster->GetBench()->ApplyChanges({ Move }, Mode->Match)
            ? ETransactionResult::Success : ETransactionResult::ChangeRejected;
    };
    const auto EquipmentCount = [&](FGuid UnitId)
    {
        TArray<FName> Equipment;
        return Roster->GetUnitEquipment(UnitId, Equipment) ? Equipment.Num() : INDEX_NONE;
    };

    if (!TestTrue(TEXT("Configure match"), StartMatch(20))) return false;
    TestEqual(TEXT("Gold remains unset before preparation"), Shop->GetGold(), 0);
    if (!TestTrue(TEXT("Preparation"), Prepare(1, 2))) return false;
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
        TestEqual(TEXT("Nested reroll rejected"), Transactions->RequestReroll(), ETransactionResult::Unavailable);
        TestEqual(TEXT("Purchase is not charged before placement succeeds"), Shop->GetGold(), 20);
    };
    if (!TestEqual(TEXT("Buy first copy"), Transactions->RequestBuy(0, Revision), ETransactionResult::Success)) return false;
    Board->OnApply = nullptr;
    const FGuid Kept = Roster->GetUnits()[0].Id;
    TestFalse(TEXT("First preparation cannot be repeated"), Prepare(1, 2));
    TestEqual(TEXT("Repeated preparation preserves gold"), Shop->GetGold(), 19);
    TestEqual(TEXT("Repeated preparation preserves units"), Roster->GetUnits().Num(), 1);
    TestEqual(TEXT("Offer becomes empty"), Shop->GetOffers()[0].UnitId, NAME_None);
    TestEqual(TEXT("Repeat purchase rejected"), Transactions->RequestBuy(0, Revision), ETransactionResult::InvalidSelection);
    TestEqual(TEXT("Purchase enters bench slot zero"), Roster->GetBench()->GetOccupant(0), Kept);
    TestFalse(TEXT("Purchase leaves an open board tile empty"), Roster->FindOccupant(Front).IsValid());
    TestEqual(TEXT("Board input places tank"), PlaceOnBoard(Kept, Front), ETransactionResult::Success);
    TestFalse(TEXT("Deployment releases the bench slot"), Roster->GetBench()->GetOccupant(0).IsValid());
    Mode->Match.Phase = EShopPhase::Combat;
    TestEqual(TEXT("Cannot sell deployed unit"), Transactions->RequestSell(Kept), ETransactionResult::WrongPhase);
    TestEqual(TEXT("Cannot move deployed unit"), PlaceOnBoard(Kept, Second), ETransactionResult::WrongPhase);

    const FGuid Defeated = FGuid::NewGuid();
    TestTrue(TEXT("Kill reward"), Transactions->GrantKillReward(Defeated));
    TestFalse(TEXT("Kill reward once"), Transactions->GrantKillReward(Defeated));
    TestEqual(TEXT("Configured kill gold"), Shop->GetGold(), 22);
    TestEqual(TEXT("One drop"), Items->GetInventory().Num(), 1);
    TestEqual(TEXT("Equip blocked in combat"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::WrongPhase);
    TestEqual(TEXT("Combat buy 1"), Transactions->RequestBuy(1, Revision), ETransactionResult::Success);
    TestEqual(TEXT("Combat buy 2"), Transactions->RequestBuy(2, Revision), ETransactionResult::Success);
    TestEqual(TEXT("Merge waits for preparation"), Roster->GetUnits().Num(), 3);
    TestEqual(TEXT("Lock remaining stock"), Transactions->RequestShopLock(true), ETransactionResult::Success);
    Mode->Match.Phase = EShopPhase::Settlement;
    TestEqual(TEXT("No reroll in settlement"), Transactions->RequestReroll(), ETransactionResult::WrongPhase);
    TestTrue(TEXT("Round gold"), Transactions->GrantRoundGold(4));
    TestFalse(TEXT("Round gold once"), Transactions->GrantRoundGold(4));
    Mode->Match = { EShopPhase::RewardSelection, 1, 3 };
    TestTrue(TEXT("Four reward choices"), Transactions->OpenRewards());
    TestEqual(TEXT("Limit is read from GameMode"), Transactions->GetMatchState().DeploymentLimit, 3);
    TestEqual(TEXT("Cannot buy during reward"), Transactions->RequestBuy(3, Revision), ETransactionResult::WrongPhase);
    TestFalse(TEXT("Cannot skip reward"), Prepare(2, 3));
    Mode->Match = { EShopPhase::RewardSelection, 1, 3 };

    const auto Rewards = Shop->GetRewards();
    if (!TestEqual(TEXT("Four cards"), Rewards.Num(), 4)) return false;
    const int32 RewardSlot = Rewards.IndexOfByPredicate([](const FRewardOffer& Reward) { return Reward.Kind == ERewardKind::Item; });
    const int32 RewardRevision = Shop->GetRewardRevision();
    Board->bReject = true;
    TestEqual(TEXT("Rejected equip"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::ChangeRejected);
    TestEqual(TEXT("Rejected equip keeps inventory"), Items->GetInventory().Num(), 1);
    TestEqual(TEXT("Rejected equip keeps loadout"), EquipmentCount(Kept), 0);
    Board->bReject = false;
    Board->bEquipmentApiAvailable = false;
    TestEqual(TEXT("Missing unit equipment API rejects equip"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::Unavailable);
    TestEqual(TEXT("Missing API preserves inventory"), Items->GetInventory().Num(), 1);
    Board->bEquipmentApiAvailable = true;
    TestEqual(TEXT("Equip during reward"), Transactions->RequestEquip(Kept, ItemId), ETransactionResult::Success);
    TestEqual(TEXT("Take one reward"), Transactions->RequestReward(RewardSlot, RewardRevision), ETransactionResult::Success);
    TestEqual(TEXT("No duplicate reward"), Transactions->RequestReward(RewardSlot, RewardRevision), ETransactionResult::InvalidSelection);

    Board->bReject = true;
    TestFalse(TEXT("Rejected merge does not enter preparation"), Prepare(2, 3));
    TestEqual(TEXT("Rejected merge preserves all units"), Roster->GetUnits().Num(), 3);
    Board->bReject = false;
    TestTrue(TEXT("Enter next preparation"), Prepare(2, 3));
    FRosterUnit Upgraded;
    TestTrue(TEXT("Board unit retained"), Roster->GetUnit(Kept, Upgraded));
    TestEqual(TEXT("Placement retained"), Roster->GetPosition(Kept).Column, Front.Column);
    TestEqual(TEXT("Merged into one two-star"), Upgraded.Stars, 2);
    TestEqual(TEXT("Item inherited"), EquipmentCount(Upgraded.Id), 1);
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
    TestEqual(TEXT("Invalid bench slot rejected"), Bench->RequestMove(Kept, 7), ETransactionResult::InvalidSelection);
    TestEqual(TEXT("Unequip returns one item"), Transactions->RequestUnequip(Kept, 0), ETransactionResult::Success);
    TestEqual(TEXT("One item is still on the unit"), EquipmentCount(Kept), 1);
    TestEqual(TEXT("One item is back in inventory"), Items->GetInventory().Num(), 1);
    TestEqual(TEXT("Successful board sale"), Transactions->RequestSell(Kept), ETransactionResult::Success);
    TestEqual(TEXT("Board sale returns equipment"), Items->GetInventory().Num(), 2);
    TestEqual(TEXT("Board sale frees tile"), Roster->FindOccupant(Front), FGuid());
    TestEqual(TEXT("Successful refund once"), Shop->GetGold(), GoldBeforeSale + 1);
    TestEqual(TEXT("Successful reroll"), Transactions->RequestReroll(), ETransactionResult::Success);
    TestEqual(TEXT("Reroll costs exactly two"), Shop->GetGold(), GoldBeforeSale - 1);
    TestEqual(TEXT("Old shop display rejected"), Transactions->RequestBuy(0, Revision), ETransactionResult::InvalidSelection);
    if (!TestTrue(TEXT("Fresh zero-gold match"), StartMatch(0))) return false;
    TestTrue(TEXT("Zero gold preparation"), Prepare(1, 2));
    TestEqual(TEXT("Insufficient gold"), Transactions->RequestBuy(0, Shop->GetShopRevision()), ETransactionResult::InsufficientGold);
    TestEqual(TEXT("Insufficient gold preserves offer"), Shop->GetOffers()[0].UnitId, UnitType);
    if (!TestTrue(TEXT("Fresh match for full bench"), StartMatch(20))) return false;
    Bench = Roster->GetBench();
    for (int32 Slot = 0; Slot < Bench->GetSlotCount(); ++Slot)
        TestEqual(TEXT("Starting unit on bench"), Transactions->GrantStartingUnit(Slot % 2 == 0 ? UnitType : OtherType), ETransactionResult::Success);

    TestTrue(TEXT("Preparation processes initial triples"), Prepare(1, 2));
    TestEqual(TEXT("Two independent triples merged atomically"), Roster->GetUnits().Num(), 3);
    Mode->Match.Phase = EShopPhase::Combat;
    for (int32 Slot = 0; Slot < 4; ++Slot)
        TestEqual(TEXT("Fill bench"), Transactions->RequestBuy(Slot, Shop->GetShopRevision()), ETransactionResult::Success);

    TestEqual(TEXT("Bench reports full"), Bench->FindFreeSlot().Location, EUnitLocation::None);
    const FGuid First = Bench->GetOccupant(0);
    const FGuid SecondUnit = Bench->GetOccupant(1);
    if (!TestTrue(TEXT("First two slots occupied"), First.IsValid() && SecondUnit.IsValid())) return false;
    TestEqual(TEXT("Bench swap during combat"), Bench->RequestMove(First, 1), ETransactionResult::Success);
    TestEqual(TEXT("First unit moves to second slot"), Bench->GetOccupant(1), First);
    TestEqual(TEXT("Second unit moves to first slot"), Bench->GetOccupant(0), SecondUnit);

    const int32 FilledGold = Shop->GetGold();
    TestEqual(TEXT("Full bench blocks purchase before pending merge"), Transactions->RequestBuy(4, Shop->GetShopRevision()), ETransactionResult::NoSpace);
    TestEqual(TEXT("Full bench keeps gold"), Shop->GetGold(), FilledGold);
    TestEqual(TEXT("Full bench keeps offer"), Shop->GetOffers()[4].UnitId, UnitType);
    Mode->Match.Phase = EShopPhase::Settlement;
    Mode->Match = { EShopPhase::RewardSelection, 1, 3 };
    TestTrue(TEXT("Open rewards with full bench"), Transactions->OpenRewards());

    const auto FullBenchRewards = Shop->GetRewards();
    const int32 UnitReward = FullBenchRewards.IndexOfByPredicate([](const FRewardOffer& Reward) { return Reward.Kind == ERewardKind::Unit; });
    TestEqual(TEXT("Full bench rejects reward despite open board"), Transactions->RequestReward(UnitReward, Shop->GetRewardRevision()), ETransactionResult::NoSpace);
    TestEqual(TEXT("All four offers remain after rejection"), Shop->GetRewards().Num(), 4);
    TestEqual(TEXT("Sell a bench unit for reward space"), Transactions->RequestSell(First), ETransactionResult::Success);
    TestEqual(TEXT("Retry the same unit reward"), Transactions->RequestReward(UnitReward, Shop->GetRewardRevision()), ETransactionResult::Success);
    TestTrue(TEXT("Reward occupies the vacated bench slot"), Bench->GetOccupant(1).IsValid() && Bench->GetOccupant(1) != First);
    TestFalse(TEXT("Reward leaves board empty"), Roster->FindOccupant(Front).IsValid());
    TestEqual(TEXT("Unselected rewards disappear"), Shop->GetRewards().Num(), 0);

    if (!TestTrue(TEXT("Fresh match for equipment overflow"), StartMatch(20))) return false;
    Board->AvailableBoardTiles = 2;
    TestTrue(TEXT("Start overflow round"), Prepare(1, 2));
    const int32 OverflowRevision = Shop->GetShopRevision();
    TestEqual(TEXT("Buy first equipped material"), Transactions->RequestBuy(0, OverflowRevision), ETransactionResult::Success);
    TestEqual(TEXT("Buy second equipped material"), Transactions->RequestBuy(1, OverflowRevision), ETransactionResult::Success);
    const auto Materials = Roster->GetUnits();
    if (!TestEqual(TEXT("Two materials"), Materials.Num(), 2)) return false;
    TestEqual(TEXT("Place first material"), PlaceOnBoard(Materials[0].Id, Front), ETransactionResult::Success);
    TestEqual(TEXT("Place second material"), PlaceOnBoard(Materials[1].Id, Second), ETransactionResult::Success);
    Mode->Match.Phase = EShopPhase::Combat;
    for (int32 Kill = 0; Kill < 4; ++Kill)
        TestTrue(TEXT("Drop item for merge"), Transactions->GrantKillReward(FGuid::NewGuid()));
    TestEqual(TEXT("Buy pending third material"), Transactions->RequestBuy(2, OverflowRevision), ETransactionResult::Success);
    Mode->Match.Phase = EShopPhase::Settlement;
    Mode->Match = { EShopPhase::RewardSelection, 1, 3 };
    TestTrue(TEXT("Open reward before overflow merge"), Transactions->OpenRewards());
    for (const FRosterUnit& Material : Materials)
        for (int32 Slot = 0; Slot < 2; ++Slot)
            TestEqual(TEXT("Equip both board materials"), Transactions->RequestEquip(Material.Id, ItemId), ETransactionResult::Success);
    const int32 OverflowReward = Shop->GetRewards().IndexOfByPredicate([](const FRewardOffer& Reward)
    {
        return Reward.Kind == ERewardKind::Item;
    });
    TestEqual(TEXT("Grant one reward item"), Transactions->RequestReward(OverflowReward, Shop->GetRewardRevision()), ETransactionResult::Success);
    TestTrue(TEXT("Resolve equipment overflow"), Prepare(2, 3));
    TestTrue(TEXT("First deployed material keeps its slot"), Roster->GetUnit(Materials[0].Id, Upgraded));
    TestEqual(TEXT("Exactly three items inherited"), EquipmentCount(Upgraded.Id), 3);
    TestEqual(TEXT("Overflow item joins reward item"), Items->GetInventory().Num(), 2);
    TestEqual(TEXT("Other board slot freed"), Roster->FindOccupant(Second), FGuid());

    if (!TestTrue(TEXT("Fresh match for immediate merge"), StartMatch(20))) return false;
    TestTrue(TEXT("Start final preparation"), Prepare(1, 2));
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

    if (!TestTrue(TEXT("Enter full-bench preparation"), Prepare(1, 2))) return false;
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

