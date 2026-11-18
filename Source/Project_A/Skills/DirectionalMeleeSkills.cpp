#include "DirectionalMeleeSkills.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"

FUnitSkillDefinition UDirectionalMeleeSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = Settings.SkillId;
    Config.CastRange = 1;
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;
    Config.bUseAnimNotify = bUseAnimNotify;
    for (const float Ratio : {PrimaryAttackDamageRatio, SecondaryAttackDamageRatio})
    {
        FUnitSkillEffect& Effect = Config.Effects.AddDefaulted_GetRef();
        Effect.Kind = EUnitEffectKind::Damage;
        Effect.DamageType = EUnitDamageType::Physical;
        Effect.AttackDamageRatio = FMath::IsFinite(Ratio) ? FMath::Max(0.f, Ratio) : 0.f;
    }
    return Config;
}

bool UDirectionalMeleeSkill::CanUse_Implementation(ABattleUnitBase* Target) const
{
    if (!Super::CanUse_Implementation(Target) || !IsValid(OwnerUnit->BoardManager) || !OwnerUnit->IsEnemyTarget(Target))
        return false;
    const ABoardTile* Origin = OwnerUnit->CurrentTile;
    const ABoardTile* Front = Target->CurrentTile;
    return IsValid(Origin) && Front->OwningBoard == OwnerUnit->BoardManager
        && Origin->OwningBoard == OwnerUnit->BoardManager && ABattleUnitBase::HexDistance(Origin, Front) == 1;
}

void UDirectionalMeleeSkill::PrepareCast(ABattleUnitBase* Target, int32 ActionId)
{
    StrikeTiles.Reset();
    StrikeActionId = 0;
    if (!CanUse_Implementation(Target)) return;

    const ABoardTile* Origin = OwnerUnit->CurrentTile;
    ABoardTile* Front = Target->CurrentTile;
    const FIntPoint Direction(Front->HexQ - Origin->HexQ, Front->HexR - Origin->HexR);
    StrikeTiles.Add(Front);
    for (const FIntPoint Offset : GetSecondaryOffsets(Direction))
        if (ABoardTile* Tile = OwnerUnit->BoardManager->GetTileByHex(Origin->HexQ + Offset.X, Origin->HexR + Offset.Y))
            StrikeTiles.AddUnique(Tile);
    StrikeActionId = ActionId;
}

void UDirectionalMeleeSkill::ApplyImpact_Implementation(const FUnitSkillDefinition& Config, ABattleUnitBase* Target,
    const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    if (!IsValid(OwnerUnit) || Context.ActionId <= 0 || Context.ActionId != StrikeActionId || Values.Num() != 2) return;
    ABattleUnitBase* Caster = OwnerUnit;
    StrikeActionId = 0;
    const auto Tiles = MoveTemp(StrikeTiles);
    TArray<TPair<TWeakObjectPtr<ABattleUnitBase>, float>> Hits;
    for (int32 Index = 0; Index < Tiles.Num(); ++Index)
    {
        const ABoardTile* Tile = Tiles[Index].Get();
        ABattleUnitBase* Unit = IsValid(Tile) ? Cast<ABattleUnitBase>(Tile->OccupiedUnit) : nullptr;
        if (Caster->IsEnemyTarget(Unit) && Unit->CurrentTile == Tile)
            Hits.Emplace(Unit, Values[Index == 0 ? 0 : 1]);
    }

    for (const auto& Hit : Hits)
    {
        if (!IsValid(Caster) || !Caster->bInCombat || Caster->ActiveActionId != Context.ActionId) return;
        ABattleUnitBase* Unit = Hit.Key.Get();
        if (!Caster->IsEnemyTarget(Unit)) continue;
        FUnitDamageRequest Damage = Context;
        Damage.Victim = Unit;
        Damage.RawDamage = Hit.Value;
        Damage.Type = EUnitDamageType::Physical;
        Damage.DamageId = FGuid::NewGuid();
        ApplyStrikeHit(Unit, Damage);
    }
}

void UDirectionalMeleeSkill::ApplyStrikeHit(ABattleUnitBase* Target, const FUnitDamageRequest& Damage)
{
    Target->ReceiveDamage(Damage);
}

UBarbarianGroundSlamSkill::UBarbarianGroundSlamSkill()
{
    Settings.SkillId = TEXT("BarbarianGroundSlam");
    PrimaryAttackDamageRatio = 2.5f;
    SecondaryAttackDamageRatio = 0.8f;
}

TArray<FIntPoint> UBarbarianGroundSlamSkill::GetSecondaryOffsets(FIntPoint Direction) const
{
    // 육각 축 좌표에서 정면 방향을 좌우로 한 칸씩 회전한다.
    const int32 Q = Direction.X;
    const int32 R = Direction.Y;
    return {FIntPoint(Q + R, -Q), FIntPoint(-R, Q + R)};
}

USpearmanPiercingThrustSkill::USpearmanPiercingThrustSkill()
{
    Settings.SkillId = TEXT("SpearmanPiercingThrust");
    PrimaryAttackDamageRatio = 2.2f;
    SecondaryAttackDamageRatio = 1.4f;
}

TArray<FIntPoint> USpearmanPiercingThrustSkill::GetSecondaryOffsets(FIntPoint Direction) const
{
    return {FIntPoint(2 * Direction.X, 2 * Direction.Y)};
}

UDarkKnightJudgmentSkill::UDarkKnightJudgmentSkill()
{
    Settings.SkillId = TEXT("DarkKnightJudgment");
    PrimaryAttackDamageRatio = 1.2f;
    SecondaryAttackDamageRatio = 1.2f;
}

void UDarkKnightJudgmentSkill::ApplyStrikeHit(ABattleUnitBase* Target, const FUnitDamageRequest& Damage)
{
    if (!Target->ReceiveDamage(Damage).bApplied || !IsValid(Target) || !Target->IsAlive()) return;
    FUnitStatModifier Stun;
    Stun.bStun = true;
    Target->AddModifier(TEXT("DarkKnightJudgment"), Stun, StunSeconds);
}
