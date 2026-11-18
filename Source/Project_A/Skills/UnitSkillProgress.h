#pragma once
#include "CoreMinimal.h"
#include "UnitSkillProgress.generated.h"

class UUnitSkillBase;

// 기물별 실행 기록이다. DT 설정/스킬 객체 포인터를 저장하지 않는다.
// 키의 의미와 합성 규칙은 해당 스킬 클래스가 정의한다.
USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitSkillProgress
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Skill Progress") FName SkillId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Skill Progress") TSubclassOf<UUnitSkillBase> SkillClass;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Skill Progress") TMap<FName, int32> Counters;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Skill Progress") TSet<FName> Flags;
};

namespace UnitSkillProgress
{
    PROJECT_A_API bool Validate(const TArray<FUnitSkillProgress>& States);
    PROJECT_A_API bool Merge(const TArray<FUnitSkillProgress>& Kept, const TArray<FUnitSkillProgress>& Materials, TArray<FUnitSkillProgress>& Out);
}
