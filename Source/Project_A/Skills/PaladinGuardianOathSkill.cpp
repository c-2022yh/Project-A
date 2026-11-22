#include "PaladinGuardianOathSkill.h"
#include "../AutoBattleGameMode.h"
#include "../BattleUnitBase.h"
#include "Engine/World.h"

FUnitSkillDefinition UPaladinGuardianOathSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("PaladinGuardianOath");
    Config.Trigger = EUnitTrigger::ManaFull;
    Config.Target = EUnitSkillTarget::Self;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.3f;
    Config.CastMontage = CastMontage;
    return Config;
}

void UPaladinGuardianOathSkill::Execute_Implementation(ABattleUnitBase*, int32)
{
    ABattleUnitBase* Caster = OwnerUnit;
    if (!IsValid(Caster) || !Caster->bInCombat || !Caster->IsAlive()) return;

    const int32 Range = FMath::Max(1, AllyRange);
    // 시전이 끝난 시점의 위치와 체력으로 고른다. 거리와 체력이 모두 같으면 앞선 등록 순서를 유지한다.
    const auto SelectAlly = [Caster, Range](const auto& Roster)
    {
        ABattleUnitBase* Best = nullptr;
        int32 BestDistance = MAX_int32;
        float BestHealthRatio = MAX_flt;
        for (ABattleUnitBase* Ally : Roster)
        {
            if (!IsValid(Ally) || Ally == Caster || Ally->TeamType != Caster->TeamType
                || !Ally->bInCombat || !Ally->IsAlive() || Ally->Placement != EUnitPlacement::Board
                || Ally->GetWorld() != Caster->GetWorld()) continue;

            const int32 Distance = ABattleUnitBase::HexDistance(Caster->CurrentTile, Ally->CurrentTile);
            if (Distance > Range) continue;

            const float HealthRatio = Ally->CurrentHP / FMath::Max(1.f, Ally->FinalStats.MaxHealth);
            if (Distance < BestDistance || (Distance == BestDistance && HealthRatio < BestHealthRatio))
            {
                Best = Ally;
                BestDistance = Distance;
                BestHealthRatio = HealthRatio;
            }
        }
        return Best;
    };

    // 메인 전투는 게임모드의 등록 순서를, 테스트 전투는 전달받은 전투 명단 순서를 사용한다.
    const AAutoBattleGameMode* Mode = Caster->GetWorld()->GetAuthGameMode<AAutoBattleGameMode>();
    ABattleUnitBase* Ally = IsValid(Mode) && Mode->BattleUnits.Contains(Caster)
        ? SelectAlly(Mode->BattleUnits)
        : SelectAlly(Caster->GetCombatRoster());

    const float ShieldAt100AP = Caster->StarLevel == 2 ? TwoStarShieldAt100AP : OneStarShieldAt100AP;
    const float Amount = ShieldAt100AP * (Caster->FinalStats.AbilityPower / 100.f);
    const bool bSelfApplied = Caster->AddShield(Settings.SkillId, Amount, ShieldDuration);
    const bool bAllyApplied = IsValid(Ally) && Ally->AddShield(Settings.SkillId, Amount, ShieldDuration);
    if (bSelfApplied && IsValid(Caster)) OnProtectionApplied(Caster);
    if (bAllyApplied && IsValid(Ally)) OnProtectionApplied(Ally);
}
