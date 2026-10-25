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
protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Selection")
    ABattleUnitBase* SelectedUnit = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Selection")
    ABoardTile* HoveredTile = nullptr;
    // 지정 태그가 있는 CameraActor를 우선 사용하고, 없으면 레벨에서 처음 찾은 CameraActor를 사용한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera")
    FName BattleCameraTag = TEXT("BattleCamera");
    UFUNCTION(BlueprintCallable, Category="Selection") void SelectUnit(ABattleUnitBase* Unit);
    UFUNCTION(BlueprintCallable, Category="Selection") void ClearSelection();
    UFUNCTION(BlueprintCallable, Category="Board") bool PlaceSelectedUnitOnTile(ABoardTile* Tile);
    UFUNCTION(BlueprintCallable, Category="Game") void RequestStartCombat();
    UFUNCTION(BlueprintCallable, Category="Game") void RequestRestart();
private:
    void HandlePrimaryClick();
};
