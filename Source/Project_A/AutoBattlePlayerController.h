#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AutoBattlePlayerController.generated.h"

class ABoardTile;
class ABenchTile;
class ABoardManager;
class ABattleUnitBase;

UCLASS()
class PROJECT_A_API AAutoBattlePlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AAutoBattlePlayerController();
    virtual void PlayerTick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABattleUnitBase* SelectedUnit = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABoardTile* HoveredTile = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABenchTile* HoveredBenchSlot = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection|Drag")
    bool bIsUnitDragging = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FName BattleCameraTag = TEXT("BattleCamera");

    UFUNCTION(BlueprintCallable, Category = "Selection")
    bool GetBoardClickHit(FHitResult& Hit);

    UFUNCTION(BlueprintCallable, Category = "Selection")
    void SelectUnit(ABattleUnitBase* Unit);

    UFUNCTION(BlueprintCallable, Category = "Selection")
    void ClearSelection();

    UFUNCTION(BlueprintCallable, Category = "Selection|Drag")
    bool BeginUnitDrag(ABattleUnitBase* Unit);

    UFUNCTION(BlueprintCallable, Category = "Selection|Drag")
    bool EndUnitDrag();

    UFUNCTION(BlueprintCallable, Category = "Selection|Drag")
    void CancelUnitDrag();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Drag",
        meta = (ClampMin = "0.0", UIMin = "0.0"))
    float UnitDragThreshold = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Drag",
        meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Additional height above the slot while dragging, in centimeters."))
    float UnitDragHeightOffset = 100.0f;

    UFUNCTION(BlueprintCallable, Category = "Board")
    bool PlaceSelectedUnitOnTile(ABoardTile* Tile);

    UFUNCTION(BlueprintCallable, Category = "Board")
    bool MoveSelectedUnitToBench(ABenchTile* Slot);

    UFUNCTION(BlueprintCallable, Category = "Game")
    void RequestStartCombat();

    UFUNCTION(BlueprintCallable, Category = "Game")
    void RequestRestart();

private:
    bool bUnitDragPending = false;
    FVector2D UnitDragStartScreenPosition = FVector2D::ZeroVector;
    FTransform UnitDragStartTransform = FTransform::Identity;
    float UnitDragSurfaceZ = 0.0f;

    bool IsPointerOverHUD() const;
    bool IsUnitPlacementPhaseActive() const;
    bool IsUnitInteractionAllowed(const ABattleUnitBase* Unit) const;
    ABoardManager* GetBoardManager() const;
    AActor* GetDragDropSlotAtCursor(FHitResult* OutHit = nullptr) const;
    void UpdateDraggedUnitLocation();
};
