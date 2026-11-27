#include "ProjectUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"

namespace
{
FLinearColor Color(const TCHAR* Hex)
{
    return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
}

FSlateBrush MakeFrameBrush(FLinearColor Top, FLinearColor Bottom, FLinearColor Rim, FLinearColor Shine)
{
    const FVector2D Size(64, 64);
    if (!FApp::CanEverRender())
        return FSlateRoundedBoxBrush(Top, 6.f, Rim, 1.f, Size);

    constexpr int32 Extent = 64;
    TArray64<uint8> Pixels;
    Pixels.SetNumUninitialized(Extent * Extent * 4);
    for (int32 Y = 0; Y < Extent; ++Y)
    {
        const float Vertical = Y / float(Extent - 1);
        for (int32 X = 0; X < Extent; ++X)
        {
            const float Side = FMath::Min(X, Extent - 1 - X);
            const float End = FMath::Min(Y, Extent - 1 - Y);
            const float Edge = FMath::Min3(Side, End, (Side + End - 10.f) * 0.7071f);
            FLinearColor Pixel = FMath::Lerp(Top, Bottom, Vertical);
            if (Edge < 1.5f) Pixel = Color(TEXT("111A22"));
            else if (Edge < 3.5f) Pixel = FMath::Lerp(Shine, Rim, Vertical);
            else if (Edge < 5.f) Pixel = Rim * 0.54f;
            else if (Edge < 6.5f) Pixel = FMath::Lerp(Pixel, Shine, 0.18f);
            Pixel.A = FMath::Clamp(Edge + 0.5f, 0.f, 1.f);
            const FColor BGRA = Pixel.ToFColorSRGB();
            const int32 Index = (Y * Extent + X) * 4;
            Pixels[Index] = BGRA.B;
            Pixels[Index + 1] = BGRA.G;
            Pixels[Index + 2] = BGRA.R;
            Pixels[Index + 3] = BGRA.A;
        }
    }
    UTexture2D* Texture = UTexture2D::CreateTransient(Extent, Extent, PF_B8G8R8A8, NAME_None, Pixels);
    if (!Texture) return FSlateRoundedBoxBrush(Top, 6.f, Rim, 1.f, Size);
    Texture->NeverStream = true;
    Texture->UpdateResource();

    FSlateBrush Brush;
    Brush.SetResourceObject(Texture);
    Brush.ImageType = ESlateBrushImageType::FullColor;
    Brush.DrawAs = ESlateBrushDrawType::Box;
    Brush.ImageSize = Size;
    Brush.Margin = FMargin(0.25f);
    return Brush;
}
}

const FLinearColor UProjectUITheme::Ink = Color(TEXT("080F1B"));
const FLinearColor UProjectUITheme::Panel = Color(TEXT("121F30F2"));
const FLinearColor UProjectUITheme::Gold = Color(TEXT("DDB36B"));
const FLinearColor UProjectUITheme::Text = Color(TEXT("F5EBD8"));
const FLinearColor UProjectUITheme::Muted = Color(TEXT("ACBAC8"));
const FLinearColor UProjectUITheme::Accent = Color(TEXT("73D7E3"));

void UProjectUITheme::Initialize()
{
    ButtonStyle.SetNormal(MakeFrameBrush(Color(TEXT("23374B")), Color(TEXT("101B2A")), Color(TEXT("9F6C37")), Gold));
    ButtonStyle.SetHovered(MakeFrameBrush(Color(TEXT("22506A")), Color(TEXT("102E43")), Gold, Color(TEXT("FFF0BC"))));
    ButtonStyle.SetPressed(MakeFrameBrush(Color(TEXT("101E2A")), Color(TEXT("193449")), Color(TEXT("81582E")), Gold));
    ButtonStyle.SetDisabled(MakeFrameBrush(Color(TEXT("28313C")), Color(TEXT("181E27")), Color(TEXT("5F646E")), Color(TEXT("848A92"))));
    ButtonStyle.SetNormalPadding(FMargin(12.f, 7.f));
    ButtonStyle.SetPressedPadding(FMargin(12.f, 9.f, 12.f, 5.f));
    ButtonStyle.SetNormalForeground(FSlateColor(Text));
    ButtonStyle.SetHoveredForeground(FSlateColor(Color(TEXT("FFF0BC"))));
    ButtonStyle.SetPressedForeground(FSlateColor(Gold));
    ButtonStyle.SetDisabledForeground(FSlateColor(Color(TEXT("9BA3AE"))));
}

FLinearColor UProjectUITheme::CostColor(int32 Cost)
{
    static const FLinearColor Colors[] = { Color(TEXT("B1BECC")), Color(TEXT("75D7A1")), Color(TEXT("73BFF3")),
        Color(TEXT("C79BFF")), Color(TEXT("F4C26F")) };
    return Colors[FMath::Clamp(Cost, 1, 5) - 1];
}

UTextBlock* UProjectUITheme::Label(UWidgetTree& Tree, const FText& Value, int32 Size, FLinearColor Tint, bool bBold)
{
    auto* Label = Tree.ConstructWidget<UTextBlock>();
    Label->SetText(Value);
    Label->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
    Label->SetColorAndOpacity(FSlateColor(Tint));
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Label;
}

UBorder* UProjectUITheme::Frame(UWidgetTree& Tree, UWidget* Content, FMargin Inset)
{
    auto* Border = Tree.ConstructWidget<UBorder>();
    Border->SetBrush(FSlateRoundedBoxBrush(Panel, 7.f, Color(TEXT("80603D")), 1.f));
    Border->SetPadding(Inset);
    if (Content) Border->SetContent(Content);
    return Border;
}

UCanvasPanelSlot* UProjectUITheme::Pin(UCanvasPanel& Canvas, UWidget& Child, FVector2D Anchor,
    FVector2D Alignment, FVector2D Offset)
{
    auto* Slot = Canvas.AddChildToCanvas(&Child);
    Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
    Slot->SetAlignment(Alignment);
    Slot->SetPosition(Offset);
    Slot->SetAutoSize(true);
    return Slot;
}
