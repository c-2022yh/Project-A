#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopPanel.generated.h"

class AShopManager;
class AShopTransactionManager;
class AHUD;
class UCanvas;
class UGameViewportClient;
struct FInputKeyEventArgs;

UCLASS()
class PROJECT_A_API UShopPanel : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;

    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

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
    };

    TWeakObjectPtr<AShopTransactionManager> Transactions;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakObjectPtr<AHUD> ActiveHUD;
    FDelegateHandle DrawHandle;
    FDelegateHandle InputHandle;
    TArray<FButton> Buttons;
    FVector2D ViewOrigin = FVector2D::ZeroVector;
    FVector2D ViewSize = FVector2D::ZeroVector;
    FGuid Selected;
    FString Feedback;
    int32 ItemPage = 0;

    void ClearBindings();
    void Draw(AHUD* HUD, UCanvas* Canvas);
    void DrawShop(AHUD& HUD, const AShopManager& Manager);
    void DrawItemsAndUnitActions(AHUD& HUD, const AShopTransactionManager& Manager);
    void DrawRewards(AHUD& HUD, const AShopTransactionManager& Manager);
    void DrawProfile(AHUD& HUD, const AShopTransactionManager& Manager, FGuid Id, FVector2D Position);
    void AddButton(AHUD& HUD, const FString& Label, const FButton& Button, bool bEnabled = true, bool bSelected = false);
    void HandleInput(const FInputKeyEventArgs& Event);
    void RunAction(const FButton& Button, AShopTransactionManager& Manager);
};
