#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../UnitTypes.h"
#include "UnitCombatTestRunner.generated.h"

class ABoardManager;
class ABattleUnitBase;
class UDataTable;

UENUM(BlueprintType)
enum class EUnitCombatTestState : uint8 { Idle, WaitingForBoard, Preparing, Running, Finished, Failed };

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitCombatTestSide
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RowName = TEXT("Knight");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="2")) int32 Stars = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 Row = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 Column = 0;
};

/* 전용 테스트 액터. 기존 게임의 생성/준비/보상/라운드 흐름을 대신하는 제품 코드가 아니다.
 * TestMap의 GameMode Override를 기본 GameModeBase로 지정한 뒤 이 액터 한 개만 배치한다.
 * 보드 한 개 + 이 액터면 충분하다. 유닛을 미리 배치하거나 Level BP 노드를 만들 필요가 없다.
 * 생성/초기화/배치/전투는 기존 공개 API만 호출하며 원본 DT/BP/맵을 수정하지 않는다.
 */
UCLASS()
class PROJECT_A_API AUnitCombatTestRunner : public AActor
{
    GENERATED_BODY()
public:
    AUnitCombatTestRunner();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
    virtual void BeginPlay() override;
public:
    // None은 안전 검사를 해제하는 값이 아니다. 반드시 전용 테스트 맵 이름을 지정한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Setup") FName AllowedMapName = TEXT("TestMap");
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Combat Test|Setup") TObjectPtr<ABoardManager> Board = nullptr;
    // 이 프로젝트에서 이미 만든 에셋 경로를 기본값으로 사용한다. 실행할 때만 로드한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Setup") TSoftClassPtr<ABattleUnitBase> UnitClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Setup") TSoftObjectPtr<UDataTable> UnitDataTable;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Setup") FUnitCombatTestSide Ally;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Setup") FUnitCombatTestSide Enemy;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Setup") bool bAutoStartOnPlay = true;
    // true: 런타임 DT 사본에서만 평타 Notify를 끄고 타이머 판정을 사용한다.
    // 실제 몽타주/Notify 검증 단계에서는 false로 바꿔 원본 설정 그대로 시험한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Setup") bool bUseTimerAttackForTest = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Timing", meta=(ClampMin="0")) float StartDelay = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Timing", meta=(ClampMin="0.1")) float BoardWaitTimeout = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Timing", meta=(ClampMin="1")) float CombatTimeout = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat Test|Display") bool bShowScreenMessages = true;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Combat Test|Status") EUnitCombatTestState TestState = EUnitCombatTestState::Idle;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Combat Test|Status") FString Status;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Combat Test|Status") TObjectPtr<ABattleUnitBase> AllyUnit = nullptr;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Combat Test|Status") TObjectPtr<ABattleUnitBase> EnemyUnit = nullptr;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Combat Test|Status") float ElapsedCombatTime = 0.f;
    UFUNCTION(BlueprintCallable, Category="Combat Test") void StartTest();
    UFUNCTION(BlueprintCallable, Category="Combat Test") void StopTest();
    UFUNCTION(BlueprintCallable, Category="Combat Test") void RestartTest();
private:
    UPROPERTY(Transient) TObjectPtr<UDataTable> RuntimeData = nullptr;
    TArray<TWeakObjectPtr<ABattleUnitBase>> SpawnedUnits;
    FTimerHandle SetupTimer;
    FTimerHandle StartTimer;
    FTimerHandle ResultTimer;
    double WaitStartedAt = 0.0;
    bool bCleaningUp = false;
    bool bResultQueued = false;
    bool ValidateIsolation(FString& Error) const;
    bool ResolveBoard(FString& Error);
    void TrySetup();
    void BeginCombat();
    void FinishCombat(const FString& Message);
    void Fail(const FString& Message);
    void CleanupOwnedUnits();
    void ClearTimers();
    void SetStatus(EUnitCombatTestState State, const FString& Message);
    UFUNCTION() void HandleUnitDied(ABattleUnitBase* Unit);
    void ProcessResult();
};
