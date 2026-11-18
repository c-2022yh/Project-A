#include "BlackfeatherArrowRainSkill.h"
#include "../BattleUnitBase.h"
#include "../BoardManager.h"
#include "../BoardTile.h"

FUnitSkillDefinition UBlackfeatherArrowRainSkill::BuildSettings() const
{
    FUnitSkillDefinition Config;
    Config.SkillId = TEXT("BlackfeatherArrowRain");
    Config.CastSeconds = FMath::IsFinite(CastSeconds) ? FMath::Max(0.01f, CastSeconds) : 0.01f;
    Config.CastMontage = CastMontage;

    // 효과 배열은 순서대로 실행된다. 화살비 자체도 방깎이 적용된 방어력으로 계산한다.
    FUnitSkillEffect& Shred = Config.Effects.AddDefaulted_GetRef();
    Shred.Kind = EUnitEffectKind::Modifier;
    Shred.Modifier.ArmorReduction = FMath::IsFinite(ArmorReduction) ? FMath::Clamp(ArmorReduction, 0.f, 1.f) : 0.f;
    Shred.Duration = FMath::IsFinite(ArmorReductionSeconds) ? FMath::Max(0.f, ArmorReductionSeconds) : 0.f;
    FUnitSkillEffect& Damage = Config.Effects.AddDefaulted_GetRef();
    Damage.Kind = EUnitEffectKind::Damage;
    Damage.DamageType = EUnitDamageType::Physical;
    Damage.AttackDamageRatio = FMath::IsFinite(AttackDamageRatio) ? FMath::Max(0.f, AttackDamageRatio) : 0.f;
    return Config;
}

ABattleUnitBase* UBlackfeatherArrowRainSkill::SelectTarget_Implementation() const
{
    if (!IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)) return nullptr;
    ABattleUnitBase* Current = OwnerUnit->CurrentTarget;
    if (OwnerUnit->IsEnemyTarget(Current) && Current->CurrentTile->OwningBoard == OwnerUnit->BoardManager) return Current;
    for (ABattleUnitBase* Unit : OwnerUnit->BoardManager->GetUnitsOnBoard())
        if (OwnerUnit->IsEnemyTarget(Unit)) return Unit;
    return nullptr;
}

void UBlackfeatherArrowRainSkill::Execute_Implementation(ABattleUnitBase* Target, int32 ActionId)
{
    if (!IsValid(OwnerUnit) || !IsValid(OwnerUnit->BoardManager)) return;
    const ABoardManager* Board = OwnerUnit->BoardManager;
    ABoardTile* BestCenter = nullptr;
    TArray<ABoardTile*> BestTiles;
    int32 BestCount = 0;
    int32 BestDistance = MAX_int32;
    bool bBestIncludesCurrent = false;

    for (ABoardTile* Center : Board->Tiles)
    {
        if (!IsValid(Center) || Center->OwningBoard != Board) continue;
        TArray<ABoardTile*> Tiles = Board->GetNeighbors(Center);
        Tiles.AddUnique(Center);
        int32 Count = 0;
        bool bIncludesCurrent = false;
        for (ABoardTile* Tile : Tiles)
        {
            const ABattleUnitBase* Unit = Cast<ABattleUnitBase>(Tile->OccupiedUnit);
            if (!OwnerUnit->IsEnemyTarget(Unit) || Unit->CurrentTile != Tile) continue;
            ++Count;
            bIncludesCurrent |= Unit == OwnerUnit->CurrentTarget;
        }
        if (Count == 0) continue;
        const int32 Distance = ABattleUnitBase::HexDistance(OwnerUnit->CurrentTile, Center);
        const bool bWinsTie = bIncludesCurrent != bBestIncludesCurrent ? bIncludesCurrent : Distance < BestDistance;
        if (Count > BestCount || (Count == BestCount && bWinsTie))
        {
            BestCenter = Center;
            BestTiles = MoveTemp(Tiles);
            BestCount = Count;
            BestDistance = Distance;
            bBestIncludesCurrent = bIncludesCurrent;
        }
    }

    const float Delay = FMath::IsFinite(ImpactDelaySeconds) ? FMath::Max(0.01f, ImpactDelaySeconds) : 0.5f;
    if (BestCenter && OwnerUnit->QueueTileImpact(this, BestTiles, Delay, ActionId))
        OnArrowRainLaunched(BestCenter, BestTiles, Delay);
}
