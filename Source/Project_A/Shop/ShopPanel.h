#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopPanel.generated.h"

class AShopManager;
class AShopTransactionManager;
class AHUD;
class IInputProcessor;
class UCanvas;
class UGameViewportClient;
class UProjectBattleScreen;
struct FItemDefinition;
struct FInputKeyEventArgs;

UCLASS()
class PROJECT_A_API UShopPanel : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;

    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    bool IsOverButton(FVector2D ViewportPosition) const;

    UFUNCTION(BlueprintPure, Category="UI")
    UProjectBattleScreen* GetBattleScreen() const { return WidgetScreen; }

private:
    enum class EAction : uint8 { Buy, Reroll, Lock, Equip, Unequip, Sell, Reward, InventoryPage };

    struct FButton
    {
        EAction Action;
        FVector2D Position;
        FVector2D Size;
        int32 Index = 0;
        FGuid Unit;
        FName Item;
        int32 Revision = 0;
        bool bEnabled = true;

        bool Contains(FVector2D Point) const
        {
            const FVector2D Offset = Point - Position;
            return Offset.X >= 0 && Offset.Y >= 0 && Offset.X < Size.X && Offset.Y < Size.Y;
        }
    };

    UPROPERTY(Transient)
    TObjectPtr<UProjectBattleScreen> WidgetScreen;

    bool bUseWidgetScreen = false;
    TWeakObjectPtr<AShopTransactionManager> Transactions;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakObjectPtr<AHUD> ActiveHUD;
    FDelegateHandle DrawHandle;
    FDelegateHandle InputHandle;
    TSharedPtr<IInputProcessor> PauseInput;
    TArray<FButton> Buttons;
    FVector2D ViewOrigin = FVector2D::ZeroVector;
    FVector2D ViewSize = FVector2D::ZeroVector;
    float CanvasScale = 1.f;
    float ShopBottom = 0.f;
    FGuid Selected;
    FString Feedback;
    int32 DisplayedRewardRevision = INDEX_NONE;
    int32 ItemPage = 0;
    FName DraggingItem;
    FVector2D ItemDragStart = FVector2D::ZeroVector;

    void ClearBindings();
    void DrawWidgetScreen(AHUD& HUD);
    void Draw(AHUD* HUD, UCanvas* Canvas);
    void DrawShop(AHUD& HUD, const AShopManager& Manager, const FShopMatchState& Match);
    void DrawItemsAndUnitActions(AHUD& HUD, const AShopTransactionManager& Manager);
    void DrawRewards(AHUD& HUD, const AShopTransactionManager& Manager);
    void DrawProfile(AHUD& HUD, const AShopTransactionManager& Manager, FGuid Id, FVector2D Position);
    FVector2D ToCanvasUnits(FVector2D ViewportPosition) const;
    void DrawRect(AHUD& HUD, FLinearColor Color, float X, float Y, float Width, float Height) const;
    void DrawText(AHUD& HUD, const FString& Text, FLinearColor Color, float X, float Y, float Scale = 1.f) const;
    void AddButton(AHUD& HUD, const FString& Label, const FButton& Button, bool bEnabled = true, bool bSelected = false);
    void HandleInput(const FInputKeyEventArgs& Event);
    void RunAction(const FButton& Button, AShopTransactionManager& Manager);
    void CancelItemDrag();
    void DrawItemIcon(AHUD& HUD, const FItemDefinition& Item, FVector2D Position, float Size);
    void DrawItemTooltip(AHUD& HUD, const FItemDefinition& Item, FVector2D Position);
};
