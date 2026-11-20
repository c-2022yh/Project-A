#pragma once

#include "UnitSkill.h"
#include "HeroGrowthPassiveSkill.generated.h"

class UMaterialInterface;

// 견습 용사의 처치 관여 기록과 진화 외형을 담당한다.
UCLASS(Blueprintable, HideCategories=(Skill))
class PROJECT_A_API UHeroGrowthPassiveSkill : public UUnitPassiveSkill
{
    GENERATED_BODY()
public:
    UHeroGrowthPassiveSkill();

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Growth", meta=(ClampMin="0", ToolTip="진화에 필요한 누적 처치 관여 수. 0이면 자동 진화를 사용하지 않는다."))
    int32 RequiredParticipations = 0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Growth|Visuals", meta=(ToolTip="진화 후 몸 메시. 기본 몸 메시와 같은 스켈레톤을 사용해야 한다."))
    TObjectPtr<USkeletalMesh> EvolvedSkeletalMesh = nullptr;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Growth|Visuals")
    TObjectPtr<UMaterialInterface> EvolvedWeaponMaterial = nullptr;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hero Growth|Visuals", meta=(ClampMin="0"))
    int32 WeaponMaterialSlot = 0;

    // 전투별 Counter와 별개다. StopCombat/ResetForRound/StartCombat에서 초기화하지 않는다.
    // 액터 재생성 시에는 Capture/RestoreSkillProgress로 별도 이관한다. 디스크 저장 기능은 아니다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Hero Growth|Progress", meta=(ToolTip="이 기물이 누적한 처치 관여 횟수. 같은 적의 한 번의 사망당 1회이며 라운드 사이에 유지한다."))
    int32 TotalParticipations = 0;
    // 합성 및 액터 재생성 시 이어받는 진화 상태다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Hero Growth|Progress")
    bool bEvolved = false;

    UFUNCTION(BlueprintPure, Category="Hero Growth") bool IsEvolutionReady() const;
    virtual FUnitSkillDefinition BuildSettings() const override;
    virtual FUnitSkillProgress CapturePersistentProgress() const override;
    virtual bool CanRestorePersistentProgress(const FUnitSkillProgress& State) const override;
    virtual bool RestorePersistentProgress(const FUnitSkillProgress& State) override;
    virtual bool AdvancePreparation() override;
    virtual USkeletalMesh* GetPersistentBodyMeshOverride() const override;
    virtual void ApplyPersistentVisuals() override;
    virtual bool MergePersistentProgress(const FUnitSkillProgress& Kept, const FUnitSkillProgress& Material, FUnitSkillProgress& Out) const override;
    virtual void OnKillParticipation(ABattleUnitBase* DefeatedUnit) override;
    virtual bool ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context, const FUnitDamageResult& Result) override;
};
