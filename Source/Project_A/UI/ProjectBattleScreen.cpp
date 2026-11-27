#include "ProjectBattleScreen.h"
#include "BasicButton.h"
#include "ProjectUITheme.h"
#include "../AutoBattleGameMode.h"
#include "../AutoBattlePlayerController.h"
#include "../BattleUnitBase.h"
#include "../BenchTile.h"
#include "../BoardTile.h"
#include "../Shop/ItemManager.h"
#include "../Shop/MainMapShopPlacement.h"
#include "../Shop/RosterManager.h"
#include "../Shop/ShopManager.h"
#include "../Shop/ShopTransactionManager.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "ProjectBattleScreen"

namespace
{
void Show(UWidget* Widget, bool bVisible)
{
    if (Widget) Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void Fill(UCanvasPanel& Canvas, UWidget& Widget)
{
    auto* Slot = Canvas.AddChildToCanvas(&Widget);
    Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    Slot->SetOffsets(FMargin(0.f));
}

void Stack(UVerticalBox& Box, UWidget* Widget, float Top = UProjectUITheme::Gap)
{
    Box.AddChildToVerticalBox(Widget)->SetPadding(FMargin(0.f, Top, 0.f, 0.f));
}

void Row(UHorizontalBox& Box, UWidget* Widget, bool bFill = false)
{
    auto* Slot = Box.AddChildToHorizontalBox(Widget);
    Slot->SetPadding(FMargin(0.f, 0.f, UProjectUITheme::Gap, 0.f));
    if (bFill) Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

USizeBox* Sized(UWidgetTree& Tree, UWidget* Content, float Width, float Height)
{
    auto* Box = Tree.ConstructWidget<USizeBox>();
    Box->SetWidthOverride(Width);
    Box->SetHeightOverride(Height);
    Box->SetContent(Content);
    return Box;
}

FText UnitName(const AShopManager& Shop, FName Id)
{
    const auto Offer = Shop.GetOfferDefinition(Id);
    return Offer.Label.IsEmpty() ? FText::FromName(Id) : Offer.Label;
}

int32 UnitCost(const AShopTransactionManager& Manager, FName Id, int32 Fallback)
{
    const auto* Placement = Cast<UMainMapShopPlacement>(Manager.Roster->Placement.GetObject());
    const UDataTable* Table = Placement ? Placement->GetUnitDefinitions() : nullptr;
    const auto* Unit = Table ? Table->FindRow<FUnitDefinition>(Id, TEXT("Shop card"), false) : nullptr;
    return Unit ? Unit->Cost : Fallback;
}

bool ContainsPoint(const UWidget* Widget, FVector2D Absolute)
{
    return Widget && Widget->GetVisibility() == ESlateVisibility::Visible
        && Widget->GetCachedGeometry().IsUnderLocation(Absolute);
}
}

UBasicButton* UProjectBattleScreen::MakeButton(const FText& Label, TFunction<void()> Action)
{
    auto* Button = CreateWidget<UBasicButton>(GetOwningPlayer());
    Button->SetTheme(*Theme);
    Button->SetLabel(Label);
    Button->Action = MoveTemp(Action);
    return Button;
}

TSharedRef<SWidget> UProjectBattleScreen::RebuildWidget()
{
    if (!WidgetTree || WidgetTree->RootWidget) return Super::RebuildWidget();

    Theme = NewObject<UProjectUITheme>(this);
    Theme->Initialize();
    auto& Tree = *WidgetTree;
    auto* Canvas = Tree.ConstructWidget<UCanvasPanel>();
    Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Canvas->SetClipping(EWidgetClipping::ClipToBounds);
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Tree.RootWidget = Canvas;

    DropSurface = Tree.ConstructWidget<UBorder>();
    DropSurface->SetBrushColor(FLinearColor::Transparent);
    Fill(*Canvas, *DropSurface);
    Show(DropSurface, false);

    auto* Status = Tree.ConstructWidget<UHorizontalBox>();
    Round = UProjectUITheme::Label(Tree, {}, 16, UProjectUITheme::Gold, true);
    Row(*Status, Round);
    StartCombat = MakeButton(LOCTEXT("Start", "전투 시작"), [this]
    {
        if (!IsValid(Transactions) || bItemDragging) return;
        const auto* Placement = Cast<UMainMapShopPlacement>(Transactions->Roster->Placement.GetObject());
        auto* PC = Cast<AAutoBattlePlayerController>(GetOwningPlayer());
        if (Placement && PC) Placement->RequestCombatStart(*PC);
    });
    Row(*Status, Sized(Tree, StartCombat, 128.f, 38.f));
    StatusFrame = UProjectUITheme::Frame(Tree, Status, FMargin(10.f, 6.f));
    UProjectUITheme::Pin(*Canvas, *StatusFrame, FVector2D(0.5, 0), FVector2D(0.5, 0), FVector2D(0, 14));

    auto* Shop = Tree.ConstructWidget<UVerticalBox>();
    auto* Toolbar = Tree.ConstructWidget<UHorizontalBox>();
    Gold = UProjectUITheme::Label(Tree, {}, 19, UProjectUITheme::Gold, true);
    Row(*Toolbar, Gold, true);
    Reroll = MakeButton(LOCTEXT("Reroll", "리롤  2G"), [this]
    {
        if (IsValid(Transactions)) Report(Transactions->RequestReroll());
    });
    Row(*Toolbar, Sized(Tree, Reroll, 108.f, 38.f));
    Lock = MakeButton(LOCTEXT("Lock", "잠금"), [this]
    {
        if (IsValid(Transactions)) Report(Transactions->RequestShopLock(!Transactions->Shop->IsShopLocked()));
    });
    Row(*Toolbar, Sized(Tree, Lock, 116.f, 38.f));
    Stack(*Shop, Toolbar, 0.f);
    Offers = Tree.ConstructWidget<UHorizontalBox>();
    Stack(*Shop, Offers);
    ShopFrame = UProjectUITheme::Frame(Tree, Shop);
    UProjectUITheme::Pin(*Canvas, *ShopFrame, FVector2D(0.5, 1), FVector2D(0.5, 1), FVector2D(0, -14));

    auto* Reward = Tree.ConstructWidget<UVerticalBox>();
    RewardSummary = UProjectUITheme::Label(Tree, {}, 21, UProjectUITheme::Gold, true);
    Stack(*Reward, RewardSummary, 0.f);
    Stack(*Reward, UProjectUITheme::Label(Tree, LOCTEXT("Choose", "보상 하나를 선택하세요"), 15, UProjectUITheme::Muted), 3.f);
    Rewards = Tree.ConstructWidget<UHorizontalBox>();
    Stack(*Reward, Rewards, 13.f);
    RewardFrame = UProjectUITheme::Frame(Tree, Reward);
    UProjectUITheme::Pin(*Canvas, *RewardFrame, FVector2D(0.5, 0.17), FVector2D(0.5, 0), FVector2D::ZeroVector);

    auto* Tray = Tree.ConstructWidget<UVerticalBox>();
    auto* InventoryTitle = UProjectUITheme::Label(Tree, LOCTEXT("Items", "아이템"), 14, UProjectUITheme::Gold, true);
    InventoryTitle->SetJustification(ETextJustify::Center);
    Stack(*Tray, InventoryTitle, 0.f);
    Inventory = Tree.ConstructWidget<UScrollBox>();
    Inventory->SetScrollbarThickness(FVector2D(4.f, 4.f));
    Inventory->SetScrollbarPadding(FMargin(3.f, 0.f, 0.f, 0.f));
    auto* InventoryListSlot = Tray->AddChildToVerticalBox(Inventory);
    InventoryListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    InventoryListSlot->SetPadding(FMargin(0.f, UProjectUITheme::Gap, 0.f, 0.f));
    InventoryFrame = UProjectUITheme::Frame(Tree, Tray, FMargin(8.f));
    auto* InventoryFrameSlot = Canvas->AddChildToCanvas(InventoryFrame);
    InventoryFrameSlot->SetAnchors(FAnchors(0.f, 0.13f, 0.f, 0.83f));
    InventoryFrameSlot->SetOffsets(FMargin(14.f, 0.f, 88.f, 0.f));

    UnitActions = Tree.ConstructWidget<UVerticalBox>();
    UnitFrame = UProjectUITheme::Frame(Tree, Sized(Tree, UnitActions, 148.f, 0.f), FMargin(10.f));
    CastChecked<USizeBox>(UnitFrame->GetContent())->ClearHeightOverride();
    UProjectUITheme::Pin(*Canvas, *UnitFrame, FVector2D(1, 0.5), FVector2D(1, 0.5), FVector2D(-15, 0));

    ProfileText = UProjectUITheme::Label(Tree, {}, 13, UProjectUITheme::Text);
    ProfileText->SetWrapTextAt(236.f);
    ProfileFrame = UProjectUITheme::Frame(Tree, ProfileText, FMargin(11.f));
    ProfilePosition = UProjectUITheme::Pin(*Canvas, *ProfileFrame, FVector2D::ZeroVector, FVector2D(0, 1), FVector2D::ZeroVector);
    Show(ProfileFrame, false);

    Feedback = UProjectUITheme::Label(Tree, {}, 15, UProjectUITheme::Text);
    Feedback->SetJustification(ETextJustify::Center);
    Feedback->SetWrapTextAt(420.f);
    auto* FeedbackFrame = UProjectUITheme::Frame(Tree, Feedback, FMargin(12.f, 6.f));
    FeedbackFrame->SetVisibility(ESlateVisibility::Collapsed);
    UProjectUITheme::Pin(*Canvas, *FeedbackFrame, FVector2D(0.5, 0.64), FVector2D(0.5, 0.5), FVector2D::ZeroVector);

    for (UWidget* Frame : { ShopFrame.Get(), RewardFrame.Get(), InventoryFrame.Get(), UnitFrame.Get(), StatusFrame.Get() })
        Show(Frame, false);
    return Super::RebuildWidget();
}

void UProjectBattleScreen::MarkDirty()
{
    bDirty = true;
}

void UProjectBattleScreen::AttachTransactions(AShopTransactionManager* Manager)
{
    if (Transactions == Manager) return;
    if (IsValid(Transactions))
    {
        if (IsValid(Transactions->Shop)) Transactions->Shop->OnShopChanged.RemoveDynamic(this, &UProjectBattleScreen::MarkDirty);
        if (IsValid(Transactions->Roster)) Transactions->Roster->OnRosterChanged.RemoveDynamic(this, &UProjectBattleScreen::MarkDirty);
    }
    Transactions = Manager;
    if (IsValid(Transactions))
    {
        Transactions->Shop->OnShopChanged.AddUniqueDynamic(this, &UProjectBattleScreen::MarkDirty);
        Transactions->Roster->OnRosterChanged.AddUniqueDynamic(this, &UProjectBattleScreen::MarkDirty);
    }
    MarkDirty();
}

void UProjectBattleScreen::NativeDestruct()
{
    SetItemDragging(false);
    AttachTransactions(nullptr);
    if (GetWorld() && !GetWorld()->bIsTearingDown) ClosePauseMenu();
    if (PauseMenu) PauseMenu->RemoveFromParent();
    if (GameResultScreen) GameResultScreen->RemoveFromParent();
    Super::NativeDestruct();
}

void UProjectBattleScreen::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AAutoBattleGameMode>() : nullptr;
    if (!Mode) return;
    auto* Manager = Mode->MainMapShop.Get();
    AttachTransactions(IsValid(Manager) && Manager->IsConfigured() ? Manager : nullptr);

    const FShopMatchState Match = Mode->GetShopMatchState_Implementation();
    if (bItemDragging && !Match.CanManageBoard()) UWidgetBlueprintLibrary::CancelDragDrop();
    if (Match.Phase != DisplayedPhase)
    {
        Report(ETransactionResult::Success);
        DisplayedPhase = Match.Phase;
    }
    const FGuid Selection = FindSelectedUnit();
    if (Selection != RenderedUnit)
    {
        RenderedUnit = Selection;
        MarkDirty();
    }
    const bool bRefresh = bDirty && !bItemDragging;
    if (bRefresh)
    {
        bDirty = false;
        Refresh(Match);
    }

    const int32 Deployed = Mode->GetDeployedPlayerCount();
    Round->SetText(Mode->bEnableMainMapRoundTest
        ? FText::Format(LOCTEXT("Deployment", "배치 {0}/{1}"), Deployed, Match.DeploymentLimit)
        : FText::Format(LOCTEXT("Round", "라운드 {0}   ·   배치 {1}/{2}"), Match.Round, Deployed, Match.DeploymentLimit));
    const auto* PC = Cast<AAutoBattlePlayerController>(GetOwningPlayer());
    const auto* Placement = Transactions ? Cast<UMainMapShopPlacement>(Transactions->Roster->Placement.GetObject()) : nullptr;
    StartCombat->SetIsEnabled(Placement && PC && !bItemDragging && Placement->CanRequestCombatStart(*PC));
    Show(StartCombat->GetParent(), Match.Phase == EShopPhase::Preparation);
    Show(StatusFrame, Match.Phase != EShopPhase::Inactive && Match.Phase != EShopPhase::Finished);
    UpdateProfile(Geometry, bRefresh);
}

FProjectMenuActions UProjectBattleScreen::MenuActions(void (UProjectBattleScreen::*Restart)())
{
    return { FSimpleDelegate::CreateUObject(this, Restart),
        FSimpleDelegate::CreateUObject(this, &UProjectBattleScreen::ReturnToTitle),
        FSimpleDelegate::CreateUObject(this, &UProjectBattleScreen::QuitGame) };
}

void UProjectBattleScreen::ShowGameResult(EProjectGameResult Outcome, int32 RoundNumber, int32 ClearedRounds, int32 TotalRounds)
{
    auto* Player = GetOwningPlayer();
    if (!Player || !Theme) return;
    if (!GameResultScreen)
    {
        GameResultScreen = CreateWidget<UGameResultScreen>(Player);
        if (!GameResultScreen) return;
        GameResultScreen->Configure(*Theme, MenuActions(&UProjectBattleScreen::RestartFromResult));
    }
    if (!GameResultScreen->IsInViewport()) GameResultScreen->AddToViewport(110);
    GameResultScreen->Present(Outcome, RoundNumber, ClearedRounds, TotalRounds);
}

void UProjectBattleScreen::HideGameResult()
{
    Show(GameResultScreen, false);
}

void UProjectBattleScreen::TogglePauseMenu()
{
    if (bPausedByMenu)
    {
        ResumeGame();
        return;
    }

    auto* Player = Cast<AAutoBattlePlayerController>(GetOwningPlayer());
    if (!Player || !Theme || Player->IsPaused()) return;
    if (!PauseMenu)
    {
        PauseMenu = CreateWidget<UPauseMenu>(Player);
        if (!PauseMenu) return;
        PauseMenu->Configure(*Theme, MenuActions(&UProjectBattleScreen::RestartCurrentMatch),
            FSimpleDelegate::CreateUObject(this, &UProjectBattleScreen::ResumeGame));
        Show(PauseMenu, false);
    }
    if (!PauseMenu->IsInViewport()) PauseMenu->AddToViewport(120);
    if (!PauseMenu->IsInViewport() || !Player->SetPause(true)) return;

    bPausedByMenu = true;
    bMouseCursorBeforePause = Player->bShowMouseCursor;
    Player->CancelUnitDrag();
    if (bItemDragging) UWidgetBlueprintLibrary::CancelDragDrop();
    Show(PauseMenu, true);
    FInputModeUIOnly Input;
    Input.SetWidgetToFocus(PauseMenu->TakeWidget());
    Player->SetInputMode(Input);
    Player->bShowMouseCursor = true;
}

bool UProjectBattleScreen::ClosePauseMenu()
{
    if (!bPausedByMenu) return true;
    auto* Player = GetOwningPlayer();
    if (!Player || (Player->IsPaused() && !Player->SetPause(false))) return false;

    bPausedByMenu = false;
    Show(PauseMenu, false);
    FInputModeGameAndUI Input;
    Input.SetHideCursorDuringCapture(false);
    Player->SetInputMode(Input);
    UWidgetBlueprintLibrary::SetFocusToGameViewport();
    Player->bShowMouseCursor = bMouseCursorBeforePause;
    return true;
}

void UProjectBattleScreen::ResumeGame()
{
    ClosePauseMenu();
}

void UProjectBattleScreen::RestartFromResult()
{
    if (auto* Player = Cast<AAutoBattlePlayerController>(GetOwningPlayer())) Player->RequestRestart();
}

void UProjectBattleScreen::RestartCurrentMatch()
{
    if (ClosePauseMenu())
        UGameplayStatics::OpenLevel(this, FName(*UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName())));
}

void UProjectBattleScreen::ReturnToTitle()
{
    if (ClosePauseMenu()) UGameplayStatics::OpenLevel(this, TEXT("/Game/UITestMap/TitleMap"));
}

void UProjectBattleScreen::QuitGame()
{
    if (ClosePauseMenu()) UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UProjectBattleScreen::Refresh(const FShopMatchState& Match)
{
    const bool bReady = IsValid(Transactions) && Match.Phase != EShopPhase::Inactive && Match.Phase != EShopPhase::Finished;
    Show(ShopFrame, bReady && Match.Phase != EShopPhase::RewardSelection);
    Show(RewardFrame, bReady && Match.Phase == EShopPhase::RewardSelection);
    Show(InventoryFrame, bReady);
    Show(UnitFrame, bReady && RenderedUnit.IsValid());
    if (!bReady) return;
    RefreshShop(Match);
    RefreshRewards(Match);
    RefreshInventory(Match);
    RefreshUnit(Match);
}

void UProjectBattleScreen::RefreshShop(const FShopMatchState& Match)
{
    const auto& Shop = *Transactions->Shop;
    Gold->SetText(FText::Format(LOCTEXT("Gold", "{0} 골드"), Shop.GetGold()));
    Reroll->SetIsEnabled(Match.CanTrade() && Shop.GetGold() >= AShopManager::RerollCost);
    Lock->SetIsEnabled(Match.CanTrade());
    Lock->SetLabel(Shop.IsShopLocked() ? LOCTEXT("Locked", "잠금 해제") : LOCTEXT("Lock", "잠금"));
    Offers->ClearChildren();

    const int32 Revision = Shop.GetShopRevision();
    const auto Stock = Shop.GetOffers();
    for (int32 Index = 0; Index < Stock.Num(); ++Index)
    {
        const auto& Offer = Stock[Index];
        const bool bAvailable = !Offer.UnitId.IsNone();
        auto* Button = MakeButton({}, [this, Index, Revision]
        {
            if (IsValid(Transactions)) Report(Transactions->RequestBuy(Index, Revision));
        });
        Button->SetIsEnabled(bAvailable && Match.CanTrade() && Shop.GetGold() >= Offer.Cost);
        auto* Body = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Name = UProjectUITheme::Label(*WidgetTree, bAvailable ? UnitName(Shop, Offer.UnitId) : FText::GetEmpty(), 17,
            UProjectUITheme::Text, true);
        Name->SetJustification(ETextJustify::Center);
        Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Stack(*Body, Name, 0);
        if (bAvailable)
        {
            const int32 Cost = UnitCost(*Transactions, Offer.UnitId, Offer.Cost);
            auto* Price = UProjectUITheme::Label(*WidgetTree,
                FText::Format(LOCTEXT("OfferCost", "{0}코스트   ·   {1}G"), Cost, Offer.Cost), 12, UProjectUITheme::CostColor(Cost));
            Price->SetJustification(ETextJustify::Center);
            Stack(*Body, Price, 4.f);
        }
        Button->SetBody(Body);
        Row(*Offers, Sized(*WidgetTree, Button, 131.f, 78.f));
    }
}

void UProjectBattleScreen::RefreshRewards(const FShopMatchState& Match)
{
    Rewards->ClearChildren();
    if (Match.Phase != EShopPhase::RewardSelection) return;
    const auto& Shop = *Transactions->Shop;
    const int32 Revision = Shop.GetRewardRevision();
    if (DisplayedRewardRevision != Revision)
    {
        DisplayedRewardRevision = Revision;
        Report(ETransactionResult::Success);
    }
    RewardSummary->SetText(FText::Format(LOCTEXT("RewardHeading", "라운드 {0} 완료   ·   +{1}G"), Match.Round, Match.ClearGoldAwarded));
    const auto Candidates = Shop.GetRewards();
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        const auto& Reward = Candidates[Index];
        FText Label;
        FText Details;
        FItemDefinition Item;
        int32 Cost = 0;
        switch (Reward.Kind)
        {
            case ERewardKind::Gold:
                Label = FText::Format(LOCTEXT("GoldReward", "+{0} 골드"), Reward.GoldAmount);
                break;
            case ERewardKind::RandomItem:
                Label = LOCTEXT("RandomItem", "랜덤 아이템");
                break;
            case ERewardKind::Item:
                Transactions->Items->GetItem(Reward.DefinitionId, Item);
                Label = Item.Label.IsEmpty() ? FText::FromName(Reward.DefinitionId) : Item.Label;
                Details = Item.Description;
                break;
            case ERewardKind::RandomUnit:
                Cost = Reward.UnitCost;
                Label = FText::Format(LOCTEXT("RandomUnit", "랜덤 {0}코스트 기물"), Cost);
                break;
            case ERewardKind::Unit:
                Cost = UnitCost(*Transactions, Reward.DefinitionId, Shop.GetOfferDefinition(Reward.DefinitionId).Cost);
                Label = UnitName(Shop, Reward.DefinitionId);
                break;
        }

        auto* Button = MakeButton({}, [this, Index, Revision]
        {
            if (IsValid(Transactions)) Report(Transactions->RequestReward(Index, Revision));
        });
        Button->SetToolTipText(Details);
        auto* Body = WidgetTree->ConstructWidget<UVerticalBox>();
        if (Item.Icon)
        {
            auto* Image = WidgetTree->ConstructWidget<UImage>();
            Image->SetBrushFromTexture(Item.Icon);
            auto* IconSlot = Body->AddChildToVerticalBox(Sized(*WidgetTree, Image, 22, 22));
            IconSlot->SetHorizontalAlignment(HAlign_Center);
        }
        auto* Name = UProjectUITheme::Label(*WidgetTree, Label, 16,
            Cost > 0 ? UProjectUITheme::CostColor(Cost) : UProjectUITheme::Text, true);
        Name->SetAutoWrapText(true);
        Name->SetJustification(ETextJustify::Center);
        Stack(*Body, Name, 0);
        if (Cost > 0)
        {
            auto* Stars = UProjectUITheme::Label(*WidgetTree,
                FText::Format(LOCTEXT("RewardStars", "{0}성"), Reward.Stars), 12, UProjectUITheme::Gold);
            Stars->SetJustification(ETextJustify::Center);
            Stack(*Body, Stars, 3);
        }
        Button->SetBody(Body);
        Row(*Rewards, Sized(*WidgetTree, Button, 146.f, 82.f));
    }
}

void UProjectBattleScreen::RefreshInventory(const FShopMatchState& Match)
{
    const float ScrollOffset = Inventory->GetScrollOffset();
    Inventory->ClearChildren();
    const auto Items = Transactions->Items->GetInventory();
    if (Items.IsEmpty())
    {
        auto* Empty = UProjectUITheme::Label(*WidgetTree, LOCTEXT("EmptyItems", "비어 있음"), 12, UProjectUITheme::Muted);
        Empty->SetJustification(ETextJustify::Center);
        Empty->SetAutoWrapText(true);
        Inventory->AddChild(Empty);
        Inventory->ScrollToStart();
        return;
    }
    for (const FName ItemId : Items)
    {
        FItemDefinition Definition;
        Transactions->Items->GetItem(ItemId, Definition);
        auto* ItemSlot = CreateWidget<UInventorySlot>(GetOwningPlayer());
        ItemSlot->Configure(*this, ItemId, Definition);
        ItemSlot->SetIsEnabled(Match.CanManageBoard());

        auto* ItemSize = WidgetTree->ConstructWidget<USizeBox>();
        ItemSize->SetHeightOverride(54.f);
        ItemSize->SetContent(ItemSlot);
        auto* ScrollSlot = CastChecked<UScrollBoxSlot>(Inventory->AddChild(ItemSize));
        ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 5.f));
    }
    Inventory->SetScrollOffset(ScrollOffset);
}

void UProjectBattleScreen::RefreshUnit(const FShopMatchState& Match)
{
    UnitActions->ClearChildren();
    FRosterUnit Unit;
    const auto& Roster = *Transactions->Roster;
    if (!Roster.GetUnit(RenderedUnit, Unit)) return;

    auto* Name = UProjectUITheme::Label(*WidgetTree,
        FText::Format(LOCTEXT("Selected", "{0}  ·  {1}성"), UnitName(*Transactions->Shop, Unit.UnitId), Unit.Stars),
        15, UProjectUITheme::Gold, true);
    Name->SetAutoWrapText(true);
    Stack(*UnitActions, Name, 0.f);

    FRosterUnitProfile Profile;
    if (Roster.GetProfile(Unit.Id, Profile))
    {
        Stack(*UnitActions, UProjectUITheme::Label(*WidgetTree,
            FText::Format(LOCTEXT("Stats", "HP {0}   ·   공격 {1}"), FMath::RoundToInt(Profile.MaxHP), FMath::RoundToInt(Profile.AttackDamage)),
            12, UProjectUITheme::Muted));
    }
    auto* Sell = MakeButton(LOCTEXT("Sell", "기물 판매"), [this, Id = Unit.Id]
    {
        if (IsValid(Transactions) && Id == FindSelectedUnit()) Report(Transactions->RequestSell(Id));
    });
    Sell->SetIsEnabled(Match.CanManageBoard()
        || (Match.Phase == EShopPhase::Combat && Roster.GetPosition(Unit.Id).Location == EUnitLocation::Bench));
    Stack(*UnitActions, Sell);
    TArray<FName> Equipment;
    Roster.GetUnitEquipment(Unit.Id, Equipment);
    for (int32 EquipmentIndex = 0; EquipmentIndex < Equipment.Num(); ++EquipmentIndex)
    {
        FItemDefinition Item;
        Transactions->Items->GetItem(Equipment[EquipmentIndex], Item);
        auto* Unequip = MakeButton(FText::Format(LOCTEXT("Unequip", "{0} 해제"), Item.Label), [this, Id = Unit.Id, EquipmentIndex]
        {
            if (IsValid(Transactions) && Id == FindSelectedUnit()) Report(Transactions->RequestUnequip(Id, EquipmentIndex));
        });
        Unequip->SetIsEnabled(Match.CanManageBoard());
        Stack(*UnitActions, Unequip, 4.f);
    }
}

void UProjectBattleScreen::UpdateProfile(const FGeometry& Geometry, bool bRefresh)
{
    float X = 0.f;
    float Y = 0.f;
    const auto* PC = Cast<AAutoBattlePlayerController>(GetOwningPlayer());
    if (!IsValid(Transactions) || !InventoryFrame->IsVisible() || !PC || PC->IsUnitDragActive()
        || !PC->GetMousePosition(X, Y) || BlocksWorldInput(FVector2D(X, Y)))
    {
        ProfileUnit.Invalidate();
        Show(ProfileFrame, false);
        return;
    }

    const auto& Roster = *Transactions->Roster;
    const FGuid Hovered = Roster.GetHoveredUnit();
    FRosterUnit Unit;
    if (!Hovered.IsValid() || !Roster.GetUnit(Hovered, Unit))
    {
        ProfileUnit.Invalidate();
        Show(ProfileFrame, false);
        return;
    }

    if (bRefresh || ProfileUnit != Hovered)
    {
        ProfileUnit = Hovered;
        TArray<FText> Lines;
        Lines.Add(FText::Format(LOCTEXT("ProfileName", "{0}  ·  {1}성"), UnitName(*Transactions->Shop, Unit.UnitId), Unit.Stars));
        FRosterUnitProfile Stats;
        if (Roster.GetProfile(Unit.Id, Stats))
        {
            Lines.Add(FText::Format(LOCTEXT("ProfileAttack", "HP {0}  ·  AD {1}  ·  AP {2}"),
                FMath::RoundToInt(Stats.MaxHP), FMath::RoundToInt(Stats.AttackDamage), FMath::RoundToInt(Stats.AbilityPower)));
            Lines.Add(FText::Format(LOCTEXT("ProfileDefense", "방어력 {0}  ·  공속 {1}  ·  MP {2}"),
                FMath::RoundToInt(Stats.Armor), FText::AsNumber(Stats.AttackSpeed), FMath::RoundToInt(Stats.MaxMP)));
        }
        TArray<FName> Equipment;
        Roster.GetUnitEquipment(Unit.Id, Equipment);
        for (FName ItemId : Equipment)
        {
            FItemDefinition Item;
            if (Transactions->Items->GetItem(ItemId, Item))
                Lines.Add(FText::Format(LOCTEXT("ProfileItem", "{0}  ·  {1}"), Item.Label, Item.Description));
        }
        ProfileText->SetText(FText::Join(FText::FromString(TEXT("\n")), Lines));
    }

    FVector2D Absolute;
    USlateBlueprintLibrary::ScreenToWidgetAbsolute(this, FVector2D(X, Y), Absolute);
    const FVector2D Local = Geometry.AbsoluteToLocal(Absolute);
    const FVector2D Size = ProfileFrame->GetDesiredSize();
    const FVector2D ViewSize = Geometry.GetLocalSize();
    const double Right = FMath::Max(8.0, ViewSize.X - FMath::Max(258.0, Size.X) - 8.0);
    const double Top = FMath::Min(Size.Y + 8.0, ViewSize.Y);
    ProfilePosition->SetPosition(FVector2D(FMath::Clamp(Local.X + 14.0, 8.0, Right), FMath::Max(Top, Local.Y - 13.0)));
    ProfileFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FGuid UProjectBattleScreen::FindUnitAt(FVector2D ViewportPixel) const
{
    if (!IsValid(Transactions)) return {};
    FHitResult Hit;
    const APlayerController* PC = GetOwningPlayer();
    if (!PC || !PC->GetHitResultAtScreenPosition(ViewportPixel, ECC_Visibility, true, Hit)) return {};
    AActor* Actor = Hit.GetActor();
    if (const auto* Tile = Cast<ABoardTile>(Actor)) Actor = Tile->OccupiedUnit;
    else if (const auto* BenchTile = Cast<ABenchTile>(Actor)) Actor = BenchTile->OccupiedUnit;

    const auto* Unit = Cast<ABattleUnitBase>(Actor);
    const auto* Placement = Cast<UMainMapShopPlacement>(Transactions->Roster->Placement.GetObject());
    return IsValid(Unit) && IsValid(Placement) ? Placement->FindUnitId(Unit) : FGuid();
}

FGuid UProjectBattleScreen::FindSelectedUnit() const
{
    FRosterUnit Unit;
    return IsValid(Transactions) && Transactions->Roster->GetUnit(SelectedUnit, Unit) ? SelectedUnit : FGuid();
}

bool UProjectBattleScreen::IsInsidePanel(FVector2D Absolute) const
{
    for (const UWidget* Panel : { ShopFrame.Get(), RewardFrame.Get(), InventoryFrame.Get(), UnitFrame.Get(), StatusFrame.Get() })
        if (ContainsPoint(Panel, Absolute)) return true;
    return false;
}

bool UProjectBattleScreen::BlocksWorldInput(FVector2D ViewportPixel) const
{
    if (bPausedByMenu || (GameResultScreen && GameResultScreen->IsVisible())) return true;
    FVector2D Absolute = FVector2D::ZeroVector;
    USlateBlueprintLibrary::ScreenToWidgetAbsolute(this, ViewportPixel, Absolute);
    return bItemDragging || IsInsidePanel(Absolute);
}

void UProjectBattleScreen::OnWorldPointerPressed(FVector2D ViewportPixel, bool bCancel)
{
    if (bCancel && bItemDragging) UWidgetBlueprintLibrary::CancelDragDrop();
    if (!bCancel && BlocksWorldInput(ViewportPixel)) return;
    SelectedUnit = bCancel ? FGuid() : FindUnitAt(ViewportPixel);
}

void UProjectBattleScreen::SetItemDragging(bool bDragging)
{
    if (bItemDragging == bDragging) return;
    bItemDragging = bDragging;
    Show(DropSurface, bDragging);
    if (auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AAutoBattleGameMode>() : nullptr)
    {
        const auto* PC = Cast<AAutoBattlePlayerController>(GetOwningPlayer());
        Mode->SetManagedDragState(PC && PC->IsUnitDragActive(), bDragging);
    }
    if (!bDragging) MarkDirty();
}

bool UProjectBattleScreen::NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
    const auto* ItemDrag = Cast<UShopItemDrag>(Operation);
    if (!ItemDrag) return Super::NativeOnDrop(Geometry, Event, Operation);
    SetItemDragging(false);
    if (!IsValid(Transactions) || IsInsidePanel(Event.GetScreenSpacePosition())) return true;

    FVector2D Pixel;
    FVector2D WidgetPosition;
    USlateBlueprintLibrary::AbsoluteToViewport(this, Event.GetScreenSpacePosition(), Pixel, WidgetPosition);
    const FGuid Target = FindUnitAt(Pixel);
    if (!Target.IsValid())
    {
        Report(ETransactionResult::InvalidSelection);
        return true;
    }

    const auto ResultCode = Transactions->RequestEquip(Target, ItemDrag->ItemId);
    if (ResultCode == ETransactionResult::Success) SelectedUnit = Target;
    Report(ResultCode);
    return true;
}

void UProjectBattleScreen::EquipSelected(FName Item)
{
    if (IsValid(Transactions)) Report(Transactions->RequestEquip(FindSelectedUnit(), Item));
}

void UProjectBattleScreen::Report(ETransactionResult Outcome)
{
    if (!Feedback) return;
    FText Message;
    switch (Outcome)
    {
        case ETransactionResult::Success: break;
        case ETransactionResult::WrongPhase: Message = LOCTEXT("WrongPhase", "현재 단계에서는 사용할 수 없습니다."); break;
        case ETransactionResult::InvalidSelection: Message = LOCTEXT("InvalidSelection", "대상을 다시 선택하세요."); break;
        case ETransactionResult::InsufficientGold: Message = LOCTEXT("InsufficientGold", "골드가 부족합니다."); break;
        case ETransactionResult::NoSpace: Message = LOCTEXT("NoSpace", "빈 대기석 또는 장착 슬롯이 필요합니다."); break;
        case ETransactionResult::Unavailable: Message = LOCTEXT("Unavailable", "잠시 후 다시 시도하세요."); break;
        case ETransactionResult::ChangeRejected: Message = LOCTEXT("ChangeRejected", "현재 배치에서 변경할 수 없습니다."); break;
    }
    Feedback->SetText(Message);
    if (UWidget* Frame = Feedback->GetParent())
        Frame->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    MarkDirty();
}

void UInventorySlot::Configure(UProjectBattleScreen& InScreen, FName ItemId, const FItemDefinition& Definition)
{
    Screen = &InScreen;
    Item = ItemId;
    Icon = Definition.Icon;
    Name = Definition.Label.IsEmpty() ? FText::FromName(Item) : Definition.Label;
    SetToolTipText(FText::Format(LOCTEXT("ItemTooltip", "{0}\n{1}"), Name, Definition.Description));
}

UWidget* UInventorySlot::MakeIcon(UWidgetTree& Tree) const
{
    if (Icon)
    {
        auto* Image = Tree.ConstructWidget<UImage>();
        Image->SetBrushFromTexture(Icon);
        Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        return UProjectUITheme::Frame(Tree, Image, FMargin(5.f));
    }

    auto* Text = UProjectUITheme::Label(Tree, Name, 11, UProjectUITheme::Text, true);
    Text->SetAutoWrapText(true);
    Text->SetJustification(ETextJustify::Center);
    auto* Label = UProjectUITheme::Frame(Tree, Text, FMargin(3.f));
    Label->SetVerticalAlignment(VAlign_Center);
    return Label;
}

TSharedRef<SWidget> UInventorySlot::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget) WidgetTree->RootWidget = MakeIcon(*WidgetTree);
    return Super::RebuildWidget();
}

FReply UInventorySlot::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Screen.IsValid())
        return UWidgetBlueprintLibrary::DetectDragIfPressed(Event, this, EKeys::LeftMouseButton).NativeReply;
    return Super::NativeOnMouseButtonDown(Geometry, Event);
}

FReply UInventorySlot::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Screen.IsValid())
    {
        Screen->EquipSelected(Item);
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonUp(Geometry, Event);
}

void UInventorySlot::NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& Operation)
{
    if (!Screen.IsValid()) return;
    auto* Drag = NewObject<UShopItemDrag>(this);
    Drag->ItemId = Item;
    Drag->Pivot = EDragPivot::MouseDown;
    Drag->DefaultDragVisual = Sized(*WidgetTree, MakeIcon(*WidgetTree), 48.f, 48.f);
    Drag->DefaultDragVisual->SetVisibility(ESlateVisibility::HitTestInvisible);
    Screen->SetItemDragging(true);
    Operation = Drag;
}

void UInventorySlot::NativeOnDragCancelled(const FDragDropEvent& Event, UDragDropOperation* Operation)
{
    if (Screen.IsValid()) Screen->SetItemDragging(false);
    Super::NativeOnDragCancelled(Event, Operation);
}

#undef LOCTEXT_NAMESPACE
