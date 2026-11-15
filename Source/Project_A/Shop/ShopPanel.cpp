#include "ShopPanel.h"
#include "RosterManager.h"
#include "ShopManager.h"
#include "ShopTransactionManager.h"
#include "Engine/Canvas.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"

namespace
{
const FLinearColor PanelBackground(0.025f, 0.035f, 0.055f, 0.96f);
const FLinearColor ButtonBackground(0.10f, 0.17f, 0.25f, 1);
const FLinearColor SelectedBackground(0.13f, 0.30f, 0.34f, 1);
const FLinearColor Muted(0.62f, 0.72f, 0.78f, 1);
const FLinearColor GoldColor(1, 0.82f, 0.39f, 1);

FString UnitLabel(const AShopManager& Shop, FName Id)
{
    const FText Label = Shop.GetOfferDefinition(Id).Label;
    return Label.IsEmpty() ? Id.ToString() : Label.ToString();
}

FString ItemLabel(const AItemManager& Items, FName Id)
{
    FItemDefinition Item;
    return Items.GetItem(Id, Item) && !Item.Label.IsEmpty() ? Item.Label.ToString() : Id.ToString();
}

FString ResultText(ETransactionResult Result)
{
    switch (Result)
    {
        case ETransactionResult::Success: return {};
        case ETransactionResult::WrongPhase: return TEXT("This action is locked during this phase.");
        case ETransactionResult::InvalidSelection: return TEXT("That selection is no longer available.");
        case ETransactionResult::InsufficientGold: return TEXT("Not enough gold.");
        case ETransactionResult::NoSpace: return TEXT("No free slot.");
        case ETransactionResult::Unavailable: return TEXT("That action is unavailable.");
        case ETransactionResult::ChangeRejected: return TEXT("The board could not apply this change.");
    }
    return {};
}
}

bool UShopPanel::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE || Type == EWorldType::GamePreview;
}

void UShopPanel::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (InWorld.GetNetMode() != NM_DedicatedServer)
        DrawHandle = AHUD::OnHUDPostRender.AddUObject(this, &UShopPanel::Draw);
}
void UShopPanel::Deinitialize()
{
    ClearBindings();
    Super::Deinitialize();
}

void UShopPanel::ClearBindings()
{
    AHUD::OnHUDPostRender.Remove(DrawHandle);
    if (Viewport.IsValid()) Viewport->OnInputKey().Remove(InputHandle);
    DrawHandle.Reset();
    InputHandle.Reset();
    Viewport.Reset();
    ActiveHUD.Reset();
    Transactions.Reset();
    Buttons.Reset();
    Selected.Invalidate();
    Feedback.Reset();
    ItemPage = 0;
}

void UShopPanel::AddButton(AHUD& HUD, const FString& Label, const FButton& Button, bool bEnabled, bool bSelected)
{
    const FVector2D P = Button.Position;
    const FVector2D S = Button.Size;
    HUD.DrawRect(!bEnabled ? PanelBackground : bSelected ? SelectedBackground : ButtonBackground, P.X, P.Y, S.X, S.Y);
    FString Text = Label;
    float TextWidth = 0;
    float TextHeight = 0;
    HUD.GetTextSize(Text, TextWidth, TextHeight);
    if (TextWidth > S.X - 12)
    {
        do
        {
            Text.LeftChopInline(1);
            HUD.GetTextSize(Text + TEXT("..."), TextWidth, TextHeight);
        }
        while (!Text.IsEmpty() && TextWidth > S.X - 12);
        Text += TEXT("...");
    }
    HUD.DrawText(Text, bEnabled ? FLinearColor::White : Muted, P.X + 6, P.Y + (S.Y - TextHeight) / 2);
    const int32 Index = Buttons.Add(Button);
    HUD.AddHitBox(P, S, FName(TEXT("ShopPanel"), Index + 1), true, 10);
    Buttons[Index].bEnabled = bEnabled;
}

void UShopPanel::Draw(AHUD* HUD, UCanvas* Canvas)
{
    if (!HUD || !Canvas || HUD->GetWorld() != GetWorld()) return;
    AShopTransactionManager* Manager = Transactions.Get();
    if (!Manager || !Manager->IsConfigured() || Manager->Shop->GetPhase() == EShopPhase::Inactive)
    {
        Buttons.Reset();
        Transactions.Reset();
        for (TActorIterator<AShopTransactionManager> It(GetWorld()); It; ++It)
        {
            if (!It->IsConfigured() || It->Shop->GetPhase() == EShopPhase::Inactive) continue;
            Transactions = *It;
            break;
        }
        Manager = Transactions.Get();
    }
    if (!Manager) return;
    APlayerController* PC = HUD->GetOwningPlayerController();
    if (!PC || !PC->IsLocalController()) return;

    const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
    FSceneViewProjectionData Projection;
    UGameViewportClient* GameViewport = GetWorld()->GetGameViewport();
    if (!LocalPlayer || !GameViewport || !GameViewport->Viewport
        || !LocalPlayer->GetProjectionData(GameViewport->Viewport, Projection)) return;
    const FIntRect ViewRect = Projection.GetConstrainedViewRect();
    ViewOrigin = FVector2D(ViewRect.Min.X, ViewRect.Min.Y);
    ViewSize = FVector2D(ViewRect.Width(), ViewRect.Height());

    ActiveHUD = HUD;
    Buttons.Reset();
    if (!Viewport.IsValid())
    {
        Viewport = GetWorld()->GetGameViewport();
        if (Viewport.IsValid()) InputHandle = Viewport->OnInputKey().AddUObject(this, &UShopPanel::HandleInput);
    }
    if (ViewSize.X < 620 || ViewSize.Y < 410) return;

    FRosterUnit Unit;
    if (Selected.IsValid() && !Manager->Roster->GetUnit(Selected, Unit)) Selected.Invalidate();
    DrawShop(*HUD, *Manager->Shop);
    DrawItemsAndUnitActions(*HUD, *Manager);
    if (Manager->Shop->GetPhase() == EShopPhase::RewardSelection) DrawRewards(*HUD, *Manager);

    float X, Y;
    if (PC->GetMousePosition(X, Y))
    {
        const FVector2D LocalMouse = FVector2D(X, Y) - ViewOrigin;
        if (IsOverButton(FVector2D(X, Y)) || HUD->GetHitBoxAtCoordinates(FVector2D(X, Y), true)) return;
        const FGuid Hovered = Manager->Roster->GetHoveredUnit();
        if (Hovered.IsValid()) DrawProfile(*HUD, *Manager, Hovered, LocalMouse + FVector2D(12, -120));
    }
}

void UShopPanel::DrawShop(AHUD& HUD, const AShopManager& Manager)
{
    const float Width = FMath::Min(700.0f, static_cast<float>(ViewSize.X) - 32.0f);
    const float X = (static_cast<float>(ViewSize.X) - Width) / 2;
    const float Y = static_cast<float>(ViewSize.Y) - 160;
    HUD.DrawRect(PanelBackground, X, Y, Width, 94);
    HUD.DrawText(FString::Printf(TEXT("R%d   |   %d GOLD   |   LIMIT %d"), Manager.GetRound(), Manager.GetGold(), Manager.GetDeploymentLimit()),
        GoldColor, X + 12, Y + 12);
    if (!Feedback.IsEmpty()) HUD.DrawText(Feedback, Muted, X + 12, Y - 22);

    const bool bTrade = Manager.GetPhase() == EShopPhase::Preparation || Manager.GetPhase() == EShopPhase::Combat;
    AddButton(HUD, TEXT("Reroll  2G"), { EAction::Reroll, { X + Width - 206, Y + 7 }, { 94, 27 } }, bTrade);
    AddButton(HUD, Manager.IsShopLocked() ? TEXT("Unlock") : TEXT("Lock"),
        { EAction::Lock, { X + Width - 104, Y + 7 }, { 94, 27 } }, bTrade, Manager.IsShopLocked());

    const TArray<FShopOffer> Offers = Manager.GetOffers();
    const float SlotWidth = (Width - 28) / AShopManager::ShopSlots;
    for (int32 Index = 0; Index < Offers.Num(); ++Index)
    {
        const FShopOffer& Offer = Offers[Index];
        const FString Label = Offer.UnitId.IsNone() ? TEXT("-")
            : FString::Printf(TEXT("%s  %dG"), *UnitLabel(Manager, Offer.UnitId), Offer.Cost);
        FButton Button { EAction::Buy, { X + 10 + Index * SlotWidth, Y + 42 }, { SlotWidth - 4, 42 }, Index };
        Button.Revision = Manager.GetShopRevision();
        AddButton(HUD, Label, Button, bTrade && !Offer.UnitId.IsNone());
    }
}

void UShopPanel::DrawItemsAndUnitActions(AHUD& HUD, const AShopTransactionManager& Manager)
{
    const ARosterManager& Roster = *Manager.Roster;
    const AItemManager& Items = *Manager.Items;
    const EShopPhase Phase = Manager.Shop->GetPhase();
    const bool bManageBoard = Phase == EShopPhase::Preparation || Phase == EShopPhase::RewardSelection;
    const bool bManageBench = bManageBoard || Phase == EShopPhase::Combat;
    const float Y = static_cast<float>(ViewSize.Y) - 160;

    HUD.DrawText(TEXT("ITEMS"), GoldColor, 14, 86);
    const TArray<FName> Inventory = Items.GetInventory();
    const int32 ItemRows = FMath::Max(1, FMath::FloorToInt((Y - 142) / 27));
    ItemPage = FMath::Clamp(ItemPage, 0, FMath::Max(0, (Inventory.Num() - 1) / ItemRows));
    const bool bSelectedBoardUnit = Selected.IsValid() && Roster.GetPosition(Selected).Location == EUnitLocation::Board;
    for (int32 Row = 0; Row < ItemRows && Inventory.IsValidIndex(ItemPage * ItemRows + Row); ++Row)
    {
        const FName Item = Inventory[ItemPage * ItemRows + Row];
        FButton Button { EAction::Equip, { 14, 109.0 + Row * 27 }, { 146, 24 }, 0, {}, Item };
        AddButton(HUD, ItemLabel(Items, Item), Button, bManageBoard && bSelectedBoardUnit);
    }
    if (Inventory.Num() > ItemRows)
    {
        AddButton(HUD, TEXT("<"), { EAction::InventoryPage, { 14, Y - 35 }, { 32, 26 }, -1 }, ItemPage > 0);
        AddButton(HUD, TEXT(">"), { EAction::InventoryPage, { 50, Y - 35 }, { 32, 26 }, 1 },
            (ItemPage + 1) * ItemRows < Inventory.Num());
    }

    const float X = static_cast<float>(ViewSize.X) - 160;
    FRosterUnit Unit;
    if (!Roster.GetUnit(Selected, Unit)) return;
    HUD.DrawText(FString::Printf(TEXT("%s %d*"), *UnitLabel(*Manager.Shop, Unit.UnitId), Unit.Stars), GoldColor, X, 86);
    const float ActionsY = 109;
    AddButton(HUD, TEXT("Sell"), { EAction::Sell, { X, ActionsY }, { 70, 26 }, 0, Selected },
        bManageBoard || (bManageBench && !bSelectedBoardUnit));
    const TArray<FName> Equipment = Items.GetEquipment(Unit.Id);
    for (int32 Slot = 0; Slot < Equipment.Num(); ++Slot)
    {
        AddButton(HUD, ItemLabel(Items, Equipment[Slot]) + TEXT(" x"),
            { EAction::Unequip, { X, ActionsY + 30 * (Slot + 1) }, { 146, 26 }, Slot, Selected }, bManageBoard);
    }
}

void UShopPanel::DrawRewards(AHUD& HUD, const AShopTransactionManager& Manager)
{
    const TArray<FRewardOffer> Rewards = Manager.Shop->GetRewards();
    if (Rewards.IsEmpty()) return;
    const float Width = FMath::Min(620.0f, static_cast<float>(ViewSize.X) - 92.0f);
    const float X = (static_cast<float>(ViewSize.X) - Width) / 2;
    const float Y = static_cast<float>(ViewSize.Y) * 0.22f;
    const float SlotWidth = (Width - 24) / AShopManager::RewardSlots;
    HUD.DrawRect(PanelBackground, X, Y, Width, 121);
    HUD.DrawText(TEXT("CHOOSE ONE REWARD"), GoldColor, X + 10, Y + 10);
    for (int32 Index = 0; Index < Rewards.Num(); ++Index)
    {
        const FRewardOffer& Reward = Rewards[Index];
        const bool bItem = Reward.Kind == ERewardKind::Item;
        const FString Label = bItem ? ItemLabel(*Manager.Items, Reward.DefinitionId)
            : FString::Printf(TEXT("%s %d*"), *UnitLabel(*Manager.Shop, Reward.DefinitionId), Reward.Stars);
        const float CardX = X + 10 + SlotWidth * Index;
        FButton Button { EAction::Reward, { CardX, Y + 36 }, { SlotWidth - 4, 42 }, Index };
        Button.Revision = Manager.Shop->GetRewardRevision();
        AddButton(HUD, Label, Button);
        FItemDefinition Item;
        const FString Detail = bItem && Manager.Items->GetItem(Reward.DefinitionId, Item)
            ? Item.Description.ToString() : TEXT("Unit");
        HUD.DrawText(Detail.Left(24), Muted, CardX + 4, Y + 86, nullptr, 0.8f);
    }
}

void UShopPanel::DrawProfile(AHUD& HUD, const AShopTransactionManager& Manager, FGuid Id, FVector2D Position)
{
    FRosterUnit Unit;
    if (!Manager.Roster->GetUnit(Id, Unit)) return;
    FRosterUnitProfile Profile;
    const bool bHasStats = Manager.Roster->GetProfile(Id, Profile);
    const float X = FMath::Clamp(Position.X, 8.0, ViewSize.X - 248.0);
    const float Y = FMath::Max(8.0, Position.Y);
    HUD.DrawRect(PanelBackground, X, Y, 240, 100);
    HUD.DrawText(FString::Printf(TEXT("%s  %d*"), *UnitLabel(*Manager.Shop, Unit.UnitId), Unit.Stars),
        GoldColor, X + 8, Y + 6);
    if (bHasStats)
    {
        HUD.DrawText(FString::Printf(TEXT("HP %.0f   AD %.0f   AP %.0f"), Profile.MaxHP, Profile.AttackDamage, Profile.AbilityPower),
            FLinearColor::White, X + 8, Y + 26);
        HUD.DrawText(FString::Printf(TEXT("Armor %.0f   AS %.2f   MP %.0f"), Profile.Armor, Profile.AttackSpeed, Profile.MaxMP),
            FLinearColor::White, X + 8, Y + 44);
    }
    TArray<FString> Items;
    const TArray<FName> Equipment = Manager.Items->GetEquipment(Id);
    for (FName Item : Equipment)
    {
        FItemDefinition Definition;
        Manager.Items->GetItem(Item, Definition);
        Items.Add(ItemLabel(*Manager.Items, Item));
        if (Equipment.Num() == 1 && !Definition.Description.IsEmpty()) Items.Add(Definition.Description.ToString());
    }
    HUD.DrawText(FString::Join(Items, TEXT(" / ")).Left(40), Muted, X + 8, Y + 65, nullptr, 0.85f);
}

bool UShopPanel::IsOverButton(FVector2D ViewportPosition) const
{
    const FVector2D LocalPosition = ViewportPosition - ViewOrigin;
    return Buttons.ContainsByPredicate([LocalPosition](const FButton& Button)
    {
        const FVector2D Offset = LocalPosition - Button.Position;
        return Offset.X >= 0 && Offset.Y >= 0 && Offset.X < Button.Size.X && Offset.Y < Button.Size.Y;
    });
}

void UShopPanel::HandleInput(const FInputKeyEventArgs& Event)
{
    if (Event.Event != IE_Pressed && Event.Event != IE_DoubleClick) return;

    AShopTransactionManager* Manager = Transactions.Get();
    AHUD* HUD = ActiveHUD.Get();
    if (!Manager || !Manager->IsConfigured() || !HUD) return;
    if (Event.Key == EKeys::RightMouseButton)
    {
        Selected.Invalidate();
        return;
    }
    if (Event.Key != EKeys::LeftMouseButton) return;

    float X, Y;
    APlayerController* PC = HUD->GetOwningPlayerController();
    if (!PC || !PC->GetMousePosition(X, Y)) return;
    for (int32 Index = Buttons.Num() - 1; Index >= 0; --Index)
    {
        const FButton Button = Buttons[Index];

        const FVector2D Offset = FVector2D(X, Y) - ViewOrigin - Button.Position;
        if (Offset.X < 0 || Offset.Y < 0 || Offset.X >= Button.Size.X || Offset.Y >= Button.Size.Y) continue;
        if (Button.bEnabled && (!Button.Unit.IsValid() || Button.Unit == Selected))
            RunAction(Button, *Manager);
        return;
    }

    if (IsOverButton(FVector2D(X, Y)) || HUD->GetHitBoxAtCoordinates(FVector2D(X, Y), true)) return;
    const ARosterManager& Roster = *Manager->Roster;
    const FGuid Hovered = Roster.GetHoveredUnit();
    FRosterUnit Unit;
    Selected = Roster.GetUnit(Hovered, Unit) ? Hovered : FGuid();
}

void UShopPanel::RunAction(const FButton& Button, AShopTransactionManager& Manager)
{
    ETransactionResult Result = ETransactionResult::Success;
    switch (Button.Action)
    {
        case EAction::Buy: Result = Manager.RequestBuy(Button.Index, Button.Revision); break;
        case EAction::Reroll: Result = Manager.Shop->RequestReroll(); break;
        case EAction::Lock: Result = Manager.Shop->RequestShopLock(!Manager.Shop->IsShopLocked()); break;
        case EAction::Equip: Result = Manager.RequestEquip(Selected, Button.Item); break;
        case EAction::Unequip: Result = Manager.RequestUnequip(Button.Unit, Button.Index); break;
        case EAction::Sell: Result = Manager.RequestSell(Button.Unit); break;
        case EAction::Reward: Result = Manager.RequestReward(Button.Index, Button.Revision); break;
        case EAction::InventoryPage: ItemPage += Button.Index; break;
    }
    Feedback = Button.Action == EAction::Reward && Result == ETransactionResult::Success
        ? TEXT("Reward received.") : ResultText(Result);
}
