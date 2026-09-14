#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AutoBattleGameMode.generated.h"

UENUM(BlueprintType)
enum class EGamePhase : uint8
{
    Preparation UMETA(DisplayName = "Preparation"),
    Combat      UMETA(DisplayName = "Combat"),
    RoundClear  UMETA(DisplayName = "Round Clear"),
    GameOver    UMETA(DisplayName = "Game Over"),
    Victory     UMETA(DisplayName = "Victory")
};

UCLASS()
class PROJECT_A_API AAutoBattleGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AAutoBattleGameMode();

protected:
    virtual void BeginPlay() override;

public:
    //현재 게임 페이즈
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Game")
    EGamePhase CurrentPhase = EGamePhase::Preparation;

    //현재 라운드
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Round")
    int32 CurrentRound = 1;

    //총 라운드 수
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Round")
    int32 MaxRound = 5;

    //현재 최대 출전 가능 유닛 수
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Round")
    int32 MaxBoardUnits = 2;

    UFUNCTION(BlueprintCallable, Category = "Game")
    void StartPreparationPhase();

    UFUNCTION(BlueprintCallable, Category = "Game")
    void StartCombatPhase();

    UFUNCTION(BlueprintCallable, Category = "Game")
    void HandleRoundClear();

    UFUNCTION(BlueprintCallable, Category = "Game")
    void HandleGameOver();

    UFUNCTION(BlueprintCallable, Category = "Game")
    void HandleVictory();

    UFUNCTION(BlueprintCallable, Category = "Round")
    void AdvanceRound();
};