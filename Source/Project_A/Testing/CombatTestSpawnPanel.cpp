#include "CombatTestSpawnPanel.h"
#include "UnitCombatTestRunner.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
class SCombatTestSpawnPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCombatTestSpawnPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&, AUnitCombatTestRunner* InRunner)
    {
        Runner = InRunner;
        for (const FName Row : InRunner->GetSpawnRows()) Rows.Add(MakeShared<FName>(Row));
        if (!Rows.IsEmpty()) SelectedRow = Rows[0];

        ChildSlot
        [
            SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(16.f)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.025f, 0.035f, 0.055f, 0.96f))
                .Padding(12.f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("Combat Test")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 5, 0)
                        [
                            SNew(SComboBox<TSharedPtr<FName>>)
                            .OptionsSource(&Rows)
                            .InitiallySelectedItem(SelectedRow)
                            .OnGenerateWidget_Lambda([](TSharedPtr<FName> Row)
                            {
                                return SNew(STextBlock).Text(FText::FromName(Row ? *Row : NAME_None));
                            })
                            .OnSelectionChanged_Lambda([this](TSharedPtr<FName> Row, ESelectInfo::Type) { SelectedRow = Row; })
                            [
                                SNew(STextBlock).Text_Lambda([this]()
                                {
                                    return SelectedRow ? FText::FromName(*SelectedRow) : FText::FromString(TEXT("No DT rows"));
                                })
                            ]
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
                        [
                            SNew(SButton)
                            .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d-star"), Stars)); })
                            .ToolTipText(FText::FromString(TEXT("Switch between 1-star and 2-star units")))
                            .OnClicked_Lambda([this]() { Stars = 3 - Stars; return FReply::Handled(); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SButton)
                            .Text_Lambda([this]() { return FText::FromString(bRemoving ? TEXT("Mode: Remove") : TEXT("Mode: Place")); })
                            .OnClicked_Lambda([this]() { bRemoving = !bRemoving; return FReply::Handled(); })
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("Blue: Ally squares  |  Orange: Enemy squares")))
                    ]
                    + SVerticalBox::Slot().AutoHeight().MaxHeight(320.f)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [
                            SAssignNew(Grid, SUniformGridPanel).SlotPadding(FMargin(2.f))
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 5, 0)
                        [
                            SNew(SButton).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Start battle")))
                            .IsEnabled_Lambda([this]() { return Runner.IsValid() && Runner->CanEditLineup(); })
                            .OnClicked_Lambda([this]()
                            {
                                if (Runner.IsValid()) Runner->StartLineup();
                                return FReply::Handled();
                            })
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.f)
                        [
                            SNew(SButton).HAlign(HAlign_Center).Text(FText::FromString(TEXT("Stop / Clear")))
                            .OnClicked_Lambda([this]()
                            {
                                if (Runner.IsValid()) Runner->StopTest();
                                bRemoving = false;
                                return FReply::Handled();
                            })
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).WrapTextAt(392.f).Text_Lambda([this]()
                        {
                            return Runner.IsValid() ? FText::FromString(Runner->Status) : FText::GetEmpty();
                        })
                    ]
                ]
            ]
        ];
        RefreshGrid();
    }

    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override
    {
        SCompoundWidget::Tick(Geometry, Time, Delta);
        RefreshGrid();
    }

private:
    void RefreshGrid()
    {
        if (!Runner.IsValid() || !IsValid(Runner->Board)) return;
        const auto& Tiles = Runner->Board->Tiles;
        bool bChanged = Tiles.Num() != DrawnTiles.Num();
        for (int32 Index = 0; !bChanged && Index < Tiles.Num(); ++Index)
            bChanged = DrawnTiles[Index].Get() != Tiles[Index];
        if (!bChanged) return;

        Grid->ClearChildren();
        DrawnTiles.Reset();
        for (ABoardTile* Tile : Tiles)
        {
            const TWeakObjectPtr<ABoardTile> WeakTile(Tile);
            DrawnTiles.Add(WeakTile);
            if (!IsValid(Tile)) continue;
            Grid->AddSlot(Tile->Column, Tile->Row)
            [
                SNew(SBox).WidthOverride(76.f).HeightOverride(35.f)
                [
                    SNew(SButton).HAlign(HAlign_Center).VAlign(VAlign_Center).ContentPadding(FMargin(2.f))
                    .ButtonColorAndOpacity_Lambda([WeakTile]()
                    {
                        if (!WeakTile.IsValid()) return FSlateColor(FLinearColor::Gray);
                        return FSlateColor(WeakTile->TeamType == EBoardTileTeam::Player
                            ? FLinearColor(0.25f, 0.54f, 0.85f)
                            : WeakTile->TeamType == EBoardTileTeam::Enemy
                                ? FLinearColor(0.85f, 0.44f, 0.22f) : FLinearColor::Gray);
                    })
                    .IsEnabled_Lambda([this, WeakTile]()
                    {
                        if (!Runner.IsValid() || !Runner->CanEditLineup() || !WeakTile.IsValid()) return false;
                        if (bRemoving) return Runner->IsOwnedUnit(Cast<ABattleUnitBase>(WeakTile->OccupiedUnit));
                        return SelectedRow.IsValid() && WeakTile->CanPlaceUnit()
                            && (WeakTile->TeamType == EBoardTileTeam::Player || WeakTile->TeamType == EBoardTileTeam::Enemy);
                    })
                    .OnClicked_Lambda([this, WeakTile]()
                    {
                        if (Runner.IsValid() && WeakTile.IsValid())
                        {
                            if (bRemoving) Runner->RemoveFromTile(WeakTile.Get());
                            else if (SelectedRow.IsValid()) Runner->SpawnOnTile(*SelectedRow, Stars, WeakTile.Get());
                        }
                        return FReply::Handled();
                    })
                    [
                        SNew(STextBlock)
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                        .Text_Lambda([WeakTile]()
                        {
                            if (!WeakTile.IsValid()) return FText::GetEmpty();
                            if (const auto* Unit = Cast<ABattleUnitBase>(WeakTile->OccupiedUnit))
                                return FText::FromString(FString::Printf(TEXT("%s %d*"), *Unit->UnitID.ToString(), Unit->StarLevel));
                            return FText::FromString(FString::Printf(TEXT("%d, %d"), WeakTile->Row + 1, WeakTile->Column + 1));
                        })
                    ]
                ]
            ];
        }
    }

    TWeakObjectPtr<AUnitCombatTestRunner> Runner;
    TArray<TSharedPtr<FName>> Rows;
    TSharedPtr<FName> SelectedRow;
    TSharedPtr<SUniformGridPanel> Grid;
    TArray<TWeakObjectPtr<ABoardTile>> DrawnTiles;
    int32 Stars = 1;
    bool bRemoving = false;
};
}

TSharedRef<SWidget> CreateCombatTestSpawnPanel(AUnitCombatTestRunner* Runner)
{
    return SNew(SCombatTestSpawnPanel, Runner).Visibility(EVisibility::SelfHitTestInvisible);
}
