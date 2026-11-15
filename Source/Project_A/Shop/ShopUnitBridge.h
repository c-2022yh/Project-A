#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnitPlacement.h"
#include "../UnitTypes.h"
#include "ShopUnitBridge.generated.h"

class AAutoBattleGameMode;
class ABoardManager;
class ABattleUnitBase;
class AShopManager;
class ARosterManager;
class AItemManager;
class AShopTransactionManager;

USTRUCT()
struct FShopUnitLink
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<ABattleUnitBase> Actor;
    UPROPERTY() FRosterUnitChange State;
};

// 이 액터를 배치한 맵에서만 상점 ↔ 실제 유닛 ↔ 보드를 연결한다.
// 첫 단계는 한 판 전투다. 5라운드 BP 진행기의 연동 완료 플래그를 대신 켜지 않는다.
UCLASS(Blueprintable)
class PROJECT_A_API AShopUnitBridge : public AActor, public IUnitPlacement
{
    GENERATED_BODY()
public:
    AShopUnitBridge();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") bool bAutoSetupSingleBattle = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") TSoftClassPtr<ABattleUnitBase> UnitClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") TSoftObjectPtr<UDataTable> UnitDataTable;
    // 상품 ID는 DT 행 이름과 같다. 현재 단계에서는 기사만 기본으로 등록한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") TArray<FName> AvailableUnits = { FName(TEXT("Knight")) };
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") FName EnemyRow = TEXT("Knight");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") int32 EnemyBoardRow = 5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") int32 EnemyBoardColumn = 4;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop", meta=(ClampMin="0")) int32 StartingGold = 20;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop", meta=(ClampMin="1")) int32 DeploymentLimit = 2;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") TSubclassOf<AShopManager> ShopClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") TSubclassOf<ARosterManager> RosterClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit Shop") TSubclassOf<AItemManager> ItemClass;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit Shop") TObjectPtr<AShopTransactionManager> Transactions;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Unit Shop") FString Status;

    // 기존 진행 담당도 이 연결 함수를 사용 가능하다. 경제 상태는 Transactions가 소유한다.
    UFUNCTION(BlueprintCallable, Category="Unit Shop") bool Connect(AAutoBattleGameMode* GameMode, AShopTransactionManager* Manager);
    UFUNCTION(BlueprintPure, Category="Unit Shop") ABattleUnitBase* GetUnitActor(FGuid Id) const;
    UFUNCTION(BlueprintPure, Category="Unit Shop") FGuid GetUnitId(ABattleUnitBase* Actor) const;
    UFUNCTION(BlueprintCallable, Category="Unit Shop") bool StartLinkedCombat();
    UFUNCTION(BlueprintPure, Category="Unit Shop") bool CanStartLinkedCombat() const;
    void InitializeSingleBattle();
    void FinishLinkedCombat(uint8 Result);

    virtual FUnitPosition GetUnitPosition_Implementation(FGuid Id) const override;
    virtual FGuid GetOccupant_Implementation(FUnitPosition Position) const override;
    virtual FUnitPosition FindRewardTile_Implementation(int32 Limit) const override;
    virtual bool GetUnitProfile_Implementation(FGuid Id, FRosterUnitProfile& Profile) const override;
    virtual FGuid GetHoveredUnit_Implementation() const override;
    virtual bool ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<AAutoBattleGameMode> Mode;
    UPROPERTY(Transient) TObjectPtr<ABoardManager> Board;
    UPROPERTY(Transient) TMap<FGuid, FShopUnitLink> Links;
    UPROPERTY(Transient) TArray<TObjectPtr<AActor>> OwnedManagers;
    bool bApplying = false;
    bool Fail(const FString& Message);
    void ClearOwnedUnits();
};
