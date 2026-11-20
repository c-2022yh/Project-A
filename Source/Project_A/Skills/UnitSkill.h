#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "../UnitTypes.h"
#include "UnitSkillProgress.h"
#include "UnitSkill.generated.h"

class ABattleUnitBase;

// BP 기본값은 설정, 생성된 객체의 Counter/Cooldown은 유닛별 실행 상태다.
// 유닛이 마나와 시전을 관리하고 스킬은 대상 선택과 효과 실행을 확장한다.
UCLASS(Abstract, Blueprintable, BlueprintType)
class PROJECT_A_API UUnitSkillBase : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Skill") FUnitSkillDefinition Settings;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Skill|Runtime") TObjectPtr<ABattleUnitBase> OwnerUnit;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Skill|Runtime") int32 Counter = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Skill|Runtime") float Cooldown = 0.f;
    // 기본적으로 패시브가 만든 피해는 다른 패시브를 다시 발동시키지 않는다.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Skill") bool bAllowTriggeredEvents = false;

    virtual UWorld* GetWorld() const override;
    // 개별 스킬의 전용 BP 항목을 공통 시전 설정으로 변환한다. 초기화 때 한 번 호출한다.
    virtual FUnitSkillDefinition BuildSettings() const;
    virtual bool RequiresLivingCastTarget() const { return true; }
    virtual void PrepareCast(ABattleUnitBase* Target, int32 ActionId) {}
    virtual void BeginCast(int32 ActionId) {}
    virtual void ModifyMaxMana(float& MaxMana) const {}
    virtual void CustomizeEffectValues(TArray<float>& Values) const {}
    // 기본 공격 설정은 공격 시작에 한 번만 읽고, 발사 변경은 유효한 발사 직전에 적용한다.
    virtual void ConfigureMeleeCombo(float AttackDamage, float AbilityPower, float& FirstRawDamage,
        float& FollowupRawDamage, float& FollowupTimeRatio, bool& bFollowupUsesNotify) const {}
    virtual void ModifyBasicAttackRelease(float AttackDamage, float CriticalMultiplier, FUnitDamageRequest& Damage) const {}
    virtual void OnBasicAttackReleased() {}
    // 공통 피해 처리에서 인정한 처치 관여. 사망 이벤트 전의 기록 갱신 전용 native 알림이다.
    // 여기서 피해/전투 종료를 실행하지 않고 스킬 고유 성장 기록만 갱신한다.
    virtual void OnKillParticipation(ABattleUnitBase* DefeatedUnit);
    // 일시 카운트/시전은 제외하고 기물 소유 기록에 옮길 상태만 내보낸다.
    virtual FUnitSkillProgress CapturePersistentProgress() const;
    virtual bool CanRestorePersistentProgress(const FUnitSkillProgress& State) const;
    // 반환값은 영구 상태 복원 후 외형을 다시 적용해야 하는지 나타낸다.
    virtual bool RestorePersistentProgress(const FUnitSkillProgress& State);
    virtual bool AdvancePreparation();
    virtual USkeletalMesh* GetPersistentBodyMeshOverride() const { return nullptr; }
    virtual void ApplyPersistentVisuals();
    // 현재 체력 비율에 따라 공통 공속 계산에 더할 보너스다.
    virtual float GetHealthBasedAttackSpeedBonus(float HealthRatio) const { return 0.f; }
    virtual bool MergePersistentProgress(const FUnitSkillProgress& Kept, const FUnitSkillProgress& Material, FUnitSkillProgress& Out) const;
    void ClearExecution();
    void ResetRuntime();
    UFUNCTION(BlueprintNativeEvent, Category="Skill") bool CanUse(ABattleUnitBase* Target) const;
    virtual bool CanUse_Implementation(ABattleUnitBase* Target) const;
    UFUNCTION(BlueprintNativeEvent, Category="Skill") ABattleUnitBase* SelectTarget() const;
    virtual ABattleUnitBase* SelectTarget_Implementation() const;
    UFUNCTION(BlueprintNativeEvent, Category="Skill") void Execute(ABattleUnitBase* Target, int32 ActionId);
    virtual void Execute_Implementation(ABattleUnitBase* Target, int32 ActionId);
    // Config/Values/Context는 발사 시 복사한 값이다. 명중 때 현재 공격력으로 다시 계산하지 않는다.
    UFUNCTION(BlueprintNativeEvent, Category="Skill") void ApplyImpact(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
        const TArray<float>& Values, const FUnitDamageRequest& Context);
    virtual void ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
        const TArray<float>& Values, const FUnitDamageRequest& Context);
    // 자신이 부여한 평타 강화가 실제 명중했을 때만 받는다. 피해/회복 규칙은 개별 스킬이 결정한다.
    UFUNCTION(BlueprintNativeEvent, Category="Skill") void OnEmpoweredAttackHit(const FUnitDamageRequest& Request, const FUnitDamageResult& Result);
    virtual void OnEmpoweredAttackHit_Implementation(const FUnitDamageRequest& Request, const FUnitDamageResult& Result);
    UFUNCTION(BlueprintImplementableEvent, Category="Skill|Lifecycle") void OnRoundReset();
    UFUNCTION(BlueprintImplementableEvent, Category="Skill|Lifecycle") void OnOwnerDied();
    UFUNCTION(BlueprintImplementableEvent, Category="Skill|Lifecycle") void OnCastStarted(int32 ActionId);
    UFUNCTION(BlueprintImplementableEvent, Category="Skill|Lifecycle") void OnCastCancelled(int32 ActionId);
    UFUNCTION(BlueprintImplementableEvent, Category="Skill|Lifecycle") void OnCastFinished(int32 ActionId);
protected:
    TArray<ABattleUnitBase*> GetEffectTargets(const FUnitSkillDefinition& Config, ABattleUnitBase* Target, bool bSelf) const;
    static FName GetEffectId(const FUnitSkillDefinition& Config, int32 Index);
};

UCLASS(Abstract, Blueprintable)
class PROJECT_A_API UUnitActiveSkill : public UUnitSkillBase
{
    GENERATED_BODY()
public:
    UUnitActiveSkill();
};

UCLASS(Abstract, Blueprintable)
class PROJECT_A_API UUnitPassiveSkill : public UUnitSkillBase
{
    GENERATED_BODY()
public:
    UUnitPassiveSkill();
    // 실제 피해 결과까지 전달한다. BP는 기본 필터를 유지하려면 부모 구현을 호출한다.
    UFUNCTION(BlueprintNativeEvent, Category="Skill|Passive") bool ShouldActivate(EUnitTrigger Event,
        const FUnitDamageRequest& Context, const FUnitDamageResult& Result);
    virtual bool ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context, const FUnitDamageResult& Result);
};

UCLASS(Blueprintable)
class PROJECT_A_API UKnightShieldSkill : public UUnitActiveSkill
{
    GENERATED_BODY()
public:
    UKnightShieldSkill();
    virtual void ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target, const TArray<float>& Values, const FUnitDamageRequest& Context) override;
};
UCLASS(Blueprintable)
class PROJECT_A_API UMageFireballSkill : public UUnitActiveSkill
{
    GENERATED_BODY()
public:
    UMageFireballSkill();
    virtual void ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target, const TArray<float>& Values, const FUnitDamageRequest& Context) override;
};
UCLASS(Blueprintable)
class PROJECT_A_API UArcherRapidFireSkill : public UUnitActiveSkill
{
    GENERATED_BODY()
public:
    UArcherRapidFireSkill();
    virtual void ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target, const TArray<float>& Values, const FUnitDamageRequest& Context) override;
};
UCLASS(Blueprintable)
class PROJECT_A_API URogueWeakPointSkill : public UUnitActiveSkill
{
    GENERATED_BODY()
public:
    URogueWeakPointSkill();
    virtual void ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target, const TArray<float>& Values, const FUnitDamageRequest& Context) override;
};

// 재사용 가능한 패시브 예시. 실제 DT에는 자동 장착하지 않는다.
UCLASS(Blueprintable)
class PROJECT_A_API UHitGrowthPassiveSkill : public UUnitPassiveSkill
{
    GENERATED_BODY()
public:
    UHitGrowthPassiveSkill();
};
