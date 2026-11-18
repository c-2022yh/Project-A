#include "RaiderAmbushSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"

FUnitSkillDefinition URaiderAmbushSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("RaiderAmbush");
    Config.Trigger = EUnitTrigger::ManaFull;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;

    FUnitSkillEffect& Damage = Config.Effects.AddDefaulted_GetRef();
    Damage.Kind = EUnitEffectKind::Damage;
    Damage.DamageType = EUnitDamageType::Physical;
    Damage.AttackDamageRatio = FMath::IsFinite(AttackDamageRatio) ? FMath::Max(0.f, AttackDamageRatio) : 0.f;
    return Config;
}

ABattleUnitBase* URaiderAmbushSkill::SelectTarget_Implementation() const
{
    if (!IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)) return nullptr;
    const ABoardManager* Board = OwnerUnit->BoardManager;
    ABattleUnitBase* Best = nullptr;
    int32 BestDistance = MAX_int32;
    for (ABattleUnitBase* Unit : Board->GetUnitsOnBoard())
    {
        if (!OwnerUnit->IsEnemyTarget(Unit) || (Best && Unit->CurrentHP > Best->CurrentHP)
            || !Board->FindCombatLandingTile(OwnerUnit, Unit)) continue;

        // 체력 수치가 같으면 가까운 적을 우선한다. 거리도 같으면 보드 순서를 유지한다.
        const int32 Distance = ABattleUnitBase::HexDistance(OwnerUnit->CurrentTile, Unit->CurrentTile);
        if (!Best || Unit->CurrentHP < Best->CurrentHP || Distance < BestDistance)
        {
            Best = Unit;
            BestDistance = Distance;
        }
    }
    return Best;
}

void URaiderAmbushSkill::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (!IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)
        || !OwnerUnit->BoardManager->TryCombatLanding(OwnerUnit, Target, ActionId)
        || !OwnerUnit->IsEnemyTarget(Target)
        || ABattleUnitBase::HexDistance(OwnerUnit->CurrentTile, Target->CurrentTile) != 1) return;

    ABattleUnitBase* Raider = OwnerUnit;
    FVector Direction = Target->GetActorLocation() - Raider->GetActorLocation();
    Direction.Z = 0.f;
    if (!Direction.IsNearlyZero()) Raider->SetActorRotation(Direction.Rotation());
    if (!IsValid(Raider) || !Raider->bInCombat || Raider->ActiveActionId != ActionId || !Raider->IsEnemyTarget(Target)
        || ABattleUnitBase::HexDistance(Raider->CurrentTile, Target->CurrentTile) != 1) return;

    Super::Execute_Implementation(Target, ActionId);
    if (IsValid(Raider) && Raider->bInCombat && Raider->IsAlive() && Raider->ActiveActionId == ActionId && Raider->IsEnemyTarget(Target))
        Raider->CurrentTarget = Target;
}
