#pragma once
#include "../Skills/UnitSkill.h"
#include "../BattleUnitBase.h"
#include "UnitSkillTestTypes.generated.h"

// 자동 검사에서 효과 조합을 주입하는 임시 객체. 실제 DT/스킬 BP에는 사용하지 않는다.
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UUnitSkillTestActive : public UUnitActiveSkill
{
    GENERATED_BODY()
public:
    UUnitSkillTestActive() { Settings.SkillId = TEXT("TestActive"); }
};
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UUnitSkillTestPassive : public UUnitPassiveSkill
{
    GENERATED_BODY()
public:
    UUnitSkillTestPassive() { Settings.SkillId = TEXT("TestPassive"); }
};

// 사망 이벤트가 즉시 전투를 종료해도 성장 기록이 먼저 확정되는지 확인한다.
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UUnitSkillTestDeathObserver : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<ABattleUnitBase> UnitToStop;
    UFUNCTION() void OnDeath(ABattleUnitBase* DefeatedUnit)
    {
        if (IsValid(UnitToStop)) UnitToStop->StopCombat();
    }
};
