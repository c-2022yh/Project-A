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
    // Empty uses the native cube test unit.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
    TSubclassOf<ABattleUnitBase> UnitClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
    EBattleTeam Team = EBattleTeam::Player;
    // Enemies must start on their own tiles. Players may start in the waiting area.
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
    // Added to the automatic waiting-line position outside the near board edge.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battle Setup")
    FVector WaitingAreaOffset = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    float RemainingPreparationTime = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    float ElapsedCombatTime = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    bool bEncounterReady = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    FString StatusMessage;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    TObjectPtr<ABoardManager> BoardManager;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Game")
    TArray<ABattleUnitBase*> BattleUnits;
    UPROPERTY(BlueprintAssignable, Category="Game")
    FGamePhaseChangedEvent OnPhaseChanged;

    UFUNCTION(BlueprintCallable, Category="Game") void StartPreparationPhase();
    UFUNCTION(BlueprintCallable, Category="Game") void StartCombatPhase();
    UFUNCTION(BlueprintCallable, Category="Game") void HandleRoundClear();
    UFUNCTION(BlueprintCallable, Category="Game") void HandleGameOver();
    UFUNCTION(BlueprintCallable, Category="Game") void HandleVictory();
    UFUNCTION(BlueprintCallable, Category="Round") void AdvanceRound();
    UFUNCTION(BlueprintCallable, Category="Game") void RestartBattle();
    UFUNCTION(BlueprintPure, Category="Game") int32 GetDeployedPlayerCount() const;
    UFUNCTION(BlueprintPure, Category="Game") int32 GetAliveTeamCount(EBattleTeam Team) const;
private:
    void InitializeEncounter();
    void ClearEncounter();
    void ChangePhase(EGamePhase Phase);
};
