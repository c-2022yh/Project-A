#pragma once

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h"
#include "../Shop/ShopTypes.h"
#include "ProjectBattleScreen.generated.h"

class ABattleUnitBase;
class AShopTransactionManager;
class UBasicButton;
class UBorder;
class UCanvasPanelSlot;
class UHorizontalBox;
class UProjectUITheme;
class UScrollBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;

UCLASS()
class UShopItemDrag : public UDragDropOperation
{
    GENERATED_BODY()

public:
    FName ItemId;
};

UCLASS()
class UProjectBattleScreen : public UUserWidget
{
    GENERATED_BODY()

public:
    bool BlocksWorldInput(FVector2D ViewportPixel) const;
    void OnWorldPointerPressed(FVector2D ViewportPixel, bool bCancel);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual void NativeDestruct() override;
    virtual bool NativeOnDrop(const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation) override;

private:
    friend class UInventorySlot;

    UPROPERTY(Transient) TObjectPtr<UProjectUITheme> Theme;
    UPROPERTY(Transient) TObjectPtr<AShopTransactionManager> Transactions;
    UPROPERTY(Transient) TObjectPtr<UBorder> ShopFrame;
    UPROPERTY(Transient) TObjectPtr<UBorder> RewardFrame;
    UPROPERTY(Transient) TObjectPtr<UBorder> InventoryFrame;
    UPROPERTY(Transient) TObjectPtr<UBorder> UnitFrame;
    UPROPERTY(Transient) TObjectPtr<UBorder> ProfileFrame;
    UPROPERTY(Transient) TObjectPtr<UBorder> StatusFrame;
    UPROPERTY(Transient) TObjectPtr<UBorder> ResultFrame;
    UPROPERTY(Transient) TObjectPtr<UBorder> DropSurface;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> ProfilePosition;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Offers;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Rewards;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> Inventory;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> UnitActions;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Gold;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Round;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ProfileText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Result;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Feedback;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RewardSummary;
    UPROPERTY(Transient) TObjectPtr<UBasicButton> Reroll;
    UPROPERTY(Transient) TObjectPtr<UBasicButton> Lock;
    UPROPERTY(Transient) TObjectPtr<UBasicButton> StartCombat;
    UPROPERTY(Transient) TObjectPtr<UBasicButton> Restart;

    FGuid SelectedUnit;
    FGuid RenderedUnit;
    FGuid ProfileUnit;
    EShopPhase DisplayedPhase = EShopPhase::Inactive;
    int32 DisplayedRewardRevision = INDEX_NONE;
    bool bDirty = true;
    bool bItemDragging = false;

    UFUNCTION() void MarkDirty();
    void AttachTransactions(AShopTransactionManager* Manager);
    void Refresh(const FShopMatchState& Match);
    void RefreshShop(const FShopMatchState& Match);
    void RefreshRewards(const FShopMatchState& Match);
    void RefreshInventory(const FShopMatchState& Match);
    void RefreshUnit(const FShopMatchState& Match);
    void UpdateProfile(const FGeometry& Geometry, bool bRefresh);
    void SetItemDragging(bool bDragging);
    void EquipSelected(FName Item);
    FGuid FindUnitAt(FVector2D ViewportPixel) const;
    FGuid FindSelectedUnit() const;
    bool IsInsidePanel(FVector2D Absolute) const;
    UBasicButton* MakeButton(const FText& Label, TFunction<void()> Action);
    void Report(ETransactionResult Outcome);
};

UCLASS()
class UInventorySlot : public UUserWidget
{
    GENERATED_BODY()

public:
    void Configure(UProjectBattleScreen& InScreen, FName ItemId, const FItemDefinition& Definition);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual void NativeOnDragDetected(const FGeometry& Geometry, const FPointerEvent& Event,
        UDragDropOperation*& Operation) override;
    virtual void NativeOnDragCancelled(const FDragDropEvent& Event, UDragDropOperation* Operation) override;

private:
    UPROPERTY(Transient) TWeakObjectPtr<UProjectBattleScreen> Screen;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Icon;
    FName Item;
    FText Name;

    UWidget* MakeIcon(UWidgetTree& Tree) const;
};
