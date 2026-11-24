#include "UnifiedRoundManager.h"
#include "TimerManager.h"

const FAutoBattleRoundDefinition* AUnifiedRoundManager::Definition(int32 Round) const
{
    return RoundsTable && RoundsTable->GetRowStruct() == FAutoBattleRoundDefinition::StaticStruct()
        ? RoundsTable->FindRow<FAutoBattleRoundDefinition>(FName(*FString::Printf(TEXT("R%d"), Round)), TEXT("Round flow"), false) : nullptr;
}

void AUnifiedRoundManager::ConfigureIntegration(bool TimerOwnershipConfirmed, bool PhaseSyncConfirmed,
    bool DeploymentAdapterReady, bool CombatAdapterReady, bool PreparationAdapterReady, bool RewardAdapterReady)
{
    bReady = TimerOwnershipConfirmed && PhaseSyncConfirmed && DeploymentAdapterReady
        && CombatAdapterReady && PreparationAdapterReady && RewardAdapterReady;
}

void AUnifiedRoundManager::InitializeFlow()
{
    if (!bReady || CurrentPhase != 0 || bAwaitingDeployment || bAwaitingPreparation) return;
    for (int32 Round = 1; Round <= 5; ++Round)
    {
        const auto* Row = Definition(Round);
        if (!Row || Row->MaxDeployedUnits < 1 || !FMath::IsFinite(Row->CombatDurationSeconds)
            || Row->CombatDurationSeconds <= 0.f || Row->ClearGold < 0 || Row->Enemies.IsEmpty())
        {
            OnConfigurationError.Broadcast(FText::FromString(FString::Printf(TEXT("Invalid unified round row R%d."), Round)));
            return;
        }
    }
    RequestPreparation(1);
}

void AUnifiedRoundManager::SetPhase(uint8 Phase)
{
    const uint8 Previous = CurrentPhase;
    CurrentPhase = Phase;
    OnPhaseChanged.Broadcast(Previous, Phase);
}

void AUnifiedRoundManager::RequestPreparation(int32 Round)
{
    PendingRound = Round;
    bAwaitingDeployment = true;
    DeploymentRequestId = FGuid::NewGuid();
    OnDeploymentLimitChangeRequested.Broadcast(Definition(Round)->MaxDeployedUnits, Round, DeploymentRequestId);
}

void AUnifiedRoundManager::NotifyDeploymentLimitApplied(FGuid RequestId, bool bSuccess)
{
    if (!bAwaitingDeployment || DeploymentRequestId != RequestId || !bSuccess) return;
    bAwaitingDeployment = false;
    bAwaitingPreparation = true;
    PreparationRequestId = FGuid::NewGuid();
    OnRoundPreparationRequested.Broadcast(PendingRound, PreparationRequestId);
}

void AUnifiedRoundManager::NotifyRoundPreparationCompleted(FGuid RequestId, bool bSuccess)
{
    if (!bAwaitingPreparation || PreparationRequestId != RequestId || !bSuccess) return;
    bAwaitingPreparation = false;
    CurrentRoundNumber = PendingRound;
    SetPhase(1);
    RefreshAvailability();
}

void AUnifiedRoundManager::UpdateInteractionState(int32 AllyCount, bool UnitDragging, bool ItemDragging)
{
    DeployedCount = AllyCount;
    bDragging = UnitDragging || ItemDragging;
    RefreshAvailability();
}

void AUnifiedRoundManager::RefreshAvailability()
{
    const auto* Row = Definition(CurrentRoundNumber);
    const bool Allowed = CurrentPhase == 1 && !bAwaitingPreparation && !bAwaitingDeployment
        && Row && DeployedCount >= 1 && DeployedCount <= Row->MaxDeployedUnits && !bDragging;
    OnStartAvailabilityChanged.Broadcast(Allowed, Allowed ? FText::GetEmpty()
        : NSLOCTEXT("RoundFlow", "DeployUnits", "출전 한도에 맞게 기물을 배치하세요."));
}

void AUnifiedRoundManager::RequestCombatStart()
{
    const auto* Row = Definition(CurrentRoundNumber);
    if (CurrentPhase != 1 || bAwaitingDeployment || bAwaitingPreparation || !Row
        || DeployedCount < 1 || DeployedCount > Row->MaxDeployedUnits || bDragging) return;
    EncounterId = FGuid::NewGuid();
    bResolved = false;
    SetPhase(2);
    GetWorldTimerManager().SetTimer(CombatTimer, FTimerDelegate::CreateUObject(
        this, &AUnifiedRoundManager::StopCombat, true), Row->CombatDurationSeconds, false);
    OnCombatStartRequested.Broadcast(EncounterId, CurrentRoundNumber);
}

void AUnifiedRoundManager::StopCombat(bool TimedOut)
{
    if (CurrentPhase != 2) return;
    GetWorldTimerManager().ClearTimer(CombatTimer);
    SetPhase(3);
    OnCombatStopRequested.Broadcast(EncounterId, TimedOut);
    OnCombatResolutionRequired.Broadcast(EncounterId, TimedOut);
}

void AUnifiedRoundManager::NotifyCombatFinished(FGuid CompletedEncounterId)
{
    if (CompletedEncounterId == EncounterId) StopCombat(false);
}

void AUnifiedRoundManager::NotifyCombatResolutionCompleted(FGuid CompletedEncounterId)
{
    if (CurrentPhase == 3 && CompletedEncounterId == EncounterId) bResolved = true;
}

void AUnifiedRoundManager::ConfirmProgressionToReward(FGuid CompletedEncounterId)
{
    if (CurrentPhase != 3 || CompletedEncounterId != EncounterId || !bResolved || bFinalRequested) return;
    bResolved = false;
    if (CurrentRoundNumber == 5)
    {
        bFinalRequested = true;
        OnFinalRoundPolicyRequired.Broadcast(EncounterId);
        return;
    }
    EnterReward();
}

void AUnifiedRoundManager::ResolveFinalRoundPolicy(FGuid CompletedEncounterId, bool bGrantFinalReward)
{
    if (CurrentPhase != 3 || !bFinalRequested || CompletedEncounterId != EncounterId) return;
    bFinalRequested = false;
    if (bGrantFinalReward) EnterReward();
    else SetPhase(5);
}

void AUnifiedRoundManager::EnterReward()
{
    bAwaitingReward = true;
    RewardRequestId = FGuid::NewGuid();
    SetPhase(4);
    OnRewardRequested.Broadcast(CurrentRoundNumber, RewardRequestId);
}

void AUnifiedRoundManager::NotifyRewardSelectionCompleted(FGuid CompletedRewardRequestId)
{
    if (CurrentPhase != 4 || !bAwaitingReward || CompletedRewardRequestId != RewardRequestId) return;
    bAwaitingReward = false;
    if (CurrentRoundNumber == 5) SetPhase(5);
    else RequestPreparation(CurrentRoundNumber + 1);
}

void AUnifiedRoundManager::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(CombatTimer);
    Super::EndPlay(Reason);
}
