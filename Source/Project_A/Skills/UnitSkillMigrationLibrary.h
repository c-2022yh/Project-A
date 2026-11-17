#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnitSkill.h"
#include "UnitSkillMigrationLibrary.generated.h"

// 에디터 스크립트용. JSON 전체 재입력 없이 기존 행의 스킬 필드만 이전한다.
UCLASS()
class PROJECT_A_API UUnitSkillMigrationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Unit|Migration")
    static bool CopyLegacySkillDefaults(UDataTable* Table, FName RowName, TSubclassOf<UUnitSkillBase> SkillClass);
    UFUNCTION(BlueprintCallable, Category="Unit|Migration")
    static bool AttachSkillClass(UDataTable* Table, FName RowName, TSubclassOf<UUnitSkillBase> SkillClass);
};
