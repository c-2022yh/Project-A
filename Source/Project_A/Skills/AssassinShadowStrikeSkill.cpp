#include "AssassinShadowStrikeSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"

UAssassinShadowStrikeSkill::UAssassinShadowStrikeSkill()
{
    AttackDamageRatio = 1.5f;
    CastSeconds = 0.25f;
}

FUnitSkillDefinition UAssassinShadowStrikeSkill::BuildSettings() const
{
    FUnitSkillDefinition Config = Super::BuildSettings();
    Config.SkillId = TEXT("AssassinShadowStrike");
    return Config;
}

ABattleUnitBase* UAssassinShadowStrikeSkill::SelectTarget_Implementation() const
{
    if (!IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)) return nullptr;
    const ABoardManager* Board = OwnerUnit->BoardManager;
    ABattleUnitBase* Best = nullptr;
    int32 BestDistance = -1;
    for (ABattleUnitBase* Unit : Board->GetUnitsOnBoard())
    {
        if (!OwnerUnit->IsEnemyAttackTarget(Unit) || !Board->FindCombatLandingTile(OwnerUnit, Unit)) continue;
        const int32 Distance = ABattleUnitBase::HexDistance(OwnerUnit->CurrentTile, Unit->CurrentTile);
        if (Distance > BestDistance)
        {
            Best = Unit;
            BestDistance = Distance;
        }
    }
    return Best;
}

void UAssassinShadowStrikeSkill::PrepareCast(ABattleUnitBase* Target, int32 ActionId)
{
    if (IsValid(OwnerUnit)) OwnerUnit->BeginCastStealth(ActionId);
}

void UAssassinShadowStrikeSkill::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (!IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)) return;
    if (!OwnerUnit->IsEnemyAttackTarget(Target) || !OwnerUnit->BoardManager->FindCombatLandingTile(OwnerUnit, Target))
        Target = SelectTarget_Implementation();
    if (IsValid(Target)) Super::Execute_Implementation(Target, ActionId);
}
