#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "ProjectUITheme.generated.h"

class UBorder;
class UCanvasPanel;
class UCanvasPanelSlot;
class UTextBlock;
class UWidget;
class UWidgetTree;

UCLASS()
class UProjectUITheme : public UObject
{
    GENERATED_BODY()

public:
    void Initialize();
    const FButtonStyle& GetButtonStyle() const { return ButtonStyle; }

    static const FLinearColor Ink;
    static const FLinearColor Panel;
    static const FLinearColor Gold;
    static const FLinearColor Text;
    static const FLinearColor Muted;
    static const FLinearColor Accent;

    static constexpr float Gap = 8.f;
    static constexpr float Padding = 16.f;
    static FLinearColor CostColor(int32 Cost);
    static UTextBlock* Label(UWidgetTree& Tree, const FText& Value, int32 Size,
        FLinearColor Color = Text, bool bBold = false);
    static UBorder* Frame(UWidgetTree& Tree, UWidget* Content, FMargin Inset = FMargin(Padding));
    static UCanvasPanelSlot* Pin(UCanvasPanel& Canvas, UWidget& Child, FVector2D Anchor,
        FVector2D Alignment, FVector2D Offset);

private:
    UPROPERTY(Transient)
    FButtonStyle ButtonStyle;
};
