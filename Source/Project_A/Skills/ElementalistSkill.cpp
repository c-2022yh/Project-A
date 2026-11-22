#include "ElementalistSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"

namespace
{
    constexpr int32 MeteorRadius = 2;

    float AtLeast(float Value, float Minimum, float Fallback)
    {
        return FMath::IsFinite(Value) ? FMath::Max(Minimum, Value) : Fallback;
    }
}

void UElementalistSkill::RefreshCastSettings()
{
    Settings = BuildSettings();
}

void UElementalistSkill::ResetRuntime()
{
    NextForm = CastingForm = EElementalForm::Frost;
    MeteorCenter.Reset();
    RefreshCastSettings();
    Super::ResetRuntime();
}

FUnitSkillDefinition UElementalistSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("Elementalist");
    const bool bFire = GetForm() == EElementalForm::Fire;
    Config.CastSeconds = bFire ? AtLeast(FireCastSeconds, 0.01f, 0.3f) : AtLeast(FrostCastSeconds, 0.01f, 0.01f);
    Config.CastMontage = bFire ? FireCastMontage : FrostCastMontage;
    Config.CastRange = FMath::Max(1, FrostCastRange);

    FUnitSkillEffect& Damage = Config.Effects.AddDefaulted_GetRef();
    Damage.Kind = EUnitEffectKind::Damage;
    Damage.DamageType = EUnitDamageType::Magical;
    if (!bFire)
    {
        Config.bProjectile = true;
        Config.ProjectileSpeed = AtLeast(FrostProjectileSpeed, 1.f, 2000.f);
        Config.ProjectileMaxLifetime = 5.f;
        Config.ProjectileVisual = FrostProjectileVisual;
        FUnitSkillEffect& Slow = Config.Effects.AddDefaulted_GetRef();
        Slow.Kind = EUnitEffectKind::Modifier;
        Slow.Duration = AtLeast(FrostSlowSeconds, 0.f, 3.f);
        Slow.Modifier.AttackSpeedSlow = FMath::Clamp(FrostAttackSpeedSlow, 0.f, 1.f);
    }
    return Config;
}

void UElementalistSkill::ModifyMaxMana(float& MaxMana) const
{
    MaxMana = GetForm() == EElementalForm::Fire ? AtLeast(FireMaxMana, 1.f, 100.f) : AtLeast(FrostMaxMana, 1.f, 45.f);
}

void UElementalistSkill::CustomizeEffectValues(TArray<float>& Values) const
{
    if (!IsValid(OwnerUnit) || Values.IsEmpty()) return;
    const bool bTwoStar = OwnerUnit->StarLevel == 2;
    const float Ratio = CastingForm == EElementalForm::Fire
        ? (bTwoStar ? FireTwoStarAPRatio : FireOneStarAPRatio)
        : (bTwoStar ? FrostTwoStarAPRatio : FrostOneStarAPRatio);
    Values[0] = OwnerUnit->FinalStats.AbilityPower * AtLeast(Ratio, 0.f, 0.f);
}

ABattleUnitBase* UElementalistSkill::SelectTarget_Implementation() const
{
    if (GetForm() == EElementalForm::Frost) return Super::SelectTarget_Implementation();
    if (!IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)) return nullptr;
    auto Enemies = OwnerUnit->BoardManager->GetUnitsOnBoard();
    Enemies.RemoveAll([this](const ABattleUnitBase* Unit) { return !OwnerUnit->IsEnemyTarget(Unit); });

    ABattleUnitBase* Best = nullptr;
    int32 BestCount = 0;
    int32 BestDistance = MAX_int32;
    for (ABattleUnitBase* Center : Enemies)
    {
        int32 Count = 0;
        for (const ABattleUnitBase* Enemy : Enemies)
            if (ABattleUnitBase::HexDistance(Center->CurrentTile, Enemy->CurrentTile) <= MeteorRadius) ++Count;

        const int32 Distance = ABattleUnitBase::HexDistance(OwnerUnit->CurrentTile, Center->CurrentTile);
        const bool bCurrent = Center == OwnerUnit->CurrentTarget;
        const bool bBestCurrent = Best && Best == OwnerUnit->CurrentTarget;
        const bool bWinsTie = bCurrent != bBestCurrent ? bCurrent : Distance < BestDistance;
        if (Count > BestCount || (Count == BestCount && bWinsTie))
        {
            Best = Center;
            BestCount = Count;
            BestDistance = Distance;
        }
    }
    return Best;
}

void UElementalistSkill::PrepareCast(ABattleUnitBase* Target, int32 ActionId)
{
    CastingForm = NextForm;
    MeteorCenter = CastingForm == EElementalForm::Fire && IsValid(Target) ? Target->CurrentTile.Get() : nullptr;
}

void UElementalistSkill::BeginCast(int32 ActionId)
{
    if (!IsValid(OwnerUnit)) return;
    // 다음 주문의 마나는 바로 표시하고, 진행 중인 주문의 설정은 효과 처리까지 유지한다.
    NextForm = CastingForm == EElementalForm::Fire ? EElementalForm::Frost : EElementalForm::Fire;
    OwnerUnit->RecalculateStats();
    // 유성 타이머는 모션 완료 시점이 아닌 시전 시작부터 돈다.
    if (CastingForm == EElementalForm::Fire) OwnerUnit->CommitSkillEffect(ActionId);
    if (IsValid(OwnerUnit) && OwnerUnit->ActiveActionId == ActionId) OnFormChanged(NextForm);
}

bool UElementalistSkill::RequiresLivingCastTarget() const
{
    return CastingForm == EElementalForm::Frost;
}

void UElementalistSkill::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (CastingForm == EElementalForm::Frost)
    {
        Super::Execute_Implementation(Target, ActionId);
        return;
    }
    ABoardTile* Center = MeteorCenter.Get();
    MeteorCenter.Reset();
    if (!IsValid(Center) || !IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)) return;

    TArray<ABoardTile*> Tiles;
    TArray<float> Scales;
    for (ABoardTile* Tile : OwnerUnit->BoardManager->Tiles)
    {
        if (!IsValid(Tile) || Tile->OwningBoard != OwnerUnit->BoardManager) continue;
        const int32 Distance = ABattleUnitBase::HexDistance(Center, Tile);
        if (Distance > MeteorRadius) continue;
        Tiles.Add(Tile);
        Scales.Add(Distance == 0 ? 1.f : (Distance == 1 ? MeteorInnerScale : MeteorOuterScale));
    }
    const float Delay = AtLeast(MeteorDelay, 0.01f, 1.5f);
    if (OwnerUnit->QueueTileImpact(this, Tiles, Delay, ActionId, Scales)) OnMeteorTelegraph(Center, Tiles, Delay);
}
