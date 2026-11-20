#include "HeroGrowthPassiveSkill.h"
#include "../BattleUnitBase.h"
#include "Components/StaticMeshComponent.h"

UHeroGrowthPassiveSkill::UHeroGrowthPassiveSkill()
{
    Settings.SkillId = TEXT("HeroGrowth");
    Settings.Trigger = EUnitTrigger::Kill;
    Settings.Target = EUnitSkillTarget::Self;
}

FUnitSkillDefinition UHeroGrowthPassiveSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("HeroGrowth");
    Config.Trigger = EUnitTrigger::Kill;
    Config.Target = EUnitSkillTarget::Self;
    return Config;
}

bool UHeroGrowthPassiveSkill::IsEvolutionReady() const
{
    return !bEvolved && RequiredParticipations > 0 && TotalParticipations >= RequiredParticipations;
}

FUnitSkillProgress UHeroGrowthPassiveSkill::CapturePersistentProgress() const
{
    FUnitSkillProgress State;
    State.SkillId = TEXT("HeroGrowth"); State.SkillClass = UHeroGrowthPassiveSkill::StaticClass();
    State.Counters.Add(TEXT("Participations"), FMath::Max(0, TotalParticipations));
    if (bEvolved) State.Flags.Add(TEXT("Evolved"));
    return State;
}

bool UHeroGrowthPassiveSkill::CanRestorePersistentProgress(const FUnitSkillProgress& State) const
{
    const int32* Count = State.Counters.Find(TEXT("Participations"));
    return State.SkillId == TEXT("HeroGrowth") && State.SkillClass == UHeroGrowthPassiveSkill::StaticClass()
        && State.Counters.Num() == 1 && Count && *Count >= 0
        && (State.Flags.IsEmpty() || (State.Flags.Num() == 1 && State.Flags.Contains(TEXT("Evolved"))));
}

bool UHeroGrowthPassiveSkill::RestorePersistentProgress(const FUnitSkillProgress& State)
{
    if (!CanRestorePersistentProgress(State)) return false;
    TotalParticipations = FMath::Max(TotalParticipations, State.Counters.FindChecked(TEXT("Participations")));
    if (bEvolved || !State.Flags.Contains(TEXT("Evolved"))) return false;
    bEvolved = true;
    return true;
}

bool UHeroGrowthPassiveSkill::AdvancePreparation()
{
    if (!IsEvolutionReady()) return false;
    bEvolved = true;
    return true;
}

void UHeroGrowthPassiveSkill::ApplyPersistentVisuals()
{
    if (!bEvolved || !EvolvedWeaponMaterial || !IsValid(OwnerUnit)) return;
    UStaticMeshComponent* Weapon = OwnerUnit->MainHand;
    if (IsValid(Weapon) && WeaponMaterialSlot >= 0 && WeaponMaterialSlot < Weapon->GetNumMaterials())
        Weapon->SetMaterial(WeaponMaterialSlot, EvolvedWeaponMaterial);
}

USkeletalMesh* UHeroGrowthPassiveSkill::GetPersistentBodyMeshOverride() const
{
    return bEvolved ? EvolvedSkeletalMesh.Get() : nullptr;
}

bool UHeroGrowthPassiveSkill::MergePersistentProgress(const FUnitSkillProgress& Kept, const FUnitSkillProgress& Material, FUnitSkillProgress& Out) const
{
    if (!CanRestorePersistentProgress(Kept) || !CanRestorePersistentProgress(Material)) return false;
    Out = Kept;
    Out.Counters[TEXT("Participations")] = FMath::Max(Kept.Counters.FindChecked(TEXT("Participations")), Material.Counters.FindChecked(TEXT("Participations")));
    if (Material.Flags.Contains(TEXT("Evolved"))) Out.Flags.Add(TEXT("Evolved"));
    return true;
}

void UHeroGrowthPassiveSkill::OnKillParticipation(ABattleUnitBase* DefeatedUnit)
{
    if (!IsValid(OwnerUnit) || !OwnerUnit->bInCombat || !IsValid(DefeatedUnit) || !DefeatedUnit->bIsDead
        || DefeatedUnit->TeamType == OwnerUnit->TeamType || DefeatedUnit->GetWorld() != OwnerUnit->GetWorld()) return;
    // 기물 자신이 먼저 사망했어도 같은 전투 중 아군이 마무리한 관여는 인정한다.
    if (TotalParticipations < MAX_int32) ++TotalParticipations;
}

bool UHeroGrowthPassiveSkill::ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context, const FUnitDamageResult& Result)
{
    // 마지막 일격용 Kill 트리거와 이중 집계하지 않는다. 처치 관여 native 알림만 사용한다.
    return false;
}
