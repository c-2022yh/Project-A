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

protected:
    virtual void BeginPlay() override;

public:
    //현재 선택한 유닛
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABattleUnitBase* SelectedUnit = nullptr;

    //현재 마우스가 가리키는 타일
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABoardTile* HoveredTile = nullptr;

    //유닛 선택
    UFUNCTION(BlueprintCallable, Category = "Selection")
    void SelectUnit(ABattleUnitBase* Unit);

    //선택 해제
    UFUNCTION(BlueprintCallable, Category = "Selection")
    void ClearSelection();

    //선택한 유닛을 타일에 배치
    UFUNCTION(BlueprintCallable, Category = "Board")
    bool PlaceSelectedUnitOnTile(ABoardTile* Tile);
};