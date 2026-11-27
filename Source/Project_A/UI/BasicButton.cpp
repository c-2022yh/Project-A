#include "BasicButton.h"
#include "ProjectUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UBasicButton::SetTheme(UProjectUITheme& InTheme)
{
    Theme = &InTheme;
    if (Button) Button->SetStyle(Theme->GetButtonStyle());
}

void UBasicButton::SetLabel(const FText& InLabel)
{
    LabelText = InLabel;
    if (Label) Label->SetText(LabelText);
}

void UBasicButton::SetBody(UWidget* Content)
{
    Body = Content;
    if (Button) Button->SetContent(Body);
}

TSharedRef<SWidget> UBasicButton::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        if (!Theme)
        {
            Theme = NewObject<UProjectUITheme>(this);
            Theme->Initialize();
        }
        Button = WidgetTree->ConstructWidget<UButton>();
        Button->SetStyle(Theme->GetButtonStyle());
        if (!Body)
        {
            Label = UProjectUITheme::Label(*WidgetTree, LabelText, 17, UProjectUITheme::Text, true);
            Label->SetColorAndOpacity(FSlateColor::UseForeground());
            Label->SetJustification(ETextJustify::Center);
        }
        Button->SetContent(Body ? Body.Get() : Label.Get());
        Button->OnClicked.AddDynamic(this, &UBasicButton::Clicked);
        WidgetTree->RootWidget = Button;
    }
    return Super::RebuildWidget();
}

void UBasicButton::Clicked()
{
    if (Action) Action();
    OnClicked.Broadcast();
}
