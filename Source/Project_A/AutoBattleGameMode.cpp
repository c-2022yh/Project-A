#include "AutoBattleGameMode.h"

AAutoBattleGameMode::AAutoBattleGameMode()
{
}

void AAutoBattleGameMode::BeginPlay()
{
    Super::BeginPlay();

    StartPreparationPhase();
}

void AAutoBattleGameMode::StartPreparationPhase()
{
    CurrentPhase = EGamePhase::Preparation;
}

void AAutoBattleGameMode::StartCombatPhase()
{
    CurrentPhase = EGamePhase::Combat;
}

void AAutoBattleGameMode::HandleRoundClear()
{
    CurrentPhase = EGamePhase::RoundClear;

    if (CurrentRound >= MaxRound)
    {
        HandleVictory();
        return;
    }

    AdvanceRound();
    StartPreparationPhase();
}

void AAutoBattleGameMode::HandleGameOver()
{
    CurrentPhase = EGamePhase::GameOver;
}

void AAutoBattleGameMode::HandleVictory()
{
    CurrentPhase = EGamePhase::Victory;
}

void AAutoBattleGameMode::AdvanceRound()
{
    CurrentRound++;

    //현재 기획 기준:
    //Round 1 = 2명
    //Round 2 = 3명
    //...
    //Round 5 = 6명
    MaxBoardUnits = FMath::Clamp(CurrentRound + 1, 2, 6);
}