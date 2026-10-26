#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BattleUnitBase.h"
#include "AutoBattleGameMode.generated.h"

class ABoardManager;
UENUM(BlueprintType)
enum class EGamePhase : uint8
{
    Preparation = 0 UMETA(DisplayName="Preparation"),
    Combat = 1 UMETA(DisplayName="Combat"),
    RoundClear = 2 UMETA(DisplayName="Round Clear"),
    GameOver = 3 UMETA(DisplayName="Game Over"),
    Victory = 4 UMETA(DisplayName="Victory")
};

USTRUCT(BlueprintType)
struct FUnitSpawnInfo
{
    GENERATED_BODY()
    // BP의 Spawn Actor에서 사용할 클래스. 생성하려면 반드시 지정해야 한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
    TSubclassOf<ABattleUnitBase> UnitClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
    EBattleTeam Team = EBattleTeam::Player;
    // 적은 적 진영 타일에서 시작해야 한다. 아군은 대기 구역에서 시작할 수 있다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
    bool bStartOnBoard = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0", EditCondition="bStartOnBoard"))
    int32 Row = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0", EditCondition="bStartOnBoard"))
    int32 Column = 0;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGamePhaseChangedEvent, EGamePhase, Phase);

UCLASS()
class PROJECT_A_API AAutoBattleGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AAutoBattleGameMode();
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    EGamePhase CurrentPhase = EGamePhase::Preparation;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Round")
    int32 CurrentRound = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round", meta=(ClampMin="1"))
    int32 MaxRound = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Round", meta=(ClampMin="1"))
    int32 MaxBoardUnits = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Setup")
    TArray<FUnitSpawnInfo> UnitRoster;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Setup", meta=(ClampMin="0"))
    float PreparationDuration = 30.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Setup", meta=(ClampMin="1"))
    float CombatTimeLimit = 120.0f;
    // 자동 계산한 유닛 대기 위치에 추가로 적용할 이동량이다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Setup")
    FVector WaitingAreaOffset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    float RemainingPreparationTime = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    float ElapsedCombatTime = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    bool bEncounterReady = false;
    // 그래프에서 준비 단계를 시작한 뒤에만 유닛을 배치할 수 있다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    bool bPreparationActive = false;
    // 전투 결과가 감지되면 전투를 멈추고 BP 그래프의 결과 처리를 기다린다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    bool bBattleResultPending = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    FString StatusMessage;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    TObjectPtr<ABoardManager> BoardManager;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    TArray<ABattleUnitBase*> BattleUnits;
    UPROPERTY(BlueprintAssignable, Category="Game")
    FGamePhaseChangedEvent OnPhaseChanged;


    // 유닛 생성 반복은 BP에서 구성하고, 아래 함수들은 등록과 배치 등 공통 상태를 관리한다.
    UFUNCTION(BlueprintCallable, Category="Game|Unit Setup") bool BeginEncounterSetup();
    UFUNCTION(BlueprintCallable, Category="Game|Unit Setup") bool RegisterBattleUnit(ABattleUnitBase* Unit);
    UFUNCTION(BlueprintCallable, Category="Game|Unit Setup")
    bool MoveUnitToWaitingArea(ABattleUnitBase* Unit, int32 WaitingIndex, int32 WaitingCount);
    UFUNCTION(BlueprintCallable, Category="Game|Unit Setup") bool CompleteEncounterSetup();
    // 준비 단계에서 등록된 유닛을 정리한다. 자동 재생성이나 Board Ready 이벤트 호출은 하지 않는다.
    UFUNCTION(BlueprintCallable, Category="Game|Unit Setup") void CancelEncounterSetup();

    UFUNCTION(BlueprintCallable, Category="Game|Flow") void RequestCombatStart();
    UFUNCTION(BlueprintCallable, Category="Game|Flow") void RequestBattleRestart();
    UFUNCTION(BlueprintCallable, Category="Game|Flow") void RestartPreparationCountdown();
    UFUNCTION(BlueprintCallable, Category="Game") void StartPreparationPhase();
    UFUNCTION(BlueprintCallable, Category="Game") void StartCombatPhase();
    UFUNCTION(BlueprintCallable, Category="Game") void HandleRoundClear();
    UFUNCTION(BlueprintCallable, Category="Game") void HandleGameOver();
    UFUNCTION(BlueprintCallable, Category="Game") void HandleVictory();
    UFUNCTION(BlueprintCallable, Category="Round") void AdvanceRound();
    UFUNCTION(BlueprintCallable, Category="Game") void RestartBattle();
    UFUNCTION(BlueprintPure, Category="Game") int32 GetDeployedPlayerCount() const;
    UFUNCTION(BlueprintPure, Category="Game") int32 GetAliveTeamCount(EBattleTeam Team) const;
protected:
    // GameMode BP에서 아래 이벤트를 구현하여 단계별 게임 진행을 연결한다.
    // C++ 기본 진행은 없다. BP에서 필요한 이벤트를 연결해야 다음 단계로 진행한다.
    UFUNCTION(BlueprintImplementableEvent, Category="Game|Flow", meta=(DisplayName="Board Ready"))
    void ReceiveBoardReady();

    UFUNCTION(BlueprintImplementableEvent, Category="Game|Flow", meta=(DisplayName="Preparation Expired"))
    void ReceivePreparationExpired();

    UFUNCTION(BlueprintImplementableEvent, Category="Game|Flow", meta=(DisplayName="Combat Start Requested"))
    void ReceiveCombatStartRequested();

    UFUNCTION(BlueprintImplementableEvent, Category="Game|Flow", meta=(DisplayName="Battle Result Detected"))
    void ReceiveBattleResultDetected(EGamePhase Result);

    UFUNCTION(BlueprintImplementableEvent, Category="Game|Flow", meta=(DisplayName="Restart Requested"))
    void ReceiveRestartRequested();

    // 단계 변경에 맞춰 위젯 생성과 표시 등을 연결하는 선택적 이벤트다. 여기서 게임 단계를 전환하지 않는다.
    UFUNCTION(BlueprintImplementableEvent, Category="Game|Flow", meta=(DisplayName="Game Phase Changed"))
    void ReceiveGamePhaseChanged(EGamePhase Phase);

private:
    void InitializeEncounter();
    void ReportBattleResult(EGamePhase Result);
    EGamePhase PendingBattleResult = EGamePhase::GameOver;
    bool bPreparationExpiryReported = false;
    bool bEncounterSetupOpen = false;
    void ClearEncounter();
    void ChangePhase(EGamePhase Phase);
};
