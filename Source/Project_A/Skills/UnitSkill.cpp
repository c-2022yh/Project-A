#include "UnitSkill.h"
#include "../BattleUnitBase.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "Engine/LatentActionManager.h"
#include "TimerManager.h"

UWorld* UUnitSkillBase::GetWorld() const
{
    return !HasAnyFlags(RF_ClassDefaultObject) && IsValid(OwnerUnit) ? OwnerUnit->GetWorld() : nullptr;
}

FUnitSkillDefinition UUnitSkillBase::BuildSettings() const { return Settings; }
void UUnitSkillBase::OnKillParticipation(ABattleUnitBase* DefeatedUnit) {}
FUnitSkillProgress UUnitSkillBase::CapturePersistentProgress() const { return {}; }
bool UUnitSkillBase::CanRestorePersistentProgress(const FUnitSkillProgress& State) const { return false; }
bool UUnitSkillBase::RestorePersistentProgress(const FUnitSkillProgress& State) { return false; }
bool UUnitSkillBase::AdvancePreparation() { return false; }
void UUnitSkillBase::ApplyPersistentVisuals() {}
bool UUnitSkillBase::MergePersistentProgress(const FUnitSkillProgress& Kept, const FUnitSkillProgress& Material, FUnitSkillProgress& Out) const { return false; }
void UUnitSkillBase::OnEmpoweredAttackHit_Implementation(const FUnitDamageRequest& Request, const FUnitDamageResult& Result) {}

void UUnitSkillBase::ClearExecution()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearAllTimersForObject(this);
        World->GetLatentActionManager().RemoveActionsForObject(this);
    }
}
void UUnitSkillBase::ResetRuntime()
{
    ClearExecution();
    Counter = 0;
    Cooldown = 0.f;
    OnRoundReset();
}

bool UUnitSkillBase::CanUse_Implementation(ABattleUnitBase* Target) const
{
    return IsValid(OwnerUnit) && OwnerUnit->bInCombat && OwnerUnit->IsAlive()
        && Cooldown <= 0.f && IsValid(Target) && Target->IsAlive() && Target->bInCombat;
}

ABattleUnitBase* UUnitSkillBase::SelectTarget_Implementation() const
{
    return IsValid(OwnerUnit) ? OwnerUnit->SelectSkillTarget(Settings) : nullptr;
}

void UUnitSkillBase::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (IsValid(OwnerUnit)) OwnerUnit->ExecuteSkillPayload(this, Target, ActionId);
}

TArray<ABattleUnitBase*> UUnitSkillBase::GetEffectTargets(const FUnitSkillDefinition& Config, ABattleUnitBase* Target, bool bSelf) const
{
    TArray<ABattleUnitBase*> Result;
    if (!IsValid(OwnerUnit) || !OwnerUnit->bInCombat) return Result;
    if (bSelf)
    {
        if (OwnerUnit->IsAlive()) Result.Add(OwnerUnit);
        return Result;
    }
    if (!IsValid(Target) || !Target->IsAlive() || !Target->bInCombat) return Result;
    Result.Add(Target);
    if (Config.AreaRadius > 0)
        for (ABattleUnitBase* Unit : OwnerUnit->CombatRoster)
            if (IsValid(Unit) && Unit != Target && Unit->TeamType == Target->TeamType && Unit->IsAlive() && Unit->bInCombat
                && ABattleUnitBase::HexDistance(Target->CurrentTile, Unit->CurrentTile) <= Config.AreaRadius) Result.AddUnique(Unit);
    return Result;
}

FName UUnitSkillBase::GetEffectId(const FUnitSkillDefinition& Config, int32 Index)
{
    return Config.Effects[Index].EffectId.IsNone()
        ? FName(*FString::Printf(TEXT("%s.%d"), *Config.SkillId.ToString(), Index)) : Config.Effects[Index].EffectId;
}

UUnitActiveSkill::UUnitActiveSkill() { Settings.Trigger = EUnitTrigger::ManaFull; }
UUnitPassiveSkill::UUnitPassiveSkill() { Settings.Trigger = EUnitTrigger::BasicAttackHit; }

bool UUnitPassiveSkill::ShouldActivate_Implementation(EUnitTrigger Event, const FUnitDamageRequest& Context, const FUnitDamageResult& Result)
{
    if (Settings.Trigger != Event || Cooldown > 0.f || Context.TriggerChain.Contains(Settings.SkillId)
        || (!bAllowTriggeredEvents && Context.Source == EUnitDamageSource::Triggered)) return false;
    ++Counter;
    return Counter % FMath::Max(1, Settings.TriggerEvery) == 0;
}

UKnightShieldSkill::UKnightShieldSkill()
{
    Settings.SkillId = TEXT("KnightShield"); Settings.Target = EUnitSkillTarget::Self;
    FUnitSkillEffect Effect; Effect.EffectId = TEXT("KnightShieldEffect"); Effect.Kind = EUnitEffectKind::Shield;
    Effect.bApplyToSelf = true; Effect.BaseValue = 30.f; Effect.Duration = 3.f; Settings.Effects.Add(Effect);
}
void UKnightShieldSkill::ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
    const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    for (int32 Index = 0; Index < Config.Effects.Num(); ++Index)
    {
        const FUnitSkillEffect& Effect = Config.Effects[Index];
        for (ABattleUnitBase* Unit : GetEffectTargets(Config, Target, Effect.bApplyToSelf))
            Unit->AddShield(GetEffectId(Config, Index), Values.IsValidIndex(Index) ? Values[Index] : 0.f, Effect.Duration);
    }
}

UMageFireballSkill::UMageFireballSkill()
{
    Settings.SkillId = TEXT("Fireball"); Settings.Target = EUnitSkillTarget::CurrentEnemy;
    Settings.CastRange = 3; Settings.AreaRadius = 1; Settings.bProjectile = true;
    FUnitSkillEffect Effect; Effect.EffectId = TEXT("Fireball_Damage"); Effect.Kind = EUnitEffectKind::Damage;
    Effect.BaseValue = 100.f; Effect.TwoStarBaseValue = 324.f; Settings.Effects.Add(Effect);
}
void UMageFireballSkill::ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
    const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    for (int32 Index = 0; Index < Config.Effects.Num(); ++Index)
    {
        const FUnitSkillEffect& Effect = Config.Effects[Index];
        for (ABattleUnitBase* Unit : GetEffectTargets(Config, Target, Effect.bApplyToSelf))
        {
            FUnitDamageRequest Damage = Context; Damage.Victim = Unit; Damage.DamageId = FGuid::NewGuid();
            Damage.RawDamage = Values.IsValidIndex(Index) ? Values[Index] : 0.f;
            Damage.Type = Effect.DamageType; Damage.bCritical = false; Unit->ReceiveDamage(Damage);
        }
    }
}

UArcherRapidFireSkill::UArcherRapidFireSkill()
{
    Settings.SkillId = TEXT("RapidFire"); Settings.Target = EUnitSkillTarget::Self; Settings.CastSeconds = 0.2f;
    FUnitSkillEffect Effect; Effect.EffectId = TEXT("RapidFire"); Effect.Kind = EUnitEffectKind::Modifier;
    Effect.bApplyToSelf = true; Effect.Duration = 4.f; Effect.Modifier.AttackSpeedBonus = 0.5f;
    Effect.Modifier.bBlockManaGain = true; Effect.bOverrideModifierAtTwoStars = true;
    Effect.TwoStarModifier = Effect.Modifier; Effect.TwoStarModifier.AttackSpeedBonus = 0.8f; Settings.Effects.Add(Effect);
}
void UArcherRapidFireSkill::ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
    const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    if (!IsValid(OwnerUnit)) return;
    for (int32 Index = 0; Index < Config.Effects.Num(); ++Index)
    {
        const FUnitSkillEffect& Effect = Config.Effects[Index];
        const FUnitStatModifier& Modifier = OwnerUnit->StarLevel == 2 && Effect.bOverrideModifierAtTwoStars ? Effect.TwoStarModifier : Effect.Modifier;
        for (ABattleUnitBase* Unit : GetEffectTargets(Config, Target, Effect.bApplyToSelf))
            Unit->AddModifier(GetEffectId(Config, Index), Modifier, Effect.Duration);
    }
}

URogueWeakPointSkill::URogueWeakPointSkill()
{
    Settings.SkillId = TEXT("WeakPointStrike"); Settings.Target = EUnitSkillTarget::Self; Settings.CastSeconds = 0.2f;
    FUnitSkillEffect Effect; Effect.EffectId = TEXT("WeakPointStrike"); Effect.Kind = EUnitEffectKind::EmpowerNextAttack;
    Effect.bApplyToSelf = true; Effect.Duration = 10.f; Effect.BaseValue = 0.f; Effect.TwoStarBaseValue = 0.f;
    Effect.EmpowerAttackMultiplier = 2.f; Settings.Effects.Add(Effect);
}
void URogueWeakPointSkill::ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
    const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    for (int32 Index = 0; Index < Config.Effects.Num(); ++Index)
    {
        const FUnitSkillEffect& Effect = Config.Effects[Index];
        for (ABattleUnitBase* Unit : GetEffectTargets(Config, Target, Effect.bApplyToSelf))
            Unit->EmpowerNextAttack(GetEffectId(Config, Index), Effect.EmpowerAttackMultiplier,
                Values.IsValidIndex(Index) ? Values[Index] : 0.f, Effect.Duration);
    }
}

UHitGrowthPassiveSkill::UHitGrowthPassiveSkill()
{
    Settings.SkillId = TEXT("EveryThirdHitGrowth"); Settings.Target = EUnitSkillTarget::Self;
    Settings.TriggerEvery = 3;
    FUnitSkillEffect Effect; Effect.Kind = EUnitEffectKind::PermanentGrowth; Effect.BaseValue = 1.f;
    Effect.bApplyToSelf = true; Effect.GrowthLimitPerCombat = 3.f; Settings.Effects.Add(Effect);
}
void UUnitSkillBase::ApplyImpact_Implementation(const FUnitSkillDefinition& Skill, ABattleUnitBase* Target, const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    if (!IsValid(OwnerUnit) || !OwnerUnit->bInCombat) return;
    ABattleUnitBase* Owner = OwnerUnit;
    if (!IsValid(Target) || !Target->IsAlive() || !Target->bInCombat) return;
    TArray<TWeakObjectPtr<ABattleUnitBase>> Targets;
    Targets.Add(Target);
    if (Skill.AreaRadius > 0)
        for (ABattleUnitBase* Unit : Owner->CombatRoster)
            if (IsValid(Unit) && Unit != Target && Unit->TeamType == Target->TeamType && Unit->IsAlive() && Unit->bInCombat
                && ABattleUnitBase::HexDistance(Target->CurrentTile, Unit->CurrentTile) <= Skill.AreaRadius) Targets.AddUnique(Unit);
    for (int32 Index = 0; Index < Skill.Effects.Num(); ++Index)
    {
        const FUnitSkillEffect Effect = Skill.Effects[Index];
        const FName EffectId = Effect.EffectId.IsNone() ? FName(*FString::Printf(TEXT("%s.%d"), *Skill.SkillId.ToString(), Index)) : Effect.EffectId;
        TArray<TWeakObjectPtr<ABattleUnitBase>> Recipients = Targets;
        if (Effect.bApplyToSelf) { Recipients.Empty(); Recipients.Add(Owner); }
        for (TWeakObjectPtr<ABattleUnitBase> Recipient : Recipients)
        {
            ABattleUnitBase* Unit = Recipient.Get();
            if (!Owner->bInCombat || !IsValid(Unit) || !Unit->IsAlive() || !Unit->bInCombat) continue;
            const float Value = Values.IsValidIndex(Index) ? Values[Index] : 0.f;
            switch (Effect.Kind)
            {
                case EUnitEffectKind::Damage:
                {
                    FUnitDamageRequest Damage = Context; Damage.RawDamage = Value; Damage.Type = Effect.DamageType;
                    Damage.Victim = Unit; Damage.DamageId = FGuid::NewGuid(); Damage.bCritical = false;
                    Unit->ReceiveDamage(Damage); break;
                }
                case EUnitEffectKind::Healing: Unit->ReceiveHealing(Value, Owner); break;
                case EUnitEffectKind::Shield: Unit->AddShield(EffectId, Value, Effect.Duration); break;
                case EUnitEffectKind::Modifier:
                    Unit->AddModifier(EffectId, Owner->StarLevel == 2 && Effect.bOverrideModifierAtTwoStars ? Effect.TwoStarModifier : Effect.Modifier, Effect.Duration); break;
                case EUnitEffectKind::Mana: Unit->GainMana(Value); break;
                case EUnitEffectKind::PermanentGrowth: Unit->AddPermanentGrowth(EffectId, Effect.GrowthStat, Value, Effect.GrowthLimitPerCombat); break;
                case EUnitEffectKind::EmpowerNextAttack:
                {
                    Unit->EmpowerNextAttack(EffectId, Effect.EmpowerAttackMultiplier, Value, Effect.Duration); break;
                }
            }
        }
    }
}

