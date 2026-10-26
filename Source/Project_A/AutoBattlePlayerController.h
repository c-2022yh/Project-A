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
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Selection")
    ABattleUnitBase* SelectedUnit = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Selection")
    ABoardTile* HoveredTile = nullptr;
    // Camera tag used by the Blueprint BeginPlay graph.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera")
    FName BattleCameraTag = TEXT("BattleCamera");
    // BP input graph uses this trace, then chooses selection or placement.
    UFUNCTION(BlueprintCallable, Category="Selection") bool GetBoardClickHit(FHitResult& Hit);
    UFUNCTION(BlueprintCallable, Category="Selection") void SelectUnit(ABattleUnitBase* Unit);
    UFUNCTION(BlueprintCallable, Category="Selection") void ClearSelection();
    UFUNCTION(BlueprintCallable, Category="Board") bool PlaceSelectedUnitOnTile(ABoardTile* Tile);
    UFUNCTION(BlueprintCallable, Category="Game") void RequestStartCombat();
    UFUNCTION(BlueprintCallable, Category="Game") void RequestRestart();
};
