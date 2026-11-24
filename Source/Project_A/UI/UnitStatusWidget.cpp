#include "UnitStatusWidget.h"
#include "../BattleUnitBase.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "../Shop/ShopTypes.h"

UUnitStatusWidget::UUnitStatusWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetMinimumDesiredSize(GetStatusDrawSize());
}

FVector2D UUnitStatusWidget::GetStatusDrawSize() const
{
    return FVector2D(FMath::Max(40.f, BarWidth) + 4.f,
        FMath::Max(8.f, ItemIconSize) + FMath::Max(3.f, HealthHeight) + FMath::Max(2.f, ManaHeight) + 12.f);
}

void UUnitStatusWidget::BindUnit()
{
    if (ABattleUnitBase* Unit = SourceUnit.Get())
    {
        Unit->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnManaChanged.AddUniqueDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnShieldChanged.AddUniqueDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnStatsChanged.AddUniqueDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnEquipmentChanged.AddUniqueDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnUnitDied.AddUniqueDynamic(this, &ThisClass::HandleUnitChanged);
    }
}

void UUnitStatusWidget::UnbindUnit()
{
    if (ABattleUnitBase* Unit = SourceUnit.Get())
    {
        Unit->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnManaChanged.RemoveDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnShieldChanged.RemoveDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnStatsChanged.RemoveDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnEquipmentChanged.RemoveDynamic(this, &ThisClass::HandleUnitChanged);
        Unit->OnUnitDied.RemoveDynamic(this, &ThisClass::HandleUnitChanged);
    }
}

void UUnitStatusWidget::SetUnit(ABattleUnitBase* Unit)
{
    if (SourceUnit.Get() != Unit)
    {
        UnbindUnit();
        SourceUnit = Unit;
        ItemIcons.Empty();
        ItemLabels.Empty();
    }
    BindUnit();
    RefreshStatus();
}

void UUnitStatusWidget::RefreshStatus()
{
    const ABattleUnitBase* Unit = SourceUnit.Get();
    if (!Unit || !Unit->bInitialized || !Unit->IsAlive())
    {
        SetVisibility(ESlateVisibility::Hidden);
        return;
    }
    Health = FMath::Max(0.f, Unit->CurrentHP);
    MaxHealth = FMath::Max(1.f, Unit->FinalStats.MaxHealth);
    Shield = FMath::Max(0.f, Unit->CurrentShield);
    bEnemy = Unit->TeamType == EBattleTeam::Enemy;
    bShowMana = Unit->UsesMana() && Unit->FinalStats.MaxMana > 0.f;
    ManaFraction = bShowMana ? FMath::Clamp(Unit->CurrentMana / Unit->FinalStats.MaxMana, 0.f, 1.f) : 0.f;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    InvalidateLayoutAndVolatility();
}

void UUnitStatusWidget::HandleUnitChanged(ABattleUnitBase* Unit) { RefreshStatus(); }

void UUnitStatusWidget::SetItemIcons(const TArray<UTexture2D*>& Icons)
{
    ItemIcons.Empty();
    ItemLabels.Empty();
    for (UTexture2D* Icon : Icons) if (IsValid(Icon)) ItemIcons.Add(Icon);
    InvalidateLayoutAndVolatility();
}

void UUnitStatusWidget::SetEquipment(const TArray<FItemDefinition>& Equipment)
{
    ItemIcons.Reset();
    ItemLabels.Reset();
    for (const FItemDefinition& Item : Equipment)
    {
        ItemIcons.Add(Item.Icon);
        ItemLabels.Add(Item.Label);
    }
    InvalidateLayoutAndVolatility();
}

void UUnitStatusWidget::NativeConstruct()
{
    Super::NativeConstruct();
    BindUnit();
    RefreshStatus();
}

void UUnitStatusWidget::NativeDestruct()
{
    UnbindUnit();
    Super::NativeDestruct();
}

void UUnitStatusWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    SetMinimumDesiredSize(GetStatusDrawSize());
    if (IsDesignTime())
    {
        // 디자이너 미리보기 전용 값이다. 실제 유닛/아이템 상태에는 쓰지 않는다.
        Health = 720.f; MaxHealth = 1000.f; Shield = 180.f; ManaFraction = 0.55f;
        bEnemy = false; bShowMana = true;
        SetVisibility(ESlateVisibility::HitTestInvisible);
    }
}

int32 UUnitStatusWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    int32 Layer = Super::NativePaint(Args, Geometry, CullingRect, Elements, LayerId, Style, bParentEnabled);
    const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
    const FLinearColor Tint = Style.GetColorAndOpacityTint();
    const auto Box = [&](float X, float Y, float W, float H, FLinearColor Color)
    {
        if (W > 0.f && H > 0.f)
            FSlateDrawElement::MakeBox(Elements, ++Layer, Geometry.ToPaintGeometry(FVector2D(W,H), FSlateLayoutTransform(FVector2D(X,Y))),
                White, ESlateDrawEffect::None, Color * Tint);
    };
    const float Width = FMath::Max(40.f, BarWidth);
    const float HPHeight = FMath::Max(3.f, HealthHeight);
    const float MPHeight = FMath::Max(2.f, ManaHeight);
    const float IconSize = FMath::Max(8.f, ItemIconSize);
    const float HealthY = IconSize + 6.f;
    const FLinearColor Frame(0.015f, 0.021f, 0.028f, 1.f);
    const FLinearColor Background(0.055f, 0.068f, 0.078f, 1.f);

    // 아이콘이 없으면 빈 슬롯을 그리지 않는다. 장착 아이콘은 위쪽 왼편에 작게 모은다.
    const int32 IconCount = FMath::Min(ItemIcons.Num(), FMath::FloorToInt(Width / (IconSize + 3.f)));
    const int32 DrawCount = IsDesignTime() && IconCount == 0 ? 3 : IconCount;
    for (int32 Index = 0; Index < DrawCount; ++Index)
    {
        const float X = 1.f + Index * (IconSize + 3.f);
        Box(X, 1.f, IconSize + 2.f, IconSize + 2.f, FLinearColor(0.32f,0.26f,0.14f,1));
        Box(X+1.f, 2.f, IconSize, IconSize, Background);
        if (ItemIcons.IsValidIndex(Index) && IsValid(ItemIcons[Index]))
        {
            FSlateBrush IconBrush;
            IconBrush.SetResourceObject(ItemIcons[Index]);
            IconBrush.DrawAs = ESlateBrushDrawType::Image;
            FSlateDrawElement::MakeBox(Elements, ++Layer, Geometry.ToPaintGeometry(FVector2D(IconSize,IconSize), FSlateLayoutTransform(FVector2D(X+1.f,2.f))),
                &IconBrush, ESlateDrawEffect::None, Tint);
        }
        else if (ItemLabels.IsValidIndex(Index))
        {
            FSlateDrawElement::MakeText(Elements, ++Layer,
                Geometry.ToPaintGeometry(FVector2D(IconSize, IconSize), FSlateLayoutTransform(FVector2D(X+2.f, 3.f))),
                FText::FromString(ItemLabels[Index].ToString().Left(2)),
                FCoreStyle::GetDefaultFontStyle("Bold", FMath::Max(8, FMath::RoundToInt(IconSize * 0.45f))),
                ESlateDrawEffect::None, FLinearColor(1.f, 0.82f, 0.39f) * Tint);
        }
    }

    Box(0.f, HealthY+1.f, Width+4.f, HPHeight+3.f, FLinearColor(0,0,0,0.45f));
    Box(1.f, HealthY, Width+2.f, HPHeight+2.f, Frame);
    Box(2.f, HealthY+1.f, Width, HPHeight, Background);
    // 보호막은 체력 뒤의 흰 구간이다. 총량이 최대 체력을 넘으면 두 구간을 함께 압축한다.
    const float Capacity = FMath::Max(1.f, FMath::Max(MaxHealth, Health + Shield));
    const float HPWidth = Width * FMath::Clamp(Health / Capacity, 0.f, 1.f);
    const float ShieldWidth = Width * FMath::Clamp(Shield / Capacity, 0.f, 1.f);
    const FLinearColor HPColor = bEnemy ? EnemyHealthColor : AllyHealthColor;
    Box(2.f, HealthY+1.f, HPWidth, HPHeight, HPColor);
    Box(2.f, HealthY+1.f, HPWidth, 1.f, FLinearColor(1,1,1,0.22f));
    Box(2.f+HPWidth, HealthY+1.f, ShieldWidth, HPHeight, ShieldColor);
    if (HealthPerSegment > 0.f)
    {
        const float Step = HealthPerSegment * FMath::Max(1.f, FMath::CeilToFloat(Capacity / HealthPerSegment / 12.f));
        for (float Value = Step; Value < Capacity; Value += Step)
            Box(FMath::RoundToFloat(2.f + Width * Value / Capacity), HealthY+1.f, 1.f, HPHeight, FLinearColor(0,0,0,0.65f));
    }
    if (bShowMana)
    {
        const float ManaY = HealthY + HPHeight + 3.f;
        Box(1.f, ManaY, Width+2.f, MPHeight+2.f, Frame);
        Box(2.f, ManaY+1.f, Width, MPHeight, FLinearColor(0.025f,0.055f,0.09f,1));
        Box(2.f, ManaY+1.f, Width * ManaFraction, MPHeight, ManaColor);
        Box(2.f, ManaY+1.f, Width * ManaFraction, 1.f, FLinearColor(0.65f,0.85f,1.f,0.3f));
    }
    return Layer;
}
