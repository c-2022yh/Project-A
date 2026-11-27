#include "ProjectMenus.h"

#include "BasicButton.h"
#include "ProjectUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Input/Events.h"

#define LOCTEXT_NAMESPACE "ProjectMenus"

namespace
{
struct FMenuLayout
{
    UBorder* Backdrop;
    UVerticalBox* Column;
};

FMenuLayout BuildMenu(UWidgetTree& Tree)
{
    auto* Overlay = Tree.ConstructWidget<UOverlay>();
    Overlay->SetClipping(EWidgetClipping::ClipToBounds);
    Tree.RootWidget = Overlay;

    auto* Backdrop = Tree.ConstructWidget<UBorder>();
    Backdrop->SetBrushColor(FLinearColor(0.008f, 0.015f, 0.028f, 0.72f));
    auto* BackgroundSlot = Overlay->AddChildToOverlay(Backdrop);
    BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
    BackgroundSlot->SetVerticalAlignment(VAlign_Fill);

    auto* Column = Tree.ConstructWidget<UVerticalBox>();
    auto* Width = Tree.ConstructWidget<USizeBox>();
    Width->SetWidthOverride(360.f);
    Width->SetContent(Column);
    auto* FrameSlot = Overlay->AddChildToOverlay(UProjectUITheme::Frame(Tree, Width, FMargin(28.f, 24.f)));
    FrameSlot->SetHorizontalAlignment(HAlign_Center);
    FrameSlot->SetVerticalAlignment(VAlign_Center);
    return { Backdrop, Column };
}

UTextBlock* AddHeading(UWidgetTree& Tree, UVerticalBox& Column, const FText& Text, int32 FontSize, FLinearColor Color)
{
    auto* Label = UProjectUITheme::Label(Tree, Text, FontSize, Color, true);
    Label->SetJustification(ETextJustify::Center);
    Column.AddChildToVerticalBox(Label);
    return Label;
}

void AddAction(UWidgetTree& Tree, UVerticalBox& Column, UUserWidget& Owner, UProjectUITheme& Theme,
    const FText& Label, const FSimpleDelegate& Action)
{
    auto* Button = CreateWidget<UBasicButton>(&Owner);
    Button->SetTheme(Theme);
    Button->SetLabel(Label);
    Button->Action = [Action] { Action.ExecuteIfBound(); };
    Button->SetIsEnabled(Action.IsBound());

    auto* Size = Tree.ConstructWidget<USizeBox>();
    Size->SetHeightOverride(48.f);
    Size->SetContent(Button);
    Column.AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, UProjectUITheme::Gap, 0.f, 0.f));
}

void AddMatchActions(UWidgetTree& Tree, UVerticalBox& Column, UUserWidget& Owner, UProjectUITheme& Theme,
    const FProjectMenuActions& Actions)
{
    AddAction(Tree, Column, Owner, Theme, LOCTEXT("Restart", "다시하기"), Actions.Restart);
    AddAction(Tree, Column, Owner, Theme, LOCTEXT("Title", "타이틀로"), Actions.ReturnToTitle);
    AddAction(Tree, Column, Owner, Theme, LOCTEXT("Quit", "종료하기"), Actions.Quit);
}
}

void UGameResultScreen::Configure(UProjectUITheme& InTheme, const FProjectMenuActions& InActions)
{
    Theme = &InTheme;
    Actions = InActions;
}

TSharedRef<SWidget> UGameResultScreen::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        if (!Theme)
        {
            Theme = NewObject<UProjectUITheme>(this);
            Theme->Initialize();
        }
        const auto Layout = BuildMenu(*WidgetTree);
        Backdrop = Layout.Backdrop;
        Heading = AddHeading(*WidgetTree, *Layout.Column, {}, 32, UProjectUITheme::Gold);
        Summary = AddHeading(*WidgetTree, *Layout.Column, {}, 17, UProjectUITheme::Muted);
        Buttons = WidgetTree->ConstructWidget<UVerticalBox>();
        Layout.Column->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
        AddMatchActions(*WidgetTree, *Buttons, *this, *Theme, Actions);
    }
    return Super::RebuildWidget();
}

void UGameResultScreen::Present(EProjectGameResult Result, int32 Round, int32 ClearedRounds, int32 TotalRounds)
{
    TakeWidget();
    if (!Heading) return;
    const bool bFinal = Result == EProjectGameResult::Victory || Result == EProjectGameResult::Defeat;
    const bool bDefeat = Result == EProjectGameResult::RoundDefeat || Result == EProjectGameResult::Defeat;
    if (Result == EProjectGameResult::RoundClear)
        Heading->SetText(FText::Format(LOCTEXT("RoundClear", "{0}라운드 클리어"), Round));
    else if (Result == EProjectGameResult::RoundDefeat)
        Heading->SetText(FText::Format(LOCTEXT("RoundDefeat", "{0}라운드 패배"), Round));
    else
        Heading->SetText(bDefeat ? LOCTEXT("Defeat", "패배") : LOCTEXT("Victory", "승리"));
    Heading->SetColorAndOpacity(bDefeat ? FLinearColor(1.f, 0.35f, 0.3f) : UProjectUITheme::Gold);
    Summary->SetText(FText::Format(LOCTEXT("ClearedRounds", "클리어 라운드  {0} / {1}"), ClearedRounds, TotalRounds));
    Backdrop->SetVisibility(bFinal ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Buttons->SetVisibility(bFinal ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    SetVisibility(bFinal ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
}

void UPauseMenu::Configure(UProjectUITheme& InTheme, const FProjectMenuActions& InActions, FSimpleDelegate InResume)
{
    Theme = &InTheme;
    Actions = InActions;
    Resume = MoveTemp(InResume);
    SetIsFocusable(true);
}

TSharedRef<SWidget> UPauseMenu::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        if (!Theme)
        {
            Theme = NewObject<UProjectUITheme>(this);
            Theme->Initialize();
        }
        const auto Layout = BuildMenu(*WidgetTree);
        AddHeading(*WidgetTree, *Layout.Column, LOCTEXT("Pause", "일시정지"), 28, UProjectUITheme::Gold);
        auto* Buttons = WidgetTree->ConstructWidget<UVerticalBox>();
        Layout.Column->AddChildToVerticalBox(Buttons)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
        AddAction(*WidgetTree, *Buttons, *this, *Theme, LOCTEXT("Resume", "계속하기"), Resume);
        AddMatchActions(*WidgetTree, *Buttons, *this, *Theme, Actions);
    }
    return Super::RebuildWidget();
}

FReply UPauseMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() != EKeys::Escape) return Super::NativeOnPreviewKeyDown(Geometry, Event);
    if (!Event.IsRepeat()) Resume.ExecuteIfBound();
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
