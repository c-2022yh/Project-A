#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnitTypes.h"
#include "BattleUnitBase.generated.h"

class ABoardTile;
class ABoardManager;
class UCapsuleComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class ABattleUnitBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBattleUnitEvent, ABattleUnitBase*, Unit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBattleUnitActionEvent, ABattleUnitBase*, Unit, int32, ActionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FBattleUnitDamageEvent, ABattleUnitBase*, Unit, const FUnitDamageRequest&, Request, const FUnitDamageResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FBattleUnitHealEvent, ABattleUnitBase*, Unit, ABattleUnitBase*, Healer, float, Amount);

/* 에디터 연결 순서
 * 1. DT_Units(UnitDefinition)에 외형/기본 능력치/Skills를 입력하고 InitializeUnit으로 생성한다.
 * 2. 전투 관리자는 SetCombatRoster로 양 팀 명단을 전달한 뒤 StartCombat을 호출한다.
 * 3. 공격 Notify는 시작 이벤트에서 받은 ActionId로 CommitAttackHit/FinishAction을 호출한다.
 *    오래된 Notify를 현재 ID로 바꿔 호출하지 않는다. 몽타주가 없으면 타이머 방식을 선택한다.
 * 4. 생성된 보드 타일에 배치하면 BoardManager를 자동 연결하고 C++가 경로/예약을 처리한다.
 *    전투 명단을 따로 전달하지 않았다면 시작 시 보드의 배치 유닛 목록을 사용한다.
 *    BP의 RequestCombatStep 재정의는 기본적으로 필요 없다. 예약은 반드시 보드 API를 사용한다.
 * 5. 투사체 외형은 ProjectileUpdated/ProjectileRemoved에서 표시만 한다. BP는 피해를 적용하지 않는다.
 * 6. 전투 종료는 전원 StopCombat → 전원 ResetForRound. 승패/보상/연장전 진입 시간은 진행 담당이다.
 *
 * 임시 정책: 동률은 전달된 명단 순서, 이동 중 기절/취소는 출발 타일 복귀,
 * 음수 저항은 기본 0 처리, 보호막은 기본 만료가 빠른 순서, 소수점은 float 유지.
 * 나머지 미정 값은 CombatPolicy에서 조절한다. 보스/기물 고유 수치는 DT로 설정한다.
 */
UCLASS()
class PROJECT_A_API ABattleUnitBase : public AActor
{
    GENERATED_BODY()
public:
    ABattleUnitBase();
    virtual void Tick(float DeltaSeconds) override;
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
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Components") TObjectPtr<UCapsuleComponent> SelectionCollision;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Board") TObjectPtr<ABoardManager> BoardManager = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit|Setup") FDataTableRowHandle UnitData;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetTeamType, Category="Unit|Setup") EBattleTeam TeamType = EBattleTeam::Player;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit|Setup", meta=(ClampMin="1", ClampMax="2")) int32 StarLevel = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Unit|Policy") FUnitCombatPolicy CombatPolicy;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Data") FName UnitID;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, DuplicateTransient, Category="Unit|Data") bool bInitialized = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Data") FUnitDefinition Definition;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Stats") FUnitStats FinalStats;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Stats") float CurrentHP = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Stats") float CurrentMana = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Stats") float CurrentShield = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") EUnitPlacement Placement = EUnitPlacement::Unplaced;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") EUnitActionState ActionState = EUnitActionState::Idle;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") bool bIsDead = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") bool bInCombat = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") bool bIsDeployed = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") TObjectPtr<ABoardTile> CurrentTile = nullptr;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") TObjectPtr<ABattleUnitBase> CurrentTarget = nullptr;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") FTransform PreparationTransform;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") int32 ActiveActionId = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") int32 BasicAttackCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|State") int32 BasicHitCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Validation") FString LastValidationMessage;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Effects") TArray<FUnitTimedModifier> ActiveModifiers;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Effects") TArray<FUnitShield> Shields;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Unit|Growth") FUnitStatModifier PermanentGrowth;

    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnHealthChanged;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnManaChanged;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnShieldChanged;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnStatsChanged;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnVisualsApplied;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnStateChanged;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitEvent OnUnitDied;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitActionEvent OnAttackStarted;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitActionEvent OnSkillStarted;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitDamageEvent OnDamageReceived;
    UPROPERTY(BlueprintAssignable, Category="Unit|Events") FBattleUnitHealEvent OnHealed;

    UFUNCTION(BlueprintCallable, Category="Unit|Setup") bool InitializeUnit(FDataTableRowHandle Data, EBattleTeam Team, int32 Stars, FString& Error);
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Unit|Setup") bool RefreshUnitFromData();
    UFUNCTION(BlueprintCallable, Category="Unit|Setup") void ApplyVisuals();
    UFUNCTION(BlueprintCallable, Category="Unit|Stats") void RecalculateStats();
    UFUNCTION(BlueprintCallable, Category="Unit|Stats") bool SetStarLevel(int32 Stars);
    UFUNCTION(BlueprintSetter) void SetTeamType(EBattleTeam Team);
    // 아이템/강화 소유권은 외부가 관리한다. 같은 SourceId는 교체하고 서로 다른 값은 함께 적용한다.
    UFUNCTION(BlueprintCallable, Category="Unit|Stats") bool ApplyUpgrade(FName SourceId, const FUnitStatModifier& Modifier);
    UFUNCTION(BlueprintCallable, Category="Unit|Stats") bool RemoveUpgrade(FName SourceId);
    UFUNCTION(BlueprintCallable, Category="Unit|Growth") float AddPermanentGrowth(FName SourceId, EUnitGrowthStat Stat, float Amount, float LimitPerCombat);
    UFUNCTION(BlueprintPure, Category="Unit|State") bool IsAlive() const;
    UFUNCTION(BlueprintPure, Category="Unit|State") bool IsStunned() const;
    UFUNCTION(BlueprintPure, Category="Unit|Mana") bool UsesMana() const;
    UFUNCTION(BlueprintPure, Category="Unit|Board") static int32 HexDistance(const ABoardTile* A, const ABoardTile* B);
    UFUNCTION(BlueprintCallable, Category="Unit|Board") bool PlaceOnTile(ABoardTile* Tile);
    UFUNCTION(BlueprintCallable, Category="Unit|Board") bool SetWaitingPlacement(FVector Location);
    UFUNCTION(BlueprintCallable, Category="Unit|Board") bool SetPreparationPlacement(FTransform Transform, ABoardTile* Tile, EUnitPlacement NewPlacement);
    UFUNCTION(BlueprintCallable, Category="Unit|Board") bool SetBoardManager(ABoardManager* Board);
    UFUNCTION(BlueprintCallable, Category="Unit|Board") bool CancelBoardMove(int32 MoveId);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") void SetCombatRoster(const TArray<ABattleUnitBase*>& Units);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") void StartCombat();
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") void StopCombat();
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") bool ResetForRound();
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") void SetOvertimeBonuses(float AttackSpeedBonus, float DamageBonus);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") bool TryBasicAttack();
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") bool CommitAttackHit(int32 ActionId);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") bool TryCastSkill();
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") bool CommitSkillEffect(int32 ActionId);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") bool FinishAction(int32 ActionId);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") FUnitDamageResult ReceiveDamage(const FUnitDamageRequest& Request);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") float ReceiveHealing(float Amount, ABattleUnitBase* Healer);
    UFUNCTION(BlueprintCallable, Category="Unit|Combat") float GainMana(float Amount);
    UFUNCTION(BlueprintCallable, Category="Unit|Effects") bool AddShield(FName EffectId, float Amount, float Duration);
    UFUNCTION(BlueprintCallable, Category="Unit|Effects") bool AddModifier(FName EffectId, const FUnitStatModifier& Modifier, float Duration);
    UFUNCTION(BlueprintCallable, Category="Unit|Effects") bool RemoveEffect(FName EffectId);
    UFUNCTION(BlueprintPure, Category="Unit|Visual") float GetSelectionRadius() const;
    UFUNCTION(BlueprintPure, Category="Unit|Visual") float GetHealthBarHeight() const;

    // ReservedStep을 반환할 때에는 보드에서 이미 해당 타일을 예약한 상태여야 한다.
    UFUNCTION(BlueprintNativeEvent, Category="Unit|Board") EUnitPathResult RequestCombatStep(ABattleUnitBase* Target, int32 MoveId, ABoardTile*& NextTile);
    UFUNCTION(BlueprintNativeEvent, Category="Unit|Board") void MovementReservationReleased(ABoardTile* Tile, int32 MoveId, bool bCompleted);
    virtual EUnitPathResult RequestCombatStep_Implementation(ABattleUnitBase* Target, int32 MoveId, ABoardTile*& NextTile);
    virtual void MovementReservationReleased_Implementation(ABoardTile* Tile, int32 MoveId, bool bCompleted);
    UFUNCTION(BlueprintImplementableEvent, Category="Unit|Visual") void ProjectileUpdated(const FUnitProjectileView& Projectile);
    UFUNCTION(BlueprintImplementableEvent, Category="Unit|Visual") void ProjectileRemoved(int32 ProjectileId, bool bHit);
private:
    struct FSkillRuntime { int32 Count = 0; float Cooldown = 0.f; };
    struct FPendingProjectile
    {
        int32 Id = 0;
        int32 TargetGeneration = 0;
        FVector Position = FVector::ZeroVector;
        TWeakObjectPtr<ABattleUnitBase> Target;
        float Speed = 0.f;
        float Lifetime = 0.f;
        bool bBasic = false;
        FUnitDamageRequest Damage;
        FUnitSkillDefinition Skill;
        TArray<float> EffectValues;
    };
    struct FAttackEnhancement { FName Id; float Multiplier = 1.f; float Flat = 0.f; float Remaining = 0.f; };
    UPROPERTY(Transient) TArray<TObjectPtr<ABattleUnitBase>> CombatRoster;
    UPROPERTY(Transient) TObjectPtr<ABoardTile> PreparationTile = nullptr;
    UPROPERTY(Transient) TObjectPtr<ABoardTile> ReservedTile = nullptr;
    UPROPERTY(Transient) TMap<FName, FUnitStatModifier> Upgrades;
    TArray<FSkillRuntime> SkillRuntime;
    TArray<FPendingProjectile> Projectiles;
    TArray<FAttackEnhancement> AttackEnhancements;
    TSet<FGuid> AppliedDamage;
    TSet<int32> UnreachableTargets;
    TMap<FName, float> GrowthThisCombat;
    TWeakObjectPtr<ABattleUnitBase> ActionTarget;
    EUnitPlacement PreparationPlacement = EUnitPlacement::Unplaced;
    FUnitDamageRequest TriggerContext;
    FUnitDamageRequest ActiveAttackDamage;
    int32 ActionSequence = 0;
    int32 CombatGeneration = 0;
    int32 ActiveSkillIndex = INDEX_NONE;
    int32 ShieldSequence = 0;
    float AttackCooldown = 0.f;
    float ActionElapsed = 0.f;
    float ActionDuration = 0.f;
    float RetargetTimer = 0.f;
    float BlockedSeconds = 0.f;
    float PreCastMana = 0.f;
    float DamageIncrease = 0.f;
    float DamageRemainingRatio = 1.f;
    float AttackSlow = 0.f;
    float OvertimeAttackSpeed = 0.f;
    float OvertimeDamageMultiplier = 1.f;
    bool bActionCommitted = false;
    bool bManaBlocked = false;
    bool bResolvingDamage = false;
    bool bCombatStartedForRound = false;
    bool bCombatStartPending = false;
    bool bResetting = false;
    bool bReleasingMove = false;
    FVector MoveStart = FVector::ZeroVector;

    void ClearVisuals();
    void ApplyWeapon(UStaticMeshComponent* Component, const FUnitWeaponVisual& Visual);
    void ChangeAction(EUnitActionState State);
    void CancelAction(bool bRefundMana = false);
    void ReleaseMove(bool bCompleted);
    void Die();
    void RefreshShieldTotal();
    void TickEffects(float Delta);
    void TickProjectiles(float Delta);
    void TickAction(float Delta);
    void Think(float Delta);
    void FindTarget();
    bool IsEnemyTarget(const ABattleUnitBase* Target) const;
    bool IsInRange(const ABattleUnitBase* Target, int32 Range) const;
    ABattleUnitBase* SelectSkillTarget(const FUnitSkillDefinition& Skill) const;
    FUnitDamageRequest MakeDamage(float Raw, EUnitDamageType Type, EUnitDamageSource Source, FName SkillId, int32 ActionId, bool bRollCritical) const;
    void ReportBasicHit(ABattleUnitBase* Target, const FUnitDamageRequest& Request, const FUnitDamageResult& Result);
    void DispatchTrigger(EUnitTrigger Trigger, const FUnitDamageRequest& Context);
    void ExecuteSkill(const FUnitSkillDefinition& Skill, ABattleUnitBase* Target, int32 ActionId);
    void ApplySkillEffects(const FUnitSkillDefinition& Skill, ABattleUnitBase* Target, const TArray<float>& Values, const FUnitDamageRequest& Context);
    TArray<float> CalculateEffectValues(const FUnitSkillDefinition& Skill) const;
    void LaunchProjectile(FPendingProjectile Projectile);
};
