#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnitTypes.h"
#include "BattleUnitBase.generated.h"

class ABoardTile;
class UCapsuleComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class ABattleUnitBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBattleUnitEvent, ABattleUnitBase*, Unit);

// 1단계에서는 데이터 초기화와 외형·배치만 제공한다. 자동 전투는 후속 단계에서 구현한다.
UCLASS()
class PROJECT_A_API ABattleUnitBase : public AActor
{
    GENERATED_BODY()
public:
    ABattleUnitBase();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
    virtual void BeginPlay() override;
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Components") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Components") TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Components") TObjectPtr<USkeletalMeshComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Components") TObjectPtr<UStaticMeshComponent> MainHand;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Components") TObjectPtr<UStaticMeshComponent> OffHand;
    // 선택용 충돌이며 이동이나 공격 판정용 캡슐이 아니다. Actor 원점은 바닥이다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Components") TObjectPtr<UCapsuleComponent> SelectionCollision;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit|Setup") FDataTableRowHandle UnitData;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetTeamType, Category="Unit|Setup") EBattleTeam TeamType = EBattleTeam::Player;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit|Setup", meta=(ClampMin="1", ClampMax="3")) int32 StarLevel = 1;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|Data") FName UnitID;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|Data") bool bInitialized = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|Data") FUnitDefinition Definition;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|Stats") FUnitStats FinalStats;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|Stats") float CurrentHP = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|State") EUnitPlacement Placement = EUnitPlacement::Unplaced;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|State") EUnitActionState ActionState = EUnitActionState::Idle;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|State") bool bIsDead = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|State") bool bInCombat = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|State") bool bIsDeployed = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|State") TObjectPtr<ABoardTile> CurrentTile = nullptr;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|State") FTransform PreparationTransform;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit|Validation") FString LastValidationMessage;

    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnHealthChanged;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnStatsChanged;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnVisualsApplied;

    // 실패하면 현재 유닛을 유지하고 원인을 반환한다. 전투·배치 중 종류 변경은 금지한다.
    UFUNCTION(BlueprintCallable, Category="Unit|Setup")
    bool InitializeUnit(FDataTableRowHandle Data, EBattleTeam Team, int32 Stars, FString& Error);
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Unit|Setup") bool RefreshUnitFromData();
    UFUNCTION(BlueprintCallable, Category="Unit|Setup") void ApplyVisuals();
    UFUNCTION(BlueprintCallable, Category="Unit|Stats") void RecalculateStats();
    UFUNCTION(BlueprintCallable, Category="Unit|Stats") bool SetStarLevel(int32 Stars);
    // 이전 BP의 Set TeamType 노드도 이 검증 함수를 거친다. 배치/전투 중에는 변경하지 않는다.
    UFUNCTION(BlueprintSetter) void SetTeamType(EBattleTeam Team);
    UFUNCTION(BlueprintPure, Category="Unit|State") bool IsAlive() const;
    UFUNCTION(BlueprintCallable, Category="Unit|Board") bool PlaceOnTile(ABoardTile* Tile);
    UFUNCTION(BlueprintCallable, Category="Unit|Board") bool SetWaitingPlacement(FVector Location);
    // 기존 GameMode 연결용 수명주기. 이동·공격·피해는 아직 실행하지 않는다.
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") void StartCombat();
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") void StopCombat();
    UFUNCTION(BlueprintPure, Category="Unit|Visual") float GetSelectionRadius() const;
    UFUNCTION(BlueprintPure, Category="Unit|Visual") float GetHealthBarHeight() const;
private:
    void ClearVisuals();
    void ApplyWeapon(UStaticMeshComponent* Component, const FUnitWeaponVisual& Visual);
};
