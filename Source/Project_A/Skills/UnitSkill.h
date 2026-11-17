#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "../UnitTypes.h"
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

// 이전 DT 행과 자동 검사를 읽는 어댑터. 새 유닛은 SkillClasses만 사용한다.
UCLASS()
class PROJECT_A_API ULegacyUnitActiveSkill : public UUnitActiveSkill { GENERATED_BODY() };
UCLASS()
class PROJECT_A_API ULegacyUnitPassiveSkill : public UUnitPassiveSkill { GENERATED_BODY() };

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
