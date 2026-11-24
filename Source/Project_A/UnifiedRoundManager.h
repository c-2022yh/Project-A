#pragma once

#include "CoreMinimal.h"
#include "AutoBattleGameMode.h"
#include "UnifiedRoundManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRoundDeploymentRequest, int32, Limit, int32, Round, FGuid, RequestId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoundPreparationRequest, int32, Round, FGuid, RequestId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoundCombatStart, FGuid, Encounter, int32, Round);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoundCombatStop, FGuid, Encounter, bool, TimedOut);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRoundFinalPolicy, FGuid, Encounter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoundPhaseChange, uint8, Previous, uint8, Current);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoundAvailability, bool, Allowed, FText, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRoundConfigurationError, FText, Message);

// 기존 GameMode 요청/완료 계약을 사용하며, 라운드 설정은 통합 DT만 조회한다.
UCLASS()
class PROJECT_A_API AUnifiedRoundManager : public AActor
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UDataTable> RoundsTable;
    UPROPERTY() uint8 CurrentPhase = 0;
    UPROPERTY() int32 CurrentRoundNumber = 1;
    UPROPERTY() TArray<FIntegrationRoundParticipant> RoundParticipantSnapshots;
    UPROPERTY() bool bAwaitingDeployment = false;
    UPROPERTY() FGuid DeploymentRequestId;
    UPROPERTY() bool bAwaitingPreparation = false;
    UPROPERTY() FGuid PreparationRequestId;
    UPROPERTY() bool bAwaitingReward = false;
    UPROPERTY() FGuid RewardRequestId;

    UPROPERTY() FRoundDeploymentRequest OnDeploymentLimitChangeRequested;
    UPROPERTY() FRoundPreparationRequest OnRoundPreparationRequested;
    UPROPERTY() FRoundCombatStart OnCombatStartRequested;
    UPROPERTY() FRoundCombatStop OnCombatStopRequested;
    UPROPERTY() FRoundCombatStop OnCombatResolutionRequired;
    UPROPERTY() FRoundFinalPolicy OnFinalRoundPolicyRequired;
    UPROPERTY() FRoundPreparationRequest OnRewardRequested;
    UPROPERTY() FRoundPhaseChange OnPhaseChanged;
    UPROPERTY() FRoundAvailability OnStartAvailabilityChanged;
    UPROPERTY() FRoundConfigurationError OnConfigurationError;

    UFUNCTION() void ConfigureIntegration(bool TimerOwnershipConfirmed, bool PhaseSyncConfirmed, bool DeploymentAdapterReady,
        bool CombatAdapterReady, bool PreparationAdapterReady, bool RewardAdapterReady);
    UFUNCTION() void ConfigureTemporaryRewardBypass(bool bEnabled) {}
    UFUNCTION() void InitializeFlow();
    UFUNCTION() void UpdateInteractionState(int32 AllyCount, bool UnitDragging, bool ItemDragging);
    UFUNCTION() void NotifyDeploymentLimitApplied(FGuid RequestId, bool bSuccess);
    UFUNCTION() void NotifyRoundPreparationCompleted(FGuid RequestId, bool bSuccess);
    UFUNCTION() void RequestCombatStart();
    UFUNCTION() void NotifyCombatFinished(FGuid CompletedEncounterId);
    UFUNCTION() void NotifyCombatResolutionCompleted(FGuid CompletedEncounterId);
    UFUNCTION() void ConfirmProgressionToReward(FGuid CompletedEncounterId);
    UFUNCTION() void ResolveFinalRoundPolicy(FGuid CompletedEncounterId, bool bGrantFinalReward);
    UFUNCTION() void NotifyRewardSelectionCompleted(FGuid CompletedRewardRequestId);
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    const FAutoBattleRoundDefinition* Definition(int32 Round) const;
    void SetPhase(uint8 Phase);
    void RequestPreparation(int32 Round);
    void EnterReward();
    void StopCombat(bool TimedOut);
    void RefreshAvailability();
    FTimerHandle CombatTimer;
    FGuid EncounterId;
    int32 PendingRound = 1;
    int32 DeployedCount = 0;
    bool bDragging = false;
    bool bReady = false;
    bool bResolved = false;
    bool bFinalRequested = false;
};
