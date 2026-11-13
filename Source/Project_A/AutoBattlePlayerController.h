#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AutoBattlePlayerController.generated.h"
class ABoardTile;
class ABattleUnitBase;
UCLASS()
class PROJECT_A_API AAutoBattlePlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AAutoBattlePlayerController();
    virtual void PlayerTick(float DeltaSeconds) override;

    // 선택된 유닛
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Selection")
    ABattleUnitBase* SelectedUnit = nullptr;

    // 호버된 타일
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Selection")
    ABoardTile* HoveredTile = nullptr;

    // 드래그 중인지 상태 확인
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Selection|Drag")
    bool bIsUnitDragging = false;

    // Camera tag used by the Blueprint BeginPlay graph.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera")
    FName BattleCameraTag = TEXT("BattleCamera");
    // BP input graph uses this trace, then chooses selection or placement.
    UFUNCTION(BlueprintCallable, Category="Selection") bool GetBoardClickHit(FHitResult& Hit);
    UFUNCTION(BlueprintCallable, Category="Selection") void SelectUnit(ABattleUnitBase* Unit);
    UFUNCTION(BlueprintCallable, Category="Selection") void ClearSelection();

    // Call from the left mouse button Pressed event after identifying the unit under the cursor.
    UFUNCTION(BlueprintCallable, Category="Selection|Drag") bool BeginUnitDrag(ABattleUnitBase* Unit);

    // Call from the left mouse button Released event. Placement is attempted only after the drag threshold is crossed.
    UFUNCTION(BlueprintCallable, Category="Selection|Drag") bool EndUnitDrag();
    UFUNCTION(BlueprintCallable, Category="Selection|Drag") void CancelUnitDrag();

    // 유닛 드래그 전환 임계값
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Drag", meta=(ClampMin="0.0", UIMin="0.0"))
    float UnitDragThreshold = 8.0f;

    // 유닛 이동 시 시각적으로 바닥으로부터 띄우는 높이
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Drag", meta=(ClampMin="0.0", UIMin="0.0", ToolTip="Additional height above the board while dragging, in centimeters."))
    float UnitDragHeightOffset = 100.0f;

    UFUNCTION(BlueprintCallable, Category="Board") bool PlaceSelectedUnitOnTile(ABoardTile* Tile);
    UFUNCTION(BlueprintCallable, Category="Game") void RequestStartCombat();
    UFUNCTION(BlueprintCallable, Category="Game") void RequestRestart();

private:
    bool bUnitDragPending = false;
    FVector2D UnitDragStartScreenPosition = FVector2D::ZeroVector;
    FTransform UnitDragStartTransform = FTransform::Identity;
    float UnitDragSurfaceZ = 0.0f;

    bool IsPointerOverHUD() const;
    bool IsUnitPlacementPhaseActive() const;
    ABoardTile* GetDragDropTileAtCursor(FHitResult* OutHit = nullptr) const;
    void UpdateDraggedUnitLocation();
};
