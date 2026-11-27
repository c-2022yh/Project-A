#include "ShopPanel.h"
#include "RosterManager.h"
#include "../UI/ProjectBattleScreen.h"
#include "UObject/UnrealType.h"
#include "ShopManager.h"
#include "ItemManager.h"
#include "ShopTransactionManager.h"
#include "Engine/Canvas.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "../AutoBattleGameMode.h"
#include "../BattleUnitBase.h"
#include "../BoardTile.h"
#include "Engine/Texture2D.h"

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
    bUseWidgetScreen = UWorld::RemovePIEPrefix(InWorld.GetPackage()->GetName()) == TEXT("/Game/UITestMap/MainMap_Intergration1");
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
    CancelItemDrag();
    if (WidgetScreen) WidgetScreen->RemoveFromParent();
    WidgetScreen = nullptr;
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
    DisplayedRewardRevision = INDEX_NONE;
    ItemPage = 0;
}

void UShopPanel::CancelItemDrag()
{
    if (DraggingItem.IsNone()) return;
    DraggingItem = NAME_None;
    if (GetWorld())
        if (auto* Mode = Cast<AAutoBattleGameMode>(GetWorld()->GetAuthGameMode()))
            Mode->SetManagedDragState(false, false);
}

FVector2D UShopPanel::ToCanvasUnits(FVector2D ViewportPosition) const
{
    return (ViewportPosition - ViewOrigin) / CanvasScale;
}

void UShopPanel::DrawRect(AHUD& HUD, FLinearColor Color, float X, float Y, float Width, float Height) const
{
    HUD.DrawRect(Color, X * CanvasScale, Y * CanvasScale, Width * CanvasScale, Height * CanvasScale);
}

void UShopPanel::DrawText(AHUD& HUD, const FString& Text, FLinearColor Color, float X, float Y, float Scale) const
{
    HUD.DrawText(Text, Color, X * CanvasScale, Y * CanvasScale, nullptr, Scale * CanvasScale);
}

void UShopPanel::DrawItemIcon(AHUD& HUD, const FItemDefinition& Item, FVector2D Position, float Size)
{
    DrawRect(HUD, FLinearColor(0.65f, 0.48f, 0.18f), Position.X, Position.Y, Size, Size);
    if (Item.Icon)
        HUD.DrawTexture(Item.Icon, Position.X + 1, Position.Y + 1, Size - 2, Size - 2, 0, 0, 1, 1,
            FLinearColor::White, BLEND_Translucent, CanvasScale, true);
    else
    {
        // 아이콘 에셋을 지정하기 전에도 종류를 식별할 수 있는 임시 타일을 표시한다.
        DrawRect(HUD, ButtonBackground, Position.X + 1, Position.Y + 1, Size - 2, Size - 2);
        DrawText(HUD, Item.Label.ToString().Left(2), GoldColor, Position.X + 3, Position.Y + Size * 0.25f, Size / 32.f);
    }
}

void UShopPanel::DrawItemTooltip(AHUD& HUD, const FItemDefinition& Item, FVector2D Position)
{
    const float X = FMath::Clamp(static_cast<float>(Position.X), 0.f, FMath::Max(0.f, static_cast<float>(ViewSize.X) - 250.f));
    const float Y = FMath::Clamp(static_cast<float>(Position.Y), 0.f, FMath::Max(0.f, static_cast<float>(ViewSize.Y) - 72.f));
    DrawRect(HUD, PanelBackground, X, Y, 250, 72);
    DrawText(HUD, Item.Label.ToString(), GoldColor, X + 8, Y + 8);
    DrawText(HUD, Item.Description.ToString(), FLinearColor::White, X + 8, Y + 32, 0.8f);
}

void UShopPanel::AddButton(AHUD& HUD, const FString& Label, const FButton& Button, bool bEnabled, bool bSelected)
{
    const FVector2D P = Button.Position;
    const FVector2D S = Button.Size;
    DrawRect(HUD, !bEnabled ? PanelBackground : bSelected ? SelectedBackground : ButtonBackground, P.X, P.Y, S.X, S.Y);
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
    DrawText(HUD, Text, bEnabled ? FLinearColor::White : Muted, P.X + 6, P.Y + (S.Y - TextHeight) / 2);
    const int32 Index = Buttons.Add(Button);
    HUD.AddHitBox(P * CanvasScale, S * CanvasScale, FName(TEXT("ShopPanel"), Index + 1), true, 10);
    Buttons[Index].bEnabled = bEnabled;
}

void UShopPanel::Draw(AHUD* HUD, UCanvas* Canvas)
{
    if (!HUD || !Canvas || HUD->GetWorld() != GetWorld()) return;
    if (bUseWidgetScreen)
    {
        DrawWidgetScreen(*HUD);
        return;
    }
    AShopTransactionManager* Manager = Transactions.Get();
    if (!Manager || !Manager->IsConfigured() || Manager->GetMatchState().Phase == EShopPhase::Inactive)
    {
        Buttons.Reset();
        Transactions.Reset();
        for (TActorIterator<AShopTransactionManager> It(GetWorld()); It; ++It)
        {
            if (!It->IsConfigured() || It->GetMatchState().Phase == EShopPhase::Inactive) continue;
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
    CanvasScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(GameViewport->Viewport->GetSizeXY());
    ViewOrigin = FVector2D(ViewRect.Min.X, ViewRect.Min.Y);
    ViewSize = FVector2D(ViewRect.Width(), ViewRect.Height()) / CanvasScale;
    // 카메라 종횡비로 생긴 검은 여백까지 포함한 플레이어 화면 하단.
    // 그리기/버튼 좌표는 기존 카메라 영역 기준을 유지하므로 원점만 보정한다.
    ShopBottom = static_cast<float>(Projection.GetViewRect().Max.Y - ViewRect.Min.Y) / CanvasScale;

    ActiveHUD = HUD;
    Buttons.Reset();
    if (!Viewport.IsValid())
    {
        Viewport = GetWorld()->GetGameViewport();
        if (Viewport.IsValid()) InputHandle = Viewport->OnInputKey().AddUObject(this, &UShopPanel::HandleInput);
    }
    if (ViewSize.X < 320 || ViewSize.Y < 180) return;

    FRosterUnit Unit;
    if (Selected.IsValid() && !Manager->Roster->GetUnit(Selected, Unit)) Selected.Invalidate();
    const FShopMatchState Match = Manager->GetMatchState();
    if (!Match.CanManageBoard()) CancelItemDrag();
    if (!DraggingItem.IsNone() && !PC->IsInputKeyDown(EKeys::LeftMouseButton)) CancelItemDrag();
    if (Match.Phase == EShopPhase::Finished) return;
    if (Match.Phase != EShopPhase::RewardSelection) DrawShop(*HUD, *Manager->Shop, Match);
    DrawItemsAndUnitActions(*HUD, *Manager);
    if (Match.Phase == EShopPhase::RewardSelection) DrawRewards(*HUD, *Manager);

    float X, Y;
    if (PC->GetMousePosition(X, Y))
    {
        const FVector2D LocalMouse = ToCanvasUnits(FVector2D(X, Y));
        FItemDefinition Item;
        if (!DraggingItem.IsNone() && Manager->Items->GetItem(DraggingItem, Item))
        {
            DrawItemIcon(*HUD, Item, LocalMouse + FVector2D(12, 12), 32.f);
            return;
        }
        for (const FButton& Button : Buttons)
        {
            if (Button.Action == EAction::Equip && Button.Contains(LocalMouse) && Manager->Items->GetItem(Button.Item, Item))
            {
                DrawItemTooltip(*HUD, Item, LocalMouse + FVector2D(12, 12));
                return;
            }
        }
        if (IsOverButton(FVector2D(X, Y)) || HUD->GetHitBoxAtCoordinates(FVector2D(X, Y), true)) return;
        const FGuid Hovered = Manager->Roster->GetHoveredUnit();
        if (Hovered.IsValid()) DrawProfile(*HUD, *Manager, Hovered, LocalMouse + FVector2D(12, -120));
    }
}

void UShopPanel::DrawWidgetScreen(AHUD& HUD)
{
    APlayerController* Player = HUD.GetOwningPlayerController();
    if (!Player || !Player->IsLocalController()) return;
    if (!WidgetScreen)
    {
        WidgetScreen = CreateWidget<UProjectBattleScreen>(Player);
        if (!WidgetScreen || !WidgetScreen->AddToPlayerScreen(20))
        {
            WidgetScreen = nullptr;
            return;
        }
        Viewport = GetWorld()->GetGameViewport();
        if (Viewport.IsValid()) InputHandle = Viewport->OnInputKey().AddUObject(this, &UShopPanel::HandleInput);
        ActiveHUD = &HUD;
    }

    // The shared GameMode Blueprint still creates its battle widget on phase changes.
    // Hide that instance only in the UI test world.
    if (auto* Mode = GetWorld()->GetAuthGameMode<AAutoBattleGameMode>())
    {
        const auto* Property = FindFProperty<FObjectPropertyBase>(Mode->GetClass(), TEXT("BattleHUD"));
        auto* Legacy = Property ? Cast<UUserWidget>(Property->GetObjectPropertyValue_InContainer(Mode)) : nullptr;
        if (Legacy && Legacy->GetVisibility() != ESlateVisibility::Collapsed)
            Legacy->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UShopPanel::DrawShop(AHUD& HUD, const AShopManager& Manager, const FShopMatchState& Match)
{
    const bool bCompact = ViewSize.X < 620 || ViewSize.Y < 410;
    const float Width = FMath::Min(700.0f, static_cast<float>(ViewSize.X) - 32.0f);
    const float X = (static_cast<float>(ViewSize.X) - Width) / 2;
    // 패널 높이만큼 올려 그려 하단 여백 없이 배치한다. 버튼도 같은 Y를 사용한다.
    const float Height = bCompact ? 71.f : 94.f;
    const float Y = ShopBottom - Height;
    DrawRect(HUD, PanelBackground, X, Y, Width, Height);
    const FString ShopTitle = bCompact
        ? FString::Printf(TEXT("R%d | %dG | MAX %d"), Match.Round, Manager.GetGold(), Match.DeploymentLimit)
        : FString::Printf(TEXT("R%d   |   %d GOLD   |   LIMIT %d"), Match.Round, Manager.GetGold(), Match.DeploymentLimit);
    DrawText(HUD, ShopTitle, GoldColor, X + 12, Y + 12);
    if (!Feedback.IsEmpty()) DrawText(HUD, Feedback, Muted, X + 12, Y - 22);

    const bool bTrade = Match.CanTrade();
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
        FButton Button { EAction::Buy, { X + 10 + Index * SlotWidth, Y + (bCompact ? 36.f : 42.f) },
            { SlotWidth - 4, bCompact ? 28.f : 42.f }, Index };
        Button.Revision = Manager.GetShopRevision();
        AddButton(HUD, Label, Button, bTrade && !Offer.UnitId.IsNone());
    }
}

void UShopPanel::DrawItemsAndUnitActions(AHUD& HUD, const AShopTransactionManager& Manager)
{
    const ARosterManager& Roster = *Manager.Roster;
    const AItemManager& Items = *Manager.Items;
    const EShopPhase Phase = Manager.GetMatchState().Phase;
    const bool bManageBoard = Phase == EShopPhase::Preparation || Phase == EShopPhase::RewardSelection;
    const bool bManageBench = bManageBoard || Phase == EShopPhase::Combat;
    const bool bCompact = ViewSize.X < 620 || ViewSize.Y < 410;
    const bool bReward = Phase == EShopPhase::RewardSelection;
    const float Y = bReward ? static_cast<float>(ViewSize.Y) - 8.f
        : ShopBottom - (bCompact ? 71.f : 94.f);
    const float HeaderY = bReward ? FMath::Max(100.f, static_cast<float>(ViewSize.Y) - 110.f) : 86.f;
    const float ActionY = HeaderY + 23.f;

    DrawText(HUD, TEXT("ITEMS"), GoldColor, 14, HeaderY);
    const TArray<FName> Inventory = Items.GetInventory();
    const int32 ItemRows = FMath::Max(1, FMath::FloorToInt((Y - ActionY - 33.f) / 40.f));
    const int32 PageSize = ItemRows * 2;
    ItemPage = FMath::Clamp(ItemPage, 0, FMath::Max(0, (Inventory.Num() - 1) / PageSize));
    const bool bSelectedBoardUnit = Selected.IsValid() && Roster.GetPosition(Selected).Location == EUnitLocation::Board;
    for (int32 Slot = 0; Slot < PageSize && Inventory.IsValidIndex(ItemPage * PageSize + Slot); ++Slot)
    {
        const FName Item = Inventory[ItemPage * PageSize + Slot];
        const FVector2D Position(14 + (Slot % 2) * 40.f, ActionY + (Slot / 2) * 40.f);
        FButton Button { EAction::Equip, Position, { 32, 32 }, 0, {}, Item };
        AddButton(HUD, TEXT(""), Button, bManageBoard);
        FItemDefinition Definition;
        if (Items.GetItem(Item, Definition)) DrawItemIcon(HUD, Definition, Position, 32.f);
    }
    if (Inventory.Num() > PageSize)
    {
        AddButton(HUD, TEXT("<"), { EAction::InventoryPage, { 14, Y - 35 }, { 32, 26 }, -1 }, ItemPage > 0);
        AddButton(HUD, TEXT(">"), { EAction::InventoryPage, { 50, Y - 35 }, { 32, 26 }, 1 },
            (ItemPage + 1) * PageSize < Inventory.Num());
    }

    const float X = static_cast<float>(ViewSize.X) - 160;
    FRosterUnit Unit;
    if (!Roster.GetUnit(Selected, Unit)) return;
    DrawText(HUD, FString::Printf(TEXT("%s %d*"), *UnitLabel(*Manager.Shop, Unit.UnitId), Unit.Stars), GoldColor, X, HeaderY);
    AddButton(HUD, TEXT("Sell"), { EAction::Sell, { X, ActionY }, { 70, 26 }, 0, Selected },
        bManageBoard || (bManageBench && !bSelectedBoardUnit));
    TArray<FName> Equipment;
    Roster.GetUnitEquipment(Unit.Id, Equipment);
    for (int32 Slot = 0; Slot < Equipment.Num(); ++Slot)
    {
        AddButton(HUD, ItemLabel(Items, Equipment[Slot]) + TEXT(" x"),
            { EAction::Unequip, { X, ActionY + 30 * (Slot + 1) }, { 146, 26 }, Slot, Selected }, bManageBoard);
    }
}

void UShopPanel::DrawRewards(AHUD& HUD, const AShopTransactionManager& Manager)
{
    const TArray<FRewardOffer> Rewards = Manager.Shop->GetRewards();
    if (Rewards.IsEmpty()) return;
    if (DisplayedRewardRevision != Manager.Shop->GetRewardRevision())
    {
        // 새 라운드의 보상에는 이전 거래 결과 문구를 넘기지 않는다.
        DisplayedRewardRevision = Manager.Shop->GetRewardRevision();
        Feedback.Reset();
    }
    TArray<FString> FeedbackLines;
    Feedback.ParseIntoArrayLines(FeedbackLines);
    const float FeedbackHeight = FeedbackLines.Num() * 16.f;
    const bool bCompact = ViewSize.X < 620 || ViewSize.Y < 410;
    const float Width = FMath::Min(620.0f, static_cast<float>(ViewSize.X) - (bCompact ? 24.f : 92.f));
    const float X = (static_cast<float>(ViewSize.X) - Width) / 2;
    const float Y = bCompact ? 12.f : static_cast<float>(ViewSize.Y) * 0.22f;
    const float SlotWidth = (Width - 24) / AShopManager::RewardSlots;
    DrawRect(HUD, PanelBackground, X, Y, Width, (bCompact ? 91.f : 121.f) + FeedbackHeight);
    const FShopMatchState Match = Manager.GetMatchState();
    const TCHAR* Outcome = Match.bRoundCleared ? TEXT("CLEAR") : TEXT("COMPLETE");
    DrawText(HUD, FString::Printf(TEXT("R%d %s +%dG | CHOOSE ONE REWARD"),
        Match.Round, Outcome, Match.ClearGoldAwarded), GoldColor, X + 10, Y + 10);
    for (int32 Line = 0; Line < FeedbackLines.Num(); ++Line)
        DrawText(HUD, FeedbackLines[Line], FLinearColor(1.f, 0.55f, 0.4f),
            X + 10, Y + 31.f + Line * 16.f, 0.8f);
    for (int32 Index = 0; Index < Rewards.Num(); ++Index)
    {
        const FRewardOffer& Reward = Rewards[Index];
        const bool bItem = Reward.Kind == ERewardKind::Item;
        const bool bRandomUnit = Reward.Kind == ERewardKind::RandomUnit;
        const FString Label = Reward.Kind == ERewardKind::Gold ? FString::Printf(TEXT("+%d G"), Reward.GoldAmount)
            : Reward.Kind == ERewardKind::RandomItem ? TEXT("랜덤 아이템")
            : bItem ? ItemLabel(*Manager.Items, Reward.DefinitionId)
            : bRandomUnit ? FString::Printf(TEXT("랜덤 %d코스트 %d성 유닛"), Reward.UnitCost, Reward.Stars)
            : FString::Printf(TEXT("%s %d*"), *UnitLabel(*Manager.Shop, Reward.DefinitionId), Reward.Stars);
        const float CardX = X + 10 + SlotWidth * Index;
        FButton Button { EAction::Reward, { CardX, Y + 36 + FeedbackHeight }, { SlotWidth - 4, bCompact ? 30.f : 42.f }, Index };
        Button.Revision = Manager.Shop->GetRewardRevision();
        FItemDefinition Item;
        const bool HasItem = bItem && Manager.Items->GetItem(Reward.DefinitionId, Item);
        AddButton(HUD, HasItem || bRandomUnit ? TEXT("") : Label, Button);
        if (bRandomUnit)
        {
            // 성급/코스트/유닛 문구가 카드 폭 때문에 잘리지 않게 글자 크기를 맞춘다.
            float TextWidth = 0.f, TextHeight = 0.f;
            HUD.GetTextSize(Label, TextWidth, TextHeight);
            const float Scale = FMath::Min(1.f, static_cast<float>(Button.Size.X - 12.f) / FMath::Max(1.f, TextWidth));
            DrawText(HUD, Label, FLinearColor::White, Button.Position.X + 6.f,
                Button.Position.Y + (Button.Size.Y - TextHeight * Scale) / 2.f, Scale);
        }
        if (HasItem)
        {
            DrawItemIcon(HUD, Item, Button.Position + FVector2D(3, 5), 20.f);
            DrawText(HUD, Label.Left(bCompact ? 7 : 14), FLinearColor::White,
                Button.Position.X + 26, Button.Position.Y + 9, 0.8f);
        }
        const FString Detail = HasItem
            ? Item.Description.ToString() : Reward.Kind == ERewardKind::RandomItem ? TEXT("무작위 아이템 1개")
            : Reward.Kind == ERewardKind::Gold ? TEXT("Gold") : TEXT("Unit");
        DrawText(HUD, Detail.Left(bCompact ? 13 : 24), Muted, CardX + 4,
            Y + (bCompact ? 70.f : 86.f) + FeedbackHeight, 0.8f);
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
    DrawRect(HUD, PanelBackground, X, Y, 240, 100);
    DrawText(HUD, FString::Printf(TEXT("%s  %d*"), *UnitLabel(*Manager.Shop, Unit.UnitId), Unit.Stars),
        GoldColor, X + 8, Y + 6);
    if (bHasStats)
    {
        DrawText(HUD, FString::Printf(TEXT("HP %.0f   AD %.0f   AP %.0f"), Profile.MaxHP, Profile.AttackDamage, Profile.AbilityPower),
            FLinearColor::White, X + 8, Y + 26);
        DrawText(HUD, FString::Printf(TEXT("Armor %.0f   AS %.2f   MP %.0f"), Profile.Armor, Profile.AttackSpeed, Profile.MaxMP),
            FLinearColor::White, X + 8, Y + 44);
    }
    TArray<FString> Items;
    TArray<FName> Equipment;
    Manager.Roster->GetUnitEquipment(Id, Equipment);
    for (FName Item : Equipment)
    {
        FItemDefinition Definition;
        Manager.Items->GetItem(Item, Definition);
        Items.Add(ItemLabel(*Manager.Items, Item));
        if (Equipment.Num() == 1 && !Definition.Description.IsEmpty()) Items.Add(Definition.Description.ToString());
    }
    DrawText(HUD, FString::Join(Items, TEXT(" / ")).Left(40), Muted, X + 8, Y + 65, 0.85f);
}

bool UShopPanel::IsOverButton(FVector2D ViewportPosition) const
{
    if (WidgetScreen) return WidgetScreen->BlocksWorldInput(ViewportPosition);
    if (!DraggingItem.IsNone()) return true;
    const FVector2D LocalPosition = ToCanvasUnits(ViewportPosition);
    return Buttons.ContainsByPredicate([LocalPosition](const FButton& Button)
    {
        return Button.Contains(LocalPosition);
    });
}

void UShopPanel::HandleInput(const FInputKeyEventArgs& Event)
{
    if (WidgetScreen)
    {
        const bool bCancel = Event.Key == EKeys::RightMouseButton;
        if ((Event.Event == IE_Pressed || Event.Event == IE_DoubleClick)
            && (bCancel || Event.Key == EKeys::LeftMouseButton))
        {
            float X = 0.f;
            float Y = 0.f;
            const APlayerController* Player = WidgetScreen->GetOwningPlayer();
            if (bCancel || (Player && Player->GetMousePosition(X, Y)))
                WidgetScreen->OnWorldPointerPressed(FVector2D(X, Y), bCancel);
        }
        return;
    }
    AShopTransactionManager* Manager = Transactions.Get();
    AHUD* HUD = ActiveHUD.Get();
    if (!Manager || !Manager->IsConfigured() || !HUD) return;
    if (Event.Key == EKeys::LeftMouseButton && Event.Event == IE_Released && !DraggingItem.IsNone())
    {
        const FName Item = DraggingItem;
        float MouseX, MouseY;
        APlayerController* PC = HUD->GetOwningPlayerController();
        FGuid Target;
        if (PC && PC->GetMousePosition(MouseX, MouseY))
        {
            const FVector2D LocalMouse = ToCanvasUnits(FVector2D(MouseX, MouseY));
            const bool OverUI = Buttons.ContainsByPredicate([LocalMouse](const FButton& Button)
            {
                return Button.Contains(LocalMouse);
            });
            if (FVector2D::Distance(ItemDragStart, FVector2D(MouseX, MouseY)) < 4.f * CanvasScale)
                Target = Selected;
            else if (!OverUI)
            {
                FHitResult Hit;
                if (PC->GetHitResultUnderCursor(ECC_Visibility, true, Hit))
                {
                    AActor* Actor = Hit.GetActor();
                    if (const auto* Tile = Cast<ABoardTile>(Actor)) Actor = Tile->OccupiedUnit;
                    for (const FRosterUnit& Unit : Manager->Roster->GetUnits())
                        if (IUnitPlacement::Execute_GetBattleUnit(Manager->Roster->Placement.GetObject(), Unit.Id) == Actor)
                            Target = Unit.Id;
                }
            }
        }
        CancelItemDrag();
        if (Target.IsValid())
        {
            const ETransactionResult Result = Manager->RequestEquip(Target, Item);
            Feedback = Result == ETransactionResult::NoSpace
                ? TEXT("장착 슬롯이 가득 찼습니다.\n기물당 최대 3개까지 장착할 수 있습니다.") : ResultText(Result);
            if (Result == ETransactionResult::Success) Selected = Target;
        }
        else Feedback = TEXT("보드에 배치된 아군에게 아이템을 놓아주세요.");
        return;
    }
    if (Event.Event != IE_Pressed && Event.Event != IE_DoubleClick) return;
    if (Event.Key == EKeys::RightMouseButton)
    {
        CancelItemDrag();
        Selected.Invalidate();
        return;
    }
    if (Event.Key != EKeys::LeftMouseButton) return;

    float X, Y;
    APlayerController* PC = HUD->GetOwningPlayerController();
    if (!PC || !PC->GetMousePosition(X, Y)) return;
    const FVector2D LocalMouse = ToCanvasUnits(FVector2D(X, Y));
    for (int32 Index = Buttons.Num() - 1; Index >= 0; --Index)
    {
        const FButton Button = Buttons[Index];

        if (!Button.Contains(LocalMouse)) continue;
        if (Button.Action == EAction::Equip && Button.bEnabled)
        {
            DraggingItem = Button.Item;
            ItemDragStart = FVector2D(X, Y);
            if (auto* Mode = Cast<AAutoBattleGameMode>(GetWorld()->GetAuthGameMode()))
                Mode->SetManagedDragState(false, true);
            return;
        }
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
        case EAction::Reroll: Result = Manager.RequestReroll(); break;
        case EAction::Lock: Result = Manager.RequestShopLock(!Manager.Shop->IsShopLocked()); break;
        case EAction::Equip: Result = Manager.RequestEquip(Selected, Button.Item); break;
        case EAction::Unequip: Result = Manager.RequestUnequip(Button.Unit, Button.Index); break;
        case EAction::Sell: Result = Manager.RequestSell(Button.Unit); break;
        case EAction::Reward: Result = Manager.RequestReward(Button.Index, Button.Revision); break;
        case EAction::InventoryPage: ItemPage += Button.Index; break;
    }
    Feedback = Button.Action == EAction::Reward && Result == ETransactionResult::Success
        ? TEXT("Reward received.") : ResultText(Result);
    if (Button.Action == EAction::Reward && Result == ETransactionResult::NoSpace)
        Feedback = TEXT("벤치가 가득 찼습니다.\n기물을 판매하거나 보드로 옮긴 뒤\n보상을 다시 선택하세요.");
}
