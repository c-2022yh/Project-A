#pragma once

#include "CoreMinimal.h"
#include "ShopTypes.h"
#include "UObject/Interface.h"
#include "UnitPlacement.generated.h"

class ABattleUnitBase;

// Implemented by the integration layer using the board and combat teams' APIs.
UINTERFACE(BlueprintType, Blueprintable, meta=(ScriptName="ShopUnitPlacement"))
class PROJECT_A_API UUnitPlacement : public UInterface
{
    GENERATED_BODY()
};

class PROJECT_A_API IUnitPlacement
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FUnitPosition GetUnitPosition(FGuid Id) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FGuid GetOccupant(FUnitPosition Position) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FUnitPosition FindRewardTile(int32 DeploymentLimit) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    bool GetUnitProfile(FGuid Id, FRosterUnitProfile& Profile) const;

    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    FGuid GetHoveredUnit() const;
    // GUID에 대응하는 전투 Actor를 반환한다. 없으면 저장된 보유 기록만 사용한다.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    ABattleUnitBase* GetBattleUnit(FGuid Id) const;

    // Apply the whole batch, including combat actor changes, or return false with no changes.
    // The adapter validates occupancy and deployment again at commit time. A move into an
    // occupied position is expressed as two Move edits in the same batch.
    // Add/Update/Move는 Change.Unit.SkillProgress도 Actor에 복원해야 한다.
    // CanRestoreSkillProgress로 전체 배치를 사전 검사하고 성공할 때만 RestoreSkillProgress한다.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Shop|Placement")
    bool ApplyUnitChanges(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 DeploymentLimit);
};

