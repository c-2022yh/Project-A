#include "BattleUnitBase.h"
#include "BoardTile.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
    float Safe(float Value, float Fallback = 0.f) { return FMath::IsFinite(Value) ? Value : Fallback; }
    float Positive(float Value) { return FMath::Max(0.f, Safe(Value)); }
    float Fraction(float Value) { return FMath::Clamp(Safe(Value), 0.f, 1.f); }
    float StarMultiplier(const TArray<float>& Values, int32 Star)
    {
        return Star == 1 ? 1.f : (Values.IsValidIndex(1) ? Positive(Values[1]) : 1.8f);
    }
    // 음수 저항 정책만 분리한다. 양수 공식과 피해 계산 순서는 명세의 확정 규칙이다.
    float ResistanceFactor(float Resistance, EUnitNegativeResistancePolicy Policy)
    {
        Resistance = Safe(Resistance);
        if (Resistance >= 0.f) return 100.f / (100.f + Resistance);
        return Policy == EUnitNegativeResistancePolicy::ClampToZero ? 1.f : 2.f - 100.f / (100.f - Resistance);
    }
}

ABattleUnitBase::ABattleUnitBase()
{
    PrimaryActorTick.bCanEverTick = true;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
    VisualRoot->SetupAttachment(SceneRoot);
    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(VisualRoot);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MainHand = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MainHand"));
    MainHand->SetupAttachment(Body);
    MainHand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    OffHand = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OffHand"));
    OffHand->SetupAttachment(Body);
    OffHand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SelectionCollision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("SelectionCollision"));
    SelectionCollision->SetupAttachment(SceneRoot);
    SelectionCollision->InitCapsuleSize(45.f, 100.f);
    SelectionCollision->SetRelativeLocation(FVector(0, 0, 100.f));
    SelectionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SelectionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
    SelectionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    SelectionCollision->SetGenerateOverlapEvents(false);
}

void ABattleUnitBase::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (GetWorld() && !GetWorld()->IsGameWorld())
    {
        if (UnitData.DataTable && !UnitData.RowName.IsNone()) RefreshUnitFromData();
        else { ClearVisuals(); bInitialized = false; UnitID = NAME_None; CurrentHP = 0.f; }
    }
}
void ABattleUnitBase::BeginPlay()
{
    Super::BeginPlay();
    if (!bInitialized && UnitData.DataTable) RefreshUnitFromData();
}

bool ABattleUnitBase::InitializeUnit(FDataTableRowHandle Data, EBattleTeam Team, int32 Stars, FString& Error)
{
    Error.Reset();
    if (bInCombat || IsValid(CurrentTile)) Error = TEXT("Remove the unit from its tile before initialization.");
    else if (Placement == EUnitPlacement::Waiting && Team != EBattleTeam::Player) Error = TEXT("Waiting area is player-only.");
    else if (Stars < 1 || Stars > 2) Error = TEXT("Only one-star and two-star units are supported.");
    else if (Team != EBattleTeam::Player && Team != EBattleTeam::Enemy) Error = TEXT("Invalid team.");
    else if (!Data.DataTable || Data.RowName.IsNone() || Data.DataTable->GetRowStruct() != FUnitDefinition::StaticStruct())
        Error = TEXT("Select a UnitDefinition data table and row.");
    const FUnitDefinition* Row = Error.IsEmpty() ? Data.DataTable->FindRow<FUnitDefinition>(Data.RowName, TEXT("InitializeUnit"), false) : nullptr;
    if (Error.IsEmpty() && !Row) Error = TEXT("Unit row does not exist.");
    if (Row && (Row->Cost < 1 || Row->Cost > 5)) Error = TEXT("Unit cost must be 1 to 5.");
    if (Row)
    {
        TSet<FName> SkillIds;
        for (const FUnitSkillDefinition& Skill : Row->Skills)
        {
            if (Skill.SkillId.IsNone() || SkillIds.Contains(Skill.SkillId)) Error = TEXT("Every skill needs a unique non-empty SkillId.");
            SkillIds.Add(Skill.SkillId);
        }
    }
    if (!Error.IsEmpty()) { LastValidationMessage = Error; return false; }
    // 종류 교체와 승급은 다른 작업이다. 종류 교체는 소유 상태를 새로 만들고 승급은 성장/아이템을 보존한다.
    UnitData = Data; UnitID = Data.RowName; Definition = *Row; TeamType = Team; StarLevel = Stars;
    bInitialized = true; bIsDead = false; bCombatStartedForRound = false;
    Upgrades.Empty(); PermanentGrowth = FUnitStatModifier(); GrowthThisCombat.Empty();
    ActiveModifiers.Empty(); Shields.Empty(); AttackEnhancements.Empty(); AppliedDamage.Empty();
    SkillRuntime.SetNum(Definition.Skills.Num());
    for (FSkillRuntime& Runtime : SkillRuntime) Runtime = FSkillRuntime();
    FinalStats = Definition.BaseStats;
    CurrentHP = FMath::Max(1.f, Safe(FinalStats.MaxHealth, 1.f));
    CurrentMana = Positive(Definition.BaseStats.StartMana);
    RecalculateStats();
    CurrentMana = UsesMana() ? FMath::Clamp(Positive(FinalStats.StartMana), 0.f, FinalStats.MaxMana) : 0.f;
    ActionState = EUnitActionState::Idle;
    SelectionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetActorHiddenInGame(false);
    ApplyVisuals();
    OnManaChanged.Broadcast(this);
    return true;
}

bool ABattleUnitBase::RefreshUnitFromData()
{
    FString Error;
    return InitializeUnit(UnitData, TeamType, StarLevel, Error);
}

void ABattleUnitBase::RecalculateStats()
{
    if (!bInitialized) return;
    const float OldMax = FMath::Max(1.f, Safe(FinalStats.MaxHealth, 1.f));
    const float HealthRatio = Fraction(CurrentHP / OldMax);
    FinalStats = Definition.BaseStats;
    FinalStats.MaxHealth = Positive(FinalStats.MaxHealth) * StarMultiplier(Definition.StarHealthMultipliers, StarLevel);
    FinalStats.AttackDamage = Positive(FinalStats.AttackDamage) * StarMultiplier(Definition.StarAttackMultipliers, StarLevel);
    float SpeedBonus = OvertimeAttackSpeed, ArmorReduction = 0.f, MRReduction = 0.f, ADReduction = 0.f, APReduction = 0.f;
    AttackSlow = 0.f; DamageIncrease = 0.f; DamageRemainingRatio = 1.f; bManaBlocked = false;
    const auto Add = [&](const FUnitStatModifier& Mod)
    {
        FinalStats.MaxHealth += Safe(Mod.MaxHealth);
        FinalStats.AttackDamage += Safe(Mod.AttackDamage);
        FinalStats.AbilityPower += Safe(Mod.AbilityPower);
        FinalStats.Armor += Safe(Mod.Armor);
        FinalStats.MagicResistance += Safe(Mod.MagicResistance);
        FinalStats.CriticalChance += Safe(Mod.CriticalChance);
        FinalStats.CriticalMultiplier += Safe(Mod.CriticalMultiplier);
        SpeedBonus += Safe(Mod.AttackSpeedBonus);
        AttackSlow = FMath::Max(AttackSlow, Fraction(Mod.AttackSpeedSlow));
        ArmorReduction = FMath::Max(ArmorReduction, Fraction(Mod.ArmorReduction));
        MRReduction = FMath::Max(MRReduction, Fraction(Mod.MagicResistanceReduction));
        ADReduction = FMath::Max(ADReduction, Fraction(Mod.AttackDamageReduction));
        APReduction = FMath::Max(APReduction, Fraction(Mod.AbilityPowerReduction));
        DamageIncrease += Safe(Mod.DamageIncrease);
        DamageRemainingRatio *= 1.f - Fraction(Mod.DamageReduction);
        bManaBlocked |= Mod.bBlockManaGain;
    };
    // 영구 성장은 승급 배율 적용 후 더한다. 장비와 일시 효과도 원본을 변경하지 않는다.
    Add(PermanentGrowth);
    for (const auto& Pair : Upgrades) Add(Pair.Value);
    for (const FUnitTimedModifier& Effect : ActiveModifiers) Add(Effect.Modifier);
    FinalStats.MaxHealth = FMath::Max(1.f, Safe(FinalStats.MaxHealth, 1.f));
    FinalStats.AttackDamage = Positive(FinalStats.AttackDamage) * (1.f - ADReduction);
    FinalStats.AbilityPower = Positive(FinalStats.AbilityPower) * (1.f - APReduction);
    FinalStats.Armor = Safe(FinalStats.Armor) * (1.f - ArmorReduction);
    FinalStats.MagicResistance = Safe(FinalStats.MagicResistance) * (1.f - MRReduction);
    FinalStats.AttackSpeed = FMath::Clamp(Positive(FinalStats.AttackSpeed) * FMath::Max(0.f, 1.f + Safe(SpeedBonus)) * (1.f - AttackSlow), 0.f, FMath::Max(0.01f, Safe(CombatPolicy.MaxAttackSpeed, 5.f)));
    FinalStats.AttackRange = FMath::Max(0, FinalStats.AttackRange);
    FinalStats.MovementSpeed = Positive(FinalStats.MovementSpeed);
    FinalStats.CriticalChance = Fraction(FinalStats.CriticalChance);
    FinalStats.CriticalMultiplier = FMath::Max(1.f, Safe(FinalStats.CriticalMultiplier, 1.5f));
    FinalStats.MaxMana = Positive(FinalStats.MaxMana);
    FinalStats.StartMana = FMath::Clamp(Positive(FinalStats.StartMana), 0.f, FinalStats.MaxMana);
    CurrentHP = bIsDead ? 0.f : FinalStats.MaxHealth * HealthRatio;
    CurrentMana = UsesMana() ? FMath::Clamp(Positive(CurrentMana), 0.f, FinalStats.MaxMana) : 0.f;
    OnStatsChanged.Broadcast(this); OnHealthChanged.Broadcast(this); OnManaChanged.Broadcast(this);
}

bool ABattleUnitBase::SetStarLevel(int32 Stars)
{
    if (!bInitialized || bInCombat || Stars < 1 || Stars > 2) return false;
    StarLevel = Stars; RecalculateStats(); return true;
}
void ABattleUnitBase::SetTeamType(EBattleTeam Team)
{
    if (bInCombat || IsValid(CurrentTile) || (Placement == EUnitPlacement::Waiting && Team != EBattleTeam::Player)) return;
    if (Team == EBattleTeam::Player || Team == EBattleTeam::Enemy) TeamType = Team;
}
bool ABattleUnitBase::ApplyUpgrade(FName SourceId, const FUnitStatModifier& Modifier)
{
    if (!bInitialized || bInCombat || SourceId.IsNone() || Modifier.bStun) return false;
    Upgrades.Add(SourceId, Modifier); RecalculateStats(); return true;
}
bool ABattleUnitBase::RemoveUpgrade(FName SourceId)
{
    if (bInCombat || Upgrades.Remove(SourceId) == 0) return false;
    RecalculateStats(); return true;
}
float ABattleUnitBase::AddPermanentGrowth(FName SourceId, EUnitGrowthStat Stat, float Amount, float LimitPerCombat)
{
    if (!bInCombat || !IsAlive() || SourceId.IsNone()) return 0.f;
    const float Granted = FMath::Min(Positive(Amount), FMath::Max(0.f, Positive(LimitPerCombat) - GrowthThisCombat.FindRef(SourceId)));
    if (Granted <= 0.f) return 0.f;
    GrowthThisCombat.FindOrAdd(SourceId) += Granted;
    switch (Stat)
    {
        case EUnitGrowthStat::MaxHealth: PermanentGrowth.MaxHealth += Granted; break;
        case EUnitGrowthStat::AttackDamage: PermanentGrowth.AttackDamage += Granted; break;
        case EUnitGrowthStat::AbilityPower: PermanentGrowth.AbilityPower += Granted; break;
        case EUnitGrowthStat::Armor: PermanentGrowth.Armor += Granted; break;
        case EUnitGrowthStat::MagicResistance: PermanentGrowth.MagicResistance += Granted; break;
    }
    RecalculateStats(); return Granted;
}

void ABattleUnitBase::ClearVisuals()
{
    // 메시를 바꿔도 머티리얼 오버라이드는 남을 수 있으므로 반드시 비운다.
    Body->Stop();
    Body->SetAnimInstanceClass(nullptr);
    Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Body->SetAnimation(nullptr);
    Body->EmptyOverrideMaterials();
    Body->SetSkeletalMesh(nullptr);
    Body->SetRelativeTransform(FTransform::Identity);
    VisualRoot->SetRelativeTransform(FTransform::Identity);
    for (UStaticMeshComponent* Weapon : {MainHand.Get(), OffHand.Get()})
    {
        Weapon->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        Weapon->SetStaticMesh(nullptr);
        Weapon->EmptyOverrideMaterials();
        Weapon->SetRelativeTransform(FTransform::Identity);
        Weapon->SetVisibility(false);
        Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void ABattleUnitBase::ApplyWeapon(UStaticMeshComponent* Component, const FUnitWeaponVisual& Visual)
{
    if (!Visual.Mesh) return;
    if (!Body->GetSkeletalMeshAsset() || Visual.SocketName.IsNone() || !Body->DoesSocketExist(Visual.SocketName))
    {
        LastValidationMessage += FString::Printf(TEXT("Missing weapon socket: %s. "), *Visual.SocketName.ToString());
        return; // 잘못된 소켓이면 무기를 숨기고 설정 오류를 표시한다.
    }
    Component->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Visual.SocketName);
    Component->SetStaticMesh(Visual.Mesh);
    Component->SetRelativeTransform(Visual.RelativeTransform);
    Component->SetVisibility(true);
}

void ABattleUnitBase::ApplyVisuals()
{
    ClearVisuals();
    LastValidationMessage.Reset();
    if (!bInitialized) return;
    const FUnitVisualDefinition& Visuals = Definition.Visuals;
    VisualRoot->SetRelativeTransform(Visuals.VisualTransform);
    Body->SetSkeletalMesh(Visuals.SkeletalMesh);
    if (!Visuals.SkeletalMesh) LastValidationMessage += TEXT("No skeletal mesh assigned. ");
    for (int32 Index = 0; Index < Visuals.MaterialOverrides.Num(); ++Index)
    {
        if (Visuals.MaterialOverrides[Index] && Index < Body->GetNumMaterials())
            Body->SetMaterial(Index, Visuals.MaterialOverrides[Index]);
    }
    ApplyWeapon(MainHand, Visuals.MainHand);
    ApplyWeapon(OffHand, Visuals.OffHand);
    const FUnitAnimationDefinition& Animations = Definition.Animations;
    if (Animations.AnimClass)
    {
        Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        Body->SetAnimInstanceClass(Animations.AnimClass);
    }
    else if (Animations.IdleAnimation && Visuals.SkeletalMesh)
    {
        Body->PlayAnimation(Animations.IdleAnimation, true);
    }
    OnVisualsApplied.Broadcast(this);
}


bool ABattleUnitBase::IsAlive() const { return bInitialized && !bIsDead && CurrentHP > 0.f; }
bool ABattleUnitBase::UsesMana() const
{
    return Definition.Skills.ContainsByPredicate([](const FUnitSkillDefinition& S) { return S.Trigger == EUnitTrigger::ManaFull; });
}
bool ABattleUnitBase::IsStunned() const
{
    return ActiveModifiers.ContainsByPredicate([](const FUnitTimedModifier& E) { return E.Modifier.bStun && E.RemainingSeconds > 0.f; });
}
int32 ABattleUnitBase::HexDistance(const ABoardTile* A, const ABoardTile* B)
{
    if (!IsValid(A) || !IsValid(B)) return MAX_int32;
    const int64 DQ = int64(A->HexQ) - B->HexQ, DR = int64(A->HexR) - B->HexR;
    return int32(FMath::Min<int64>(MAX_int32, (FMath::Abs(DQ) + FMath::Abs(DR) + FMath::Abs(DQ + DR)) / 2));
}

bool ABattleUnitBase::SetPreparationPlacement(FTransform Transform, ABoardTile* Tile, EUnitPlacement NewPlacement)
{
    if (!IsAlive() || bInCombat || Transform.ContainsNaN()) return false;
    if (NewPlacement == EUnitPlacement::Board)
    {
        if (!IsValid(Tile)) return false;
        const EBoardTileTeam Required = TeamType == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
        if (Tile->TeamType != Required || (Tile->OccupiedUnit != this && !Tile->CanPlaceUnit())) return false;
        Tile->SetOccupiedUnit(this);
        if (Tile->OccupiedUnit != this) return false;
        Transform.SetLocation(Tile->GetSurfaceLocation() + FVector(0, 0, 1.f));
    }
    else
    {
        if (NewPlacement == EUnitPlacement::Waiting && TeamType != EBattleTeam::Player) return false;
        if (IsValid(CurrentTile)) CurrentTile->ClearTile();
    }
    // 스케일은 모델 보정에 쓰지 않는다. 준비 위치는 논리적 Transform으로 보관한다.
    Transform.SetScale3D(FVector::OneVector);
    SetActorTransform(Transform);
    Placement = NewPlacement; bIsDeployed = NewPlacement == EUnitPlacement::Board;
    PreparationTransform = Transform; PreparationTile = Tile; PreparationPlacement = NewPlacement;
    return true;
}
bool ABattleUnitBase::PlaceOnTile(ABoardTile* Tile) { return SetPreparationPlacement(GetActorTransform(), Tile, EUnitPlacement::Board); }
bool ABattleUnitBase::SetWaitingPlacement(FVector Location)
{
    FTransform Transform = GetActorTransform(); Transform.SetLocation(Location);
    return SetPreparationPlacement(Transform, nullptr, EUnitPlacement::Waiting);
}
void ABattleUnitBase::SetCombatRoster(const TArray<ABattleUnitBase*>& Units)
{
    if (bInCombat) return;
    CombatRoster.Empty();
    for (ABattleUnitBase* Unit : Units) if (IsValid(Unit)) CombatRoster.AddUnique(Unit);
}
void ABattleUnitBase::StartCombat()
{
    if (!IsAlive() || Placement != EUnitPlacement::Board || !IsValid(CurrentTile) || bInCombat || bCombatStartedForRound) return;
    bCombatStartedForRound = true; bInCombat = true; ++CombatGeneration;
    PreparationTransform = GetActorTransform(); PreparationTile = CurrentTile; PreparationPlacement = Placement;
    ActiveModifiers.Empty(); Shields.Empty(); AttackEnhancements.Empty(); AppliedDamage.Empty(); GrowthThisCombat.Empty();
    for (FSkillRuntime& Runtime : SkillRuntime) Runtime = FSkillRuntime();
    BasicAttackCount = BasicHitCount = 0; AttackCooldown = RetargetTimer = BlockedSeconds = 0.f;
    UnreachableTargets.Empty(); CurrentTarget = nullptr;
    RecalculateStats(); CurrentHP = FinalStats.MaxHealth; CurrentMana = UsesMana() ? FinalStats.StartMana : 0.f;
    RefreshShieldTotal(); OnHealthChanged.Broadcast(this); OnManaChanged.Broadcast(this);
    // 전원이 StartCombat을 받은 뒤 첫 Tick에서 시작 효과를 한 번 적용한다.
    bCombatStartPending = true;
}
void ABattleUnitBase::StopCombat()
{
    const bool bWasInCombat = bInCombat;
    bInCombat = false; bCombatStartPending = false; ++CombatGeneration;
    CancelAction(); CurrentTarget = nullptr;
    // 전원 정지 후 준비 위치를 복구할 수 있게 전투 타일 점유를 먼저 반환한다.
    if (bWasInCombat && IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    TArray<FPendingProjectile> Removed = MoveTemp(Projectiles);
    for (const FPendingProjectile& Shot : Removed) ProjectileRemoved(Shot.Id, false);
    ActiveModifiers.Empty(); Shields.Empty(); AttackEnhancements.Empty();
    OvertimeAttackSpeed = 0.f; OvertimeDamageMultiplier = 1.f;
    for (FSkillRuntime& Runtime : SkillRuntime) Runtime = FSkillRuntime();
    BasicAttackCount = BasicHitCount = 0; AttackCooldown = 0.f;
    RefreshShieldTotal(); RecalculateStats();
}
bool ABattleUnitBase::ResetForRound()
{
    if (!bInitialized || bResolvingDamage || bResetting) return false;
    TGuardValue<bool> Guard(bResetting, true);
    StopCombat();
    if (IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    // 전원 StopCombat 후 호출한다. 원래 자리가 점유 중이면 강제로 덮어쓰지 않고 재시도하게 한다.
    if (PreparationPlacement == EUnitPlacement::Board && (!IsValid(PreparationTile) || !PreparationTile->CanPlaceUnit())) return false;
    bIsDead = false; bCombatStartedForRound = false; AppliedDamage.Empty(); GrowthThisCombat.Empty();
    SelectionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetActorHiddenInGame(false); ChangeAction(EUnitActionState::Idle);
    RecalculateStats(); CurrentHP = FinalStats.MaxHealth; CurrentMana = UsesMana() ? FinalStats.StartMana : 0.f;
    SetActorTransform(PreparationTransform);
    Placement = PreparationPlacement; bIsDeployed = Placement == EUnitPlacement::Board;
    if (bIsDeployed) PreparationTile->SetOccupiedUnit(this);
    ApplyVisuals(); OnHealthChanged.Broadcast(this); OnManaChanged.Broadcast(this);
    return true;
}
void ABattleUnitBase::SetOvertimeBonuses(float AttackSpeedBonus, float DamageBonus)
{
    if (!bInCombat) return;
    OvertimeAttackSpeed = Positive(AttackSpeedBonus); OvertimeDamageMultiplier = 1.f + Positive(DamageBonus);
    RecalculateStats();
}

void ABattleUnitBase::ChangeAction(EUnitActionState State)
{
    if (bIsDead && State != EUnitActionState::Dead) return;
    if (ActionState == State) return;
    ActionState = State; OnStateChanged.Broadcast(this);
}
void ABattleUnitBase::ReleaseMove(bool bCompleted)
{
    ABoardTile* Tile = ReservedTile;
    ReservedTile = nullptr;
    if (!bCompleted) SetActorLocation(MoveStart);
    if (IsValid(Tile)) MovementReservationReleased(Tile, ActiveActionId, bCompleted);
}
void ABattleUnitBase::CancelAction(bool bRefundMana)
{
    if (ActionState == EUnitActionState::Moving) ReleaseMove(false);
    const bool bRefund = bRefundMana && ActionState == EUnitActionState::Casting && !bActionCommitted;
    ActiveActionId = 0; ActiveSkillIndex = INDEX_NONE; ActionTarget.Reset(); bActionCommitted = false;
    if (UAnimInstance* Anim = Body->GetAnimInstance()) Anim->Montage_Stop(0.1f);
    ChangeAction(bIsDead ? EUnitActionState::Dead : EUnitActionState::Idle);
    if (bRefund) { CurrentMana = FMath::Min(PreCastMana, FinalStats.MaxMana); OnManaChanged.Broadcast(this); }
}
void ABattleUnitBase::Die()
{
    if (bIsDead) return;
    bIsDead = true; CurrentHP = 0.f;
    CancelAction(); CurrentTarget = nullptr;
    if (IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    SelectionCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (Definition.Animations.DeathMontage)
        if (UAnimInstance* Anim = Body->GetAnimInstance()) Anim->Montage_Play(Definition.Animations.DeathMontage);
    // Actor는 파괴하지 않는다. 이미 발사한 탄은 유지하고 전투 종료 때 일괄 취소한다.
    OnUnitDied.Broadcast(this);
}
float ABattleUnitBase::GetSelectionRadius() const { return SelectionCollision->GetScaledCapsuleRadius(); }
float ABattleUnitBase::GetHealthBarHeight() const
{
    return SelectionCollision->GetRelativeLocation().Z + SelectionCollision->GetScaledCapsuleHalfHeight() + 45.f;
}
void ABattleUnitBase::EndPlay(const EEndPlayReason::Type Reason)
{
    StopCombat();
    if (IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    Super::EndPlay(Reason);
}

void ABattleUnitBase::RefreshShieldTotal()
{
    CurrentShield = 0.f;
    for (const FUnitShield& Shield : Shields) CurrentShield += Shield.Amount;
    OnShieldChanged.Broadcast(this);
}
bool ABattleUnitBase::AddShield(FName EffectId, float Amount, float Duration)
{
    if (!bInCombat || !IsAlive() || EffectId.IsNone() || Positive(Amount) <= 0.f || Positive(Duration) <= 0.f) return false;
    FUnitShield* Existing = Shields.FindByPredicate([&](const FUnitShield& S) { return S.EffectId == EffectId; });
    if (Existing) { Existing->Amount = FMath::Max(Existing->Amount, Positive(Amount)); Existing->RemainingSeconds = Positive(Duration); }
    else { FUnitShield S; S.EffectId = EffectId; S.Amount = Positive(Amount); S.RemainingSeconds = Positive(Duration); S.Order = ++ShieldSequence; Shields.Add(S); }
    RefreshShieldTotal(); return true;
}
bool ABattleUnitBase::AddModifier(FName EffectId, const FUnitStatModifier& Modifier, float Duration)
{
    if (!bInCombat || !IsAlive() || EffectId.IsNone() || Positive(Duration) <= 0.f) return false;
    // 복합 효과에서 보스의 기절 시간만 절반으로 줄이고, 공속 감소 등은 원래 시간을 유지한다.
    const bool bHasOtherStats = Modifier.MaxHealth != 0.f || Modifier.AttackDamage != 0.f || Modifier.AbilityPower != 0.f
        || Modifier.Armor != 0.f || Modifier.MagicResistance != 0.f || Modifier.AttackSpeedBonus != 0.f
        || Modifier.AttackSpeedSlow != 0.f || Modifier.DamageIncrease != 0.f || Modifier.DamageReduction != 0.f
        || Modifier.ArmorReduction != 0.f || Modifier.MagicResistanceReduction != 0.f
        || Modifier.AttackDamageReduction != 0.f || Modifier.AbilityPowerReduction != 0.f
        || Modifier.CriticalChance != 0.f || Modifier.CriticalMultiplier != 0.f || Modifier.bBlockManaGain;
    if (Definition.bIsBoss && Modifier.bStun && bHasOtherStats)
    {
        FUnitStatModifier StatsOnly = Modifier; StatsOnly.bStun = false;
        FUnitStatModifier StunOnly; StunOnly.bStun = true;
        const bool bStats = AddModifier(EffectId, StatsOnly, Duration);
        const bool bStun = AddModifier(FName(*(EffectId.ToString() + TEXT(".Stun"))), StunOnly, Duration);
        return bStats && bStun;
    }
    FUnitStatModifier Applied = Modifier;
    float Seconds = Positive(Duration);
    if (Applied.bStun && Definition.bIsBoss) Seconds *= 0.5f;
    FUnitTimedModifier* Existing = ActiveModifiers.FindByPredicate([&](const FUnitTimedModifier& E) { return E.EffectId == EffectId; });
    if (Existing)
    {
        Existing->RemainingSeconds = Applied.bStun || Applied.AttackSpeedSlow > 0.f
            ? FMath::Max(Existing->RemainingSeconds, Seconds) : Seconds;
        Existing->Modifier = Applied;
    }
    else { FUnitTimedModifier E; E.EffectId = EffectId; E.RemainingSeconds = Seconds; E.Modifier = Applied; ActiveModifiers.Add(E); }
    RecalculateStats();
    if (IsStunned())
    {
        if (ActionState == EUnitActionState::Moving || (ActionState == EUnitActionState::BasicAttack && !bActionCommitted)) CancelAction();
        else if (ActionState == EUnitActionState::Casting && Definition.Skills.IsValidIndex(ActiveSkillIndex))
        {
            const FUnitSkillDefinition& Skill = Definition.Skills[ActiveSkillIndex];
            if (Skill.bInterruptibleByStun) CancelAction(Skill.bRefundManaWhenInterrupted);
        }
    }
    return true;
}
bool ABattleUnitBase::RemoveEffect(FName EffectId)
{
    const FName StunPart(*(EffectId.ToString() + TEXT(".Stun")));
    const int32 Removed = ActiveModifiers.RemoveAll([&](const FUnitTimedModifier& E) { return E.EffectId == EffectId || E.EffectId == StunPart; })
        + Shields.RemoveAll([&](const FUnitShield& S) { return S.EffectId == EffectId; })
        + AttackEnhancements.RemoveAll([&](const FAttackEnhancement& E) { return E.Id == EffectId; });
    if (Removed == 0) return false;
    RefreshShieldTotal(); RecalculateStats(); return true;
}
float ABattleUnitBase::GainMana(float Amount)
{
    if (!bInCombat || !IsAlive() || !UsesMana() || bManaBlocked || ActionState == EUnitActionState::Casting) return 0.f;
    const float OldMana = CurrentMana;
    CurrentMana = FMath::Clamp(CurrentMana + Positive(Amount), 0.f, FinalStats.MaxMana);
    if (CurrentMana != OldMana) OnManaChanged.Broadcast(this);
    return CurrentMana - OldMana;
}
float ABattleUnitBase::ReceiveHealing(float Amount, ABattleUnitBase* Healer)
{
    if (!bInCombat || !IsAlive() || Positive(Amount) <= 0.f) return 0.f;
    const float Actual = FMath::Min(Positive(Amount), FMath::Max(0.f, FinalStats.MaxHealth - CurrentHP));
    CurrentHP += Actual;
    if (Actual > 0.f) { OnHealthChanged.Broadcast(this); OnHealed.Broadcast(this, Healer, Actual); }
    return Actual;
}
FUnitDamageRequest ABattleUnitBase::MakeDamage(float Raw, EUnitDamageType Type, EUnitDamageSource Source, FName SkillId, int32 ActionId, bool bRollCritical) const
{
    FUnitDamageRequest Request = TriggerContext;
    Request.Attacker = const_cast<ABattleUnitBase*>(this); Request.RawDamage = Positive(Raw); Request.Type = Type;
    Request.Source = Source; Request.SkillId = SkillId; Request.ActionId = ActionId; Request.DamageId = FGuid::NewGuid();
    Request.bCritical = bRollCritical && FMath::FRand() < FinalStats.CriticalChance;
    Request.OffenseMultiplier = (Request.bCritical ? FinalStats.CriticalMultiplier : 1.f)
        * FMath::Max(0.f, 1.f + Safe(DamageIncrease)) * OvertimeDamageMultiplier;
    return Request;
}
FUnitDamageResult ABattleUnitBase::ReceiveDamage(const FUnitDamageRequest& Request)
{
    FUnitDamageResult Result;
    if (!bInCombat || !IsAlive() || bResolvingDamage || !FMath::IsFinite(Request.RawDamage) || Request.RawDamage <= 0.f
        || !FMath::IsFinite(Request.OffenseMultiplier) || Request.OffenseMultiplier < 0.f
        || Request.TriggerDepth > FMath::Clamp(CombatPolicy.MaxTriggerDepth, 0, 32)
        || !Request.DamageId.IsValid() || AppliedDamage.Contains(Request.DamageId)) return Result;
    TGuardValue<bool> Guard(bResolvingDamage, true);
    if (Request.DamageId.IsValid()) AppliedDamage.Add(Request.DamageId);
    float Damage = Request.RawDamage * Request.OffenseMultiplier;
    if (Request.Type != EUnitDamageType::TrueDamage)
        Damage *= ResistanceFactor(Request.Type == EUnitDamageType::Physical ? FinalStats.Armor : FinalStats.MagicResistance, CombatPolicy.NegativeResistance);
    Damage = Positive(Damage * DamageRemainingRatio);
    Result.bApplied = true; Result.bCritical = Request.bCritical; Result.MitigatedDamage = Damage;
    // 같은 스킬은 하나의 보호막으로 병합된다. 서로 다른 보호막은 개별 만료/소모한다.
    Shields.Sort([&](const FUnitShield& A, const FUnitShield& B)
    {
        if (CombatPolicy.ShieldOrder == EUnitShieldOrder::EarliestExpiry && A.RemainingSeconds != B.RemainingSeconds) return A.RemainingSeconds < B.RemainingSeconds;
        return A.Order < B.Order;
    });
    for (FUnitShield& Shield : Shields)
    {
        const float Absorbed = FMath::Min(Shield.Amount, Damage);
        Shield.Amount -= Absorbed; Damage -= Absorbed; Result.ShieldDamage += Absorbed;
        if (Damage <= 0.f) break;
    }
    Shields.RemoveAll([](const FUnitShield& S) { return S.Amount <= 0.f; });
    Result.HealthDamage = FMath::Min(CurrentHP, Damage);
    CurrentHP = FMath::Max(0.f, CurrentHP - Result.HealthDamage);
    Result.bKilled = CurrentHP <= 0.f;
    if (Result.bKilled) Die();
    // 원자적 체력 갱신을 마친 뒤 트리거의 추가 피해는 별도 요청으로 허용한다.
    bResolvingDamage = false;
    RefreshShieldTotal(); OnHealthChanged.Broadcast(this);
    // 보호막 피해도 피격 마나에 포함한다. 과잉 살상 피해는 포함하지 않는다.
    if (IsAlive()) GainMana(FMath::Min((Result.ShieldDamage + Result.HealthDamage) * Positive(CombatPolicy.DamageTakenManaRatio), Positive(CombatPolicy.DamageTakenManaCapPerHit)));
    OnDamageReceived.Broadcast(this, Request, Result);
    if (bInCombat && IsAlive()) DispatchTrigger(EUnitTrigger::DamageTaken, Request);
    ABattleUnitBase* Source = Request.Attacker;
    if (IsValid(Source) && Source->bInCombat && Source->IsAlive())
    {
        Source->DispatchTrigger(EUnitTrigger::DamageDealt, Request);
        if (Result.bKilled) Source->DispatchTrigger(EUnitTrigger::Kill, Request);
    }
    return Result;
}

void ABattleUnitBase::TickEffects(float Delta)
{
    bool bStatsChanged = false, bShieldsChanged = false;
    for (FUnitTimedModifier& Effect : ActiveModifiers) Effect.RemainingSeconds -= Delta;
    bStatsChanged = ActiveModifiers.RemoveAll([](const FUnitTimedModifier& E) { return E.RemainingSeconds <= 0.f; }) > 0;
    for (FUnitShield& Shield : Shields) Shield.RemainingSeconds -= Delta;
    bShieldsChanged = Shields.RemoveAll([](const FUnitShield& S) { return S.RemainingSeconds <= 0.f; }) > 0;
    for (FAttackEnhancement& E : AttackEnhancements) E.Remaining -= Delta;
    AttackEnhancements.RemoveAll([](const FAttackEnhancement& E) { return E.Remaining <= 0.f; });
    for (FSkillRuntime& Runtime : SkillRuntime) Runtime.Cooldown = FMath::Max(0.f, Runtime.Cooldown - Delta);
    if (bStatsChanged) RecalculateStats();
    if (bShieldsChanged) RefreshShieldTotal();
}
void ABattleUnitBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bInCombat || DeltaSeconds <= 0.f || !FMath::IsFinite(DeltaSeconds)) return;
    TickProjectiles(DeltaSeconds);
    if (!bInCombat || !IsAlive()) return;
    if (bCombatStartPending)
    {
        bCombatStartPending = false;
        DispatchTrigger(EUnitTrigger::CombatStart, FUnitDamageRequest());
        if (!bInCombat || !IsAlive()) return;
    }
    TickEffects(DeltaSeconds);
    AttackCooldown = FMath::Max(0.f, AttackCooldown - DeltaSeconds);
    TickAction(DeltaSeconds);
    if (bInCombat && IsAlive()) Think(DeltaSeconds);
}

bool ABattleUnitBase::IsEnemyTarget(const ABattleUnitBase* Target) const
{
    return IsValid(Target) && Target != this && Target->TeamType != TeamType && Target->IsAlive() && Target->bInCombat
        && Target->Placement == EUnitPlacement::Board && IsValid(Target->CurrentTile);
}
bool ABattleUnitBase::IsInRange(const ABattleUnitBase* Target, int32 Range) const
{
    return IsValid(Target) && HexDistance(CurrentTile, Target->CurrentTile) <= FMath::Max(0, Range);
}
void ABattleUnitBase::FindTarget()
{
    ABattleUnitBase* OldTarget = CurrentTarget;
    CurrentTarget = nullptr;
    int32 BestDistance = MAX_int32;
    // 동률은 명단 순서로 해결한다. GameMode를 매 프레임 검색하거나 전체 Actor를 순회하지 않는다.
    for (int32 Index = 0; Index < CombatRoster.Num(); ++Index)
    {
        ABattleUnitBase* Unit = CombatRoster[Index];
        if (!IsEnemyTarget(Unit) || UnreachableTargets.Contains(Index)) continue;
        const int32 Distance = HexDistance(CurrentTile, Unit->CurrentTile);
        if (Distance < BestDistance) { BestDistance = Distance; CurrentTarget = Unit; }
    }
    if (OldTarget != CurrentTarget)
    {
        for (int32 Index = 0; Index < Definition.Skills.Num(); ++Index)
            if (Definition.Skills[Index].bResetCountOnTargetChange && SkillRuntime.IsValidIndex(Index)) SkillRuntime[Index].Count = 0;
        BlockedSeconds = 0.f;
    }
}
void ABattleUnitBase::Think(float Delta)
{
    if (IsStunned() || ActionState != EUnitActionState::Idle) return;
    RetargetTimer -= Delta;
    if (!IsEnemyTarget(CurrentTarget))
    {
        CurrentTarget = nullptr;
        if (RetargetTimer > 0.f) return;
        UnreachableTargets.Empty(); FindTarget();
    }
    // 액티브는 현재 평타가 끝난 뒤 우선한다. 유효한 스킬 대상이 없으면 마나를 보존한다.
    if (TryCastSkill()) return;
    if (!IsEnemyTarget(CurrentTarget)) { RetargetTimer = FMath::Max(0.05f, Positive(CombatPolicy.RetargetInterval)); return; }
    if (IsInRange(CurrentTarget, FinalStats.AttackRange)) { BlockedSeconds = 0.f; TryBasicAttack(); return; }
    if (RetargetTimer > 0.f || FinalStats.MovementSpeed <= 0.f) return;
    const int32 MoveId = ++ActionSequence;
    ABoardTile* Next = nullptr;
    const EUnitPathResult Path = RequestCombatStep(CurrentTarget, MoveId, Next);
    RetargetTimer = FMath::Max(0.05f, Positive(CombatPolicy.RetargetInterval));
    if (!bInCombat || !IsAlive() || IsStunned() || ActionState != EUnitActionState::Idle)
    {
        if (Path == EUnitPathResult::ReservedStep && IsValid(Next)) MovementReservationReleased(Next, MoveId, false);
        return;
    }
    if (Path == EUnitPathResult::ReservedStep)
    {
        // 예약은 보드가 수행하고 유닛은 예약된 인접 빈 타일로만 이동한다.
        if (!IsValid(Next) || Next == CurrentTile || HexDistance(CurrentTile, Next) != 1 || !Next->CanPlaceUnit())
        {
            LastValidationMessage = TEXT("Board must reserve an empty adjacent tile before returning ReservedStep.");
            if (IsValid(Next)) MovementReservationReleased(Next, MoveId, false);
            return;
        }
        ActiveActionId = MoveId; ReservedTile = Next; MoveStart = GetActorLocation();
        BlockedSeconds = 0.f; ChangeAction(EUnitActionState::Moving);
    }
    else if (Path == EUnitPathResult::Unavailable)
        LastValidationMessage = TEXT("Connect RequestCombatStep to the board reservation/path API.");
    else
    {
        BlockedSeconds += RetargetTimer;
        if (Path == EUnitPathResult::Unreachable || BlockedSeconds >= Positive(CombatPolicy.BlockedTargetGraceSeconds))
        {
            const int32 Index = CombatRoster.IndexOfByKey(CurrentTarget);
            if (Index != INDEX_NONE) UnreachableTargets.Add(Index);
            FindTarget();
        }
    }
}

bool ABattleUnitBase::TryBasicAttack()
{
    if (!bInCombat || !IsAlive() || IsStunned() || ActionState != EUnitActionState::Idle || AttackCooldown > 0.f
        || FinalStats.AttackSpeed <= 0.f || !IsEnemyTarget(CurrentTarget) || !IsInRange(CurrentTarget, FinalStats.AttackRange)) return false;
    ActiveActionId = ++ActionSequence; ActiveSkillIndex = INDEX_NONE; ActionTarget = CurrentTarget;
    bActionCommitted = false; ActionElapsed = 0.f;
    AttackCooldown = 1.f / FMath::Max(0.01f, FinalStats.AttackSpeed);
    // 공속 감소는 대기 주기만 늘린다. 진행 중 모션은 시작 시 속도로 고정한다.
    const float MotionSpeed = AttackSlow < 1.f ? FinalStats.AttackSpeed / FMath::Max(0.01f, 1.f - AttackSlow) : FinalStats.AttackSpeed;
    ActionDuration = FMath::Clamp(Positive(Definition.BasicAttack.AnimationSecondsAtSpeedOne) / FMath::Max(0.01f, MotionSpeed), 0.01f, FMath::Max(0.01f, AttackCooldown));
    const int32 Id = ActiveActionId;
    ChangeAction(EUnitActionState::BasicAttack);
    FUnitDamageRequest Context; Context.ActionId = Id; Context.Attacker = this; Context.Victim = ActionTarget.Get();
    ++BasicAttackCount;
    DispatchTrigger(EUnitTrigger::AttackStarted, Context);
    if (!bInCombat || !IsAlive() || ActiveActionId != Id) return false;
    // 기본 공격은 물리 피해다. 구버전의 DamageType/bCanCrit 설정과 무관하게 공통 명세를 적용한다.
    ActiveAttackDamage = MakeDamage(FinalStats.AttackDamage * Positive(Definition.BasicAttack.AttackDamageRatio) + Positive(Definition.BasicAttack.FlatDamage),
        EUnitDamageType::Physical, EUnitDamageSource::BasicAttack, NAME_None, Id, true);
    if (Definition.Animations.AttackMontage)
        if (UAnimInstance* Anim = Body->GetAnimInstance())
            Anim->Montage_Play(Definition.Animations.AttackMontage, Definition.Animations.AttackMontage->GetPlayLength() / ActionDuration);
    OnAttackStarted.Broadcast(this, Id);
    return true;
}
bool ABattleUnitBase::CommitAttackHit(int32 ActionId)
{
    if (!bInCombat || !IsAlive() || IsStunned() || ActionState != EUnitActionState::BasicAttack
        || ActiveActionId != ActionId || ActionId <= 0 || bActionCommitted) return false;
    // 이벤트보다 먼저 판정을 확정해 같은 Notify가 재진입해도 중복 적용하지 않는다.
    bActionCommitted = true;
    ABattleUnitBase* Target = ActionTarget.Get();
    if (!IsEnemyTarget(Target) || !IsInRange(Target, FinalStats.AttackRange)) return false;
    FUnitDamageRequest Damage = ActiveAttackDamage;
    // 강화 기본 공격 전체는 치명타 배율을 공유한다. 별도 스킬 피해는 이 경로에 넣지 않는다.
    float ExtraMultiplier = 0.f, ExtraFlat = 0.f;
    for (const FAttackEnhancement& E : AttackEnhancements) { ExtraMultiplier += FMath::Max(0.f, E.Multiplier - 1.f); ExtraFlat += E.Flat; }
    AttackEnhancements.Empty();
    Damage.RawDamage = Positive(Damage.RawDamage * (1.f + ExtraMultiplier) + ExtraFlat);
    Damage.Victim = Target;
    if (Definition.BasicAttack.Delivery == EUnitAttackDelivery::Projectile)
    {
        FPendingProjectile Shot; Shot.bBasic = true; Shot.Target = Target; Shot.Damage = Damage;
        Shot.Speed = Definition.BasicAttack.ProjectileSpeed; Shot.Lifetime = Definition.BasicAttack.ProjectileMaxLifetime;
        LaunchProjectile(MoveTemp(Shot));
    }
    else
    {
        const FUnitDamageResult Result = Target->ReceiveDamage(Damage);
        ReportBasicHit(Target, Damage, Result);
    }
    return true;
}
void ABattleUnitBase::ReportBasicHit(ABattleUnitBase* Target, const FUnitDamageRequest& Request, const FUnitDamageResult& Result)
{
    if (!Result.bApplied || !bInCombat || !IsAlive()) return;
    ++BasicHitCount;
    GainMana(10.f); // 명세의 기본 공격 적중 마나. 원거리도 도착 시에만 한 번 지급한다.
    FUnitDamageRequest Context = Request; Context.Victim = Target;
    DispatchTrigger(EUnitTrigger::BasicAttackHit, Context);
}
bool ABattleUnitBase::FinishAction(int32 ActionId)
{
    if (ActionId <= 0 || ActionId != ActiveActionId || ActionState == EUnitActionState::Moving) return false;
    ActiveActionId = 0; ActiveSkillIndex = INDEX_NONE; ActionTarget.Reset();
    ChangeAction(bIsDead ? EUnitActionState::Dead : EUnitActionState::Idle);
    return true;
}
void ABattleUnitBase::TickAction(float Delta)
{
    const int32 Id = ActiveActionId;
    if (Id <= 0) return;
    if (ActionState == EUnitActionState::Moving)
    {
        if (!IsValid(ReservedTile)) { CancelAction(); return; }
        const FVector Goal = ReservedTile->GetSurfaceLocation() + FVector(0, 0, 1.f);
        SetActorLocation(FMath::VInterpConstantTo(GetActorLocation(), Goal, Delta, FinalStats.MovementSpeed));
        if (GetActorLocation().Equals(Goal, 0.1f))
        {
            if (!ReservedTile->CanPlaceUnit()) { CancelAction(); return; }
            ReservedTile->SetOccupiedUnit(this);
            const bool bCompleted = CurrentTile == ReservedTile;
            ReleaseMove(bCompleted);
            ActiveActionId = 0; ChangeAction(EUnitActionState::Idle);
        }
        return;
    }
    ActionElapsed += Delta;
    if (ActionState == EUnitActionState::BasicAttack)
    {
        if (!Definition.BasicAttack.bUseAnimNotify && !bActionCommitted && ActionElapsed >= ActionDuration * Fraction(Definition.BasicAttack.HitTimeRatio)) CommitAttackHit(Id);
        if (Id == ActiveActionId && ActionElapsed >= ActionDuration + Positive(CombatPolicy.NotifyTimeoutGraceSeconds))
        {
            if (!bActionCommitted) LastValidationMessage = TEXT("Attack Notify missing: action ended without dealing damage.");
            FinishAction(Id);
        }
    }
    else if (ActionState == EUnitActionState::Casting && Definition.Skills.IsValidIndex(ActiveSkillIndex))
    {
        const FUnitSkillDefinition Skill = Definition.Skills[ActiveSkillIndex];
        if (!Skill.bUseAnimNotify && !bActionCommitted && ActionElapsed >= ActionDuration) CommitSkillEffect(Id);
        if (Id == ActiveActionId && ActionElapsed >= ActionDuration + Positive(CombatPolicy.NotifyTimeoutGraceSeconds))
        {
            if (!bActionCommitted) LastValidationMessage = TEXT("Skill Notify missing: action ended without effect; mana was consumed at cast start.");
            FinishAction(Id);
        }
    }
}

ABattleUnitBase* ABattleUnitBase::SelectSkillTarget(const FUnitSkillDefinition& Skill) const
{
    if (Skill.Target == EUnitSkillTarget::Self) return IsAlive() ? const_cast<ABattleUnitBase*>(this) : nullptr;
    if (Skill.Target == EUnitSkillTarget::CurrentEnemy) return IsEnemyTarget(CurrentTarget) && IsInRange(CurrentTarget, Skill.CastRange) ? CurrentTarget.Get() : nullptr;
    ABattleUnitBase* Best = nullptr;
    float BestScore = MAX_flt;
    const auto Consider = [&](ABattleUnitBase* Unit)
    {
        if (!IsValid(Unit) || !Unit->IsAlive() || !Unit->bInCombat || Unit->Placement != EUnitPlacement::Board || !IsInRange(Unit, Skill.CastRange)) return;
        const bool bAlly = Unit->TeamType == TeamType;
        if ((Skill.Target == EUnitSkillTarget::LowestHealthAlly) != bAlly) return;
        const float Score = bAlly ? Unit->CurrentHP / Unit->FinalStats.MaxHealth : float(HexDistance(CurrentTile, Unit->CurrentTile));
        if (bAlly && Score >= 1.f) return;
        if (Score < BestScore) { Best = Unit; BestScore = Score; }
    };
    if (Skill.Target == EUnitSkillTarget::LowestHealthAlly) Consider(const_cast<ABattleUnitBase*>(this));
    for (ABattleUnitBase* Unit : CombatRoster) Consider(Unit);
    return Best;
}
bool ABattleUnitBase::TryCastSkill()
{
    if (!bInCombat || !IsAlive() || IsStunned() || ActionState != EUnitActionState::Idle || !UsesMana()
        || FinalStats.MaxMana <= 0.f || CurrentMana < FinalStats.MaxMana) return false;
    int32 Chosen = INDEX_NONE;
    ABattleUnitBase* Target = nullptr;
    for (int32 Index = 0; Index < Definition.Skills.Num(); ++Index)
    {
        const FUnitSkillDefinition& Skill = Definition.Skills[Index];
        if (Skill.Trigger != EUnitTrigger::ManaFull || !SkillRuntime.IsValidIndex(Index) || SkillRuntime[Index].Cooldown > 0.f) continue;
        ABattleUnitBase* Candidate = SelectSkillTarget(Skill);
        if (Candidate && (Chosen == INDEX_NONE || Skill.Priority > Definition.Skills[Chosen].Priority)) { Chosen = Index; Target = Candidate; }
    }
    if (Chosen == INDEX_NONE) return false;
    const FUnitSkillDefinition& Skill = Definition.Skills[Chosen];
    ActiveActionId = ++ActionSequence; ActiveSkillIndex = Chosen; ActionTarget = Target;
    ActionElapsed = 0.f; ActionDuration = FMath::Max(0.01f, Positive(Skill.CastSeconds)); bActionCommitted = false;
    PreCastMana = CurrentMana; CurrentMana = 0.f;
    SkillRuntime[Chosen].Cooldown = Positive(Skill.InternalCooldown);
    ChangeAction(EUnitActionState::Casting);
    if (Skill.CastMontage)
        if (UAnimInstance* Anim = Body->GetAnimInstance()) Anim->Montage_Play(Skill.CastMontage, Skill.CastMontage->GetPlayLength() / ActionDuration);
    OnManaChanged.Broadcast(this); OnSkillStarted.Broadcast(this, ActiveActionId);
    return true;
}
bool ABattleUnitBase::CommitSkillEffect(int32 ActionId)
{
    if (!bInCombat || !IsAlive() || ActionState != EUnitActionState::Casting || ActiveActionId != ActionId || ActionId <= 0
        || bActionCommitted || !Definition.Skills.IsValidIndex(ActiveSkillIndex)) return false;
    bActionCommitted = true;
    ABattleUnitBase* Target = ActionTarget.Get();
    // 시전 대상이 사망해도 자동 이전하지 않는다. 일반 시전 불발은 마나 미환불이 임시 정책이다.
    if (!IsValid(Target) || !Target->IsAlive() || !Target->bInCombat) return false;
    ExecuteSkill(Definition.Skills[ActiveSkillIndex], Target, ActionId);
    return true;
}
TArray<float> ABattleUnitBase::CalculateEffectValues(const FUnitSkillDefinition& Skill) const
{
    TArray<float> Values;
    for (const FUnitSkillEffect& Effect : Skill.Effects)
    {
        float Base = Safe(Effect.BaseValue);
        if (StarLevel == 2)
        {
            if (Effect.TwoStarBaseValue >= 0.f) Base = Safe(Effect.TwoStarBaseValue);
            else if (Effect.Kind == EUnitEffectKind::Damage || Effect.Kind == EUnitEffectKind::Healing || Effect.Kind == EUnitEffectKind::Shield || Effect.Kind == EUnitEffectKind::EmpowerNextAttack) Base *= 1.8f;
        }
        Values.Add(Positive(Base + Safe(Effect.AttackDamageRatio) * FinalStats.AttackDamage + Safe(Effect.AbilityPowerRatio) * FinalStats.AbilityPower));
    }
    return Values;
}
void ABattleUnitBase::ExecuteSkill(const FUnitSkillDefinition& Skill, ABattleUnitBase* Target, int32 ActionId)
{
    const TArray<float> Values = CalculateEffectValues(Skill);
    const FUnitDamageRequest Context = MakeDamage(0.f, EUnitDamageType::Magical,
        Skill.Trigger == EUnitTrigger::ManaFull ? EUnitDamageSource::Skill : EUnitDamageSource::Triggered, Skill.SkillId, ActionId, false);
    if (Skill.bProjectile && Target != this)
    {
        FPendingProjectile Shot; Shot.Target = Target; Shot.Skill = Skill; Shot.EffectValues = Values; Shot.Damage = Context;
        Shot.Speed = Skill.ProjectileSpeed; Shot.Lifetime = Skill.ProjectileMaxLifetime;
        LaunchProjectile(MoveTemp(Shot));
    }
    else ApplySkillEffects(Skill, Target, Values, Context);
}
void ABattleUnitBase::ApplySkillEffects(const FUnitSkillDefinition& Skill, ABattleUnitBase* Target, const TArray<float>& Values, const FUnitDamageRequest& Context)
{
    if (!IsValid(Target) || !Target->IsAlive() || !Target->bInCombat) return;
    TArray<TWeakObjectPtr<ABattleUnitBase>> Targets;
    Targets.Add(Target);
    if (Skill.AreaRadius > 0)
        for (ABattleUnitBase* Unit : CombatRoster)
            if (IsValid(Unit) && Unit != Target && Unit->TeamType == Target->TeamType && Unit->IsAlive() && Unit->bInCombat
                && HexDistance(Target->CurrentTile, Unit->CurrentTile) <= Skill.AreaRadius) Targets.AddUnique(Unit);
    for (int32 Index = 0; Index < Skill.Effects.Num(); ++Index)
    {
        const FUnitSkillEffect Effect = Skill.Effects[Index];
        const FName EffectId = Effect.EffectId.IsNone() ? FName(*FString::Printf(TEXT("%s.%d"), *Skill.SkillId.ToString(), Index)) : Effect.EffectId;
        TArray<TWeakObjectPtr<ABattleUnitBase>> Recipients = Targets;
        if (Effect.bApplyToSelf) { Recipients.Empty(); Recipients.Add(this); }
        for (TWeakObjectPtr<ABattleUnitBase> Recipient : Recipients)
        {
            ABattleUnitBase* Unit = Recipient.Get();
            if (!bInCombat || !IsValid(Unit) || !Unit->IsAlive() || !Unit->bInCombat) continue;
            const float Value = Values.IsValidIndex(Index) ? Values[Index] : 0.f;
            switch (Effect.Kind)
            {
                case EUnitEffectKind::Damage:
                {
                    FUnitDamageRequest Damage = Context; Damage.RawDamage = Value; Damage.Type = Effect.DamageType;
                    Damage.Victim = Unit; Damage.DamageId = FGuid::NewGuid(); Damage.bCritical = false;
                    Unit->ReceiveDamage(Damage); break;
                }
                case EUnitEffectKind::Healing: Unit->ReceiveHealing(Value, this); break;
                case EUnitEffectKind::Shield: Unit->AddShield(EffectId, Value, Effect.Duration); break;
                case EUnitEffectKind::Modifier:
                    Unit->AddModifier(EffectId, StarLevel == 2 && Effect.bOverrideModifierAtTwoStars ? Effect.TwoStarModifier : Effect.Modifier, Effect.Duration); break;
                case EUnitEffectKind::Mana: Unit->GainMana(Value); break;
                case EUnitEffectKind::PermanentGrowth: Unit->AddPermanentGrowth(EffectId, Effect.GrowthStat, Value, Effect.GrowthLimitPerCombat); break;
                case EUnitEffectKind::EmpowerNextAttack:
                {
                    Unit->AttackEnhancements.RemoveAll([&](const FAttackEnhancement& E) { return E.Id == EffectId; });
                    FAttackEnhancement Enhancement; Enhancement.Id = EffectId; Enhancement.Multiplier = FMath::Max(1.f, Safe(Effect.EmpowerAttackMultiplier, 1.f));
                    Enhancement.Flat = Value; Enhancement.Remaining = FMath::Max(0.01f, Positive(Effect.Duration));
                    Unit->AttackEnhancements.Add(Enhancement); break;
                }
            }
        }
    }
}
void ABattleUnitBase::DispatchTrigger(EUnitTrigger Trigger, const FUnitDamageRequest& Context)
{
    if (!bInCombat || !IsAlive() || Context.TriggerDepth >= FMath::Clamp(CombatPolicy.MaxTriggerDepth, 0, 32)) return;
    for (int32 Index = 0; Index < Definition.Skills.Num(); ++Index)
    {
        const FUnitSkillDefinition Skill = Definition.Skills[Index];
        if (Skill.Trigger != Trigger || Skill.Trigger == EUnitTrigger::ManaFull || !SkillRuntime.IsValidIndex(Index)
            || Context.TriggerChain.Contains(Skill.SkillId) || SkillRuntime[Index].Cooldown > 0.f) continue;
        ++SkillRuntime[Index].Count;
        if (SkillRuntime[Index].Count % FMath::Max(1, Skill.TriggerEvery) != 0) continue;
        ABattleUnitBase* Target = nullptr;
        if (Skill.Target == EUnitSkillTarget::CurrentEnemy)
        {
            ABattleUnitBase* Other = Trigger == EUnitTrigger::DamageTaken ? Context.Attacker.Get() : Context.Victim.Get();
            if (IsEnemyTarget(Other) && IsInRange(Other, Skill.CastRange)) Target = Other;
        }
        if (!Target) Target = SelectSkillTarget(Skill);
        if (!Target) continue;
        SkillRuntime[Index].Cooldown = Positive(Skill.InternalCooldown);
        FUnitDamageRequest NextContext = Context;
        ++NextContext.TriggerDepth; NextContext.TriggerChain.Add(Skill.SkillId);
        TGuardValue<FUnitDamageRequest> Guard(TriggerContext, NextContext);
        ExecuteSkill(Skill, Target, ++ActionSequence);
        if (!bInCombat || !IsAlive()) break;
    }
}

void ABattleUnitBase::LaunchProjectile(FPendingProjectile Projectile)
{
    if (!Projectile.Target.IsValid() || Positive(Projectile.Speed) <= 0.f || Positive(Projectile.Lifetime) <= 0.f)
    {
        LastValidationMessage = TEXT("Projectile needs a valid target, positive speed and lifetime."); return;
    }
    Projectile.Id = ++ActionSequence; Projectile.TargetGeneration = Projectile.Target->CombatGeneration;
    Projectile.Position = GetActorLocation() + FVector(0, 0, GetHealthBarHeight() * 0.5f);
    Projectile.Speed = Positive(Projectile.Speed); Projectile.Lifetime = Positive(Projectile.Lifetime);
    Projectiles.Add(Projectile);
    FUnitProjectileView View; View.ProjectileId = Projectile.Id; View.Position = Projectile.Position; View.Target = Projectile.Target.Get();
    ProjectileUpdated(View);
}
void ABattleUnitBase::TickProjectiles(float Delta)
{
    // 비행 목록을 분리해 명중 트리거가 새 탄을 생성해도 반복 중 배열이 손상되지 않게 한다.
    TArray<FPendingProjectile> Pending = MoveTemp(Projectiles);
    const int32 Generation = CombatGeneration;
    for (FPendingProjectile& Shot : Pending)
    {
        if (!bInCombat || Generation != CombatGeneration) { ProjectileRemoved(Shot.Id, false); continue; }
        ABattleUnitBase* Target = Shot.Target.Get();
        Shot.Lifetime -= Delta;
        if (!IsValid(Target) || !Target->IsAlive() || !Target->bInCombat || Shot.TargetGeneration != Target->CombatGeneration || Shot.Lifetime <= 0.f)
        { ProjectileRemoved(Shot.Id, false); continue; }
        const FVector Goal = Target->GetActorLocation() + FVector(0, 0, Target->GetHealthBarHeight() * 0.5f);
        Shot.Position = FMath::VInterpConstantTo(Shot.Position, Goal, Delta, Shot.Speed);
        if (Shot.Position.Equals(Goal, 0.1f))
        {
            ProjectileRemoved(Shot.Id, true);
            if (!bInCombat || Generation != CombatGeneration) continue;
            if (Shot.bBasic)
            {
                const FUnitDamageResult Result = Target->ReceiveDamage(Shot.Damage);
                ReportBasicHit(Target, Shot.Damage, Result);
            }
            else ApplySkillEffects(Shot.Skill, Target, Shot.EffectValues, Shot.Damage);
        }
        else
        {
            FUnitProjectileView View; View.ProjectileId = Shot.Id; View.Position = Shot.Position; View.Target = Target;
            Projectiles.Add(Shot); ProjectileUpdated(View);
        }
    }
}


#if WITH_DEV_AUTOMATION_TESTS
// 수정 범위를 세 파일로 유지하기 위해 전투 규칙 검사를 같은 번역 단위에 둔다.
// 게임 Shipping 빌드에는 포함되지 않는다. 화면/실제 BP 연결은 별도 에디터 확인 대상이다.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
namespace UnitCombatTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        UDataTable* Table = nullptr;
        FFixture()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Table = NewObject<UDataTable>(World); Table->RowStruct = FUnitDefinition::StaticStruct();
        }
        ~FFixture()
        {
            World->BeginTearingDown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
        ABattleUnitBase* Spawn(FName Name, FUnitDefinition Data, EBattleTeam Team, int32 Q)
        {
            Table->AddRow(Name, Data);
            ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
            FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name;
            FString Error;
            if (!Unit->InitializeUnit(Handle, Team, 1, Error)) return nullptr;
            ABoardTile* Tile = World->SpawnActor<ABoardTile>();
            Tile->HexQ = Q; Tile->HexR = 0;
            Tile->TeamType = Team == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
            Tile->SetActorLocation(FVector(Q * 500.f, 0, 0));
            return Unit->PlaceOnTile(Tile) ? Unit : nullptr;
        }
        static FUnitDefinition UnitData()
        {
            FUnitDefinition Data;
            Data.BaseStats.MaxHealth = 1000.f; Data.BaseStats.AttackDamage = 100.f;
            Data.BaseStats.AttackRange = 3; Data.BaseStats.CriticalChance = 0.f;
            FUnitSkillDefinition Skill; Skill.SkillId = TEXT("Shield"); Skill.Target = EUnitSkillTarget::Self;
            FUnitSkillEffect Shield; Shield.Kind = EUnitEffectKind::Shield; Shield.BaseValue = 200.f;
            Skill.Effects.Add(Shield); Data.Skills.Add(Skill);
            return Data;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitCombatRulesTest, "ProjectA.Unit.CombatRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitCombatRulesTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FUnitDefinition Data = FFixture::UnitData();
    Data.BaseStats.Armor = 100.f;
    ABattleUnitBase* A = F.Spawn(TEXT("A"), Data, EBattleTeam::Player, 0);
    ABattleUnitBase* B = F.Spawn(TEXT("B"), Data, EBattleTeam::Enemy, 1);
    if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B)) return false;
    FUnitStatModifier Up1, Up2; Up1.DamageIncrease = 0.2f; Up2.DamageIncrease = 0.3f;
    A->ApplyUpgrade(TEXT("Item1"), Up1); A->ApplyUpgrade(TEXT("Item2"), Up2);
    A->StartCombat(); B->StartCombat();
    A->SetOvertimeBonuses(0.f, 1.f);
    FUnitStatModifier Reduction1, Reduction2; Reduction1.DamageReduction = 0.2f; Reduction2.DamageReduction = 0.3f;
    B->AddModifier(TEXT("Reduction1"), Reduction1, 10.f); B->AddModifier(TEXT("Reduction2"), Reduction2, 10.f);
    B->AddShield(TEXT("ShieldA"), 60.f, 5.f); B->AddShield(TEXT("ShieldA"), 20.f, 5.f);
    B->AddShield(TEXT("ShieldB"), 40.f, 2.f);
    TestEqual(TEXT("Same shield uses max, other shields stack"), B->CurrentShield, 100.f);
    A->CurrentTarget = B;
    TestTrue(TEXT("Basic attack starts"), A->TryBasicAttack());
    const int32 AttackId = A->ActiveActionId;
    TestTrue(TEXT("Attack commits"), A->CommitAttackHit(AttackId));
    TestFalse(TEXT("Duplicate attack notify rejected"), A->CommitAttackHit(AttackId));
    TestTrue(TEXT("Specified damage order produces 84 damage"), FMath::IsNearlyEqual(B->CurrentShield, 16.f, 0.001f));
    TestEqual(TEXT("Shield prevents health loss"), B->CurrentHP, 1000.f);
    TestTrue(TEXT("Shield damage grants taken mana"), FMath::IsNearlyEqual(B->CurrentMana, 8.4f, 0.001f));
    TestEqual(TEXT("Basic hit grants 10 mana"), A->CurrentMana, 10.f);
    FUnitDamageRequest TrueDamage; TrueDamage.Attacker = A; TrueDamage.RawDamage = 100.f; TrueDamage.Type = EUnitDamageType::TrueDamage;
    const FUnitDamageResult TrueResult = B->ReceiveDamage(TrueDamage);
    TestTrue(TEXT("True damage still uses damage reduction"), FMath::IsNearlyEqual(TrueResult.MitigatedDamage, 56.f, 0.001f));
    TestFalse(TEXT("Duplicate damage request rejected"), B->ReceiveDamage(TrueDamage).bApplied);
    TestTrue(TEXT("Healing clamps to health cap"), FMath::IsNearlyEqual(B->ReceiveHealing(9999.f, A), 40.f, 0.001f));
    A->GainMana(90.f);
    TestFalse(TEXT("Skill waits for current basic action"), A->TryCastSkill());
    A->FinishAction(AttackId);
    TestTrue(TEXT("Mana skill starts after attack ends"), A->TryCastSkill());
    const int32 CastId = A->ActiveActionId;
    TestEqual(TEXT("Cast consumes mana at start"), A->CurrentMana, 0.f);
    TestEqual(TEXT("Casting blocks mana gain"), A->GainMana(20.f), 0.f);
    FUnitStatModifier Stun; Stun.bStun = true;
    A->AddModifier(TEXT("Stun"), Stun, 2.f);
    TestTrue(TEXT("Normal cast survives stun"), A->CommitSkillEffect(CastId));
    TestFalse(TEXT("Skill notify applies once"), A->CommitSkillEffect(CastId));
    TestEqual(TEXT("Self shield applied"), A->CurrentShield, 200.f);
    A->FinishAction(CastId);
    TestEqual(TEXT("Stun alone does not block mana"), A->GainMana(5.f), 5.f);
    TestFalse(TEXT("Stun blocks new actions"), A->TryBasicAttack());
    TestEqual(TEXT("Growth first grant"), A->AddPermanentGrowth(TEXT("Growth"), EUnitGrowthStat::AttackDamage, 2.f, 3.f), 2.f);
    TestEqual(TEXT("Growth respects per-combat cap"), A->AddPermanentGrowth(TEXT("Growth"), EUnitGrowthStat::AttackDamage, 2.f, 3.f), 1.f);
    A->StopCombat(); B->StopCombat();
    TestTrue(TEXT("Round reset A"), A->ResetForRound()); TestTrue(TEXT("Round reset B"), B->ResetForRound());
    TestEqual(TEXT("Shield reset"), A->CurrentShield, 0.f);
    TestEqual(TEXT("Temporary effects reset"), A->ActiveModifiers.Num(), 0);
    TestTrue(TEXT("Two-star upgrade"), A->SetStarLevel(2));
    TestTrue(TEXT("Two-star health standard"), FMath::IsNearlyEqual(A->FinalStats.MaxHealth, 1800.f, 0.001f));
    TestTrue(TEXT("Permanent growth is not multiplied"), FMath::IsNearlyEqual(A->FinalStats.AttackDamage, 183.f, 0.001f));
    TestFalse(TEXT("Three-star excluded"), A->SetStarLevel(3));
    A->StartCombat(); B->StartCombat();
    FUnitDamageRequest Lethal; Lethal.Attacker = A; Lethal.RawDamage = 100000.f;
    TestTrue(TEXT("Lethal damage kills"), B->ReceiveDamage(Lethal).bKilled);
    TestFalse(TEXT("Dead unit cannot take damage again"), B->ReceiveDamage(Lethal).bApplied);
    TestEqual(TEXT("Healing cannot revive"), B->ReceiveHealing(500.f, A), 0.f);
    TestTrue(TEXT("Corpse actor remains for recovery"), IsValid(B));
    A->StopCombat(); B->StopCombat();
    TestTrue(TEXT("Dead owned unit recovers"), B->ResetForRound());
    TestTrue(TEXT("Recovered unit alive"), B->IsAlive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitProjectileRulesTest, "ProjectA.Unit.ProjectileAndTriggers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitProjectileRulesTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FUnitDefinition Data = FFixture::UnitData(); Data.BasicAttack.Delivery = EUnitAttackDelivery::Projectile;
    FUnitSkillDefinition Growth; Growth.SkillId = TEXT("EveryThirdHit"); Growth.Trigger = EUnitTrigger::BasicAttackHit;
    Growth.TriggerEvery = 3; Growth.Target = EUnitSkillTarget::Self;
    FUnitSkillEffect Effect; Effect.Kind = EUnitEffectKind::PermanentGrowth; Effect.BaseValue = 1.f; Effect.GrowthLimitPerCombat = 3.f;
    Growth.Effects.Add(Effect); Data.Skills.Add(Growth);
    ABattleUnitBase* A = F.Spawn(TEXT("Archer"), Data, EBattleTeam::Player, 0);
    FUnitDefinition Enemy = FFixture::UnitData(); Enemy.Skills.Empty();
    ABattleUnitBase* B = F.Spawn(TEXT("Enemy"), Enemy, EBattleTeam::Enemy, 1);
    if (!A || !B) return false;
    A->StartCombat(); B->StartCombat(); A->CurrentTarget = B;
    TestTrue(TEXT("Ranged attack starts"), A->TryBasicAttack());
    int32 Id = A->ActiveActionId;
    TestTrue(TEXT("Projectile launches"), A->CommitAttackHit(Id));
    TestEqual(TEXT("No damage at launch"), B->CurrentHP, 1000.f);
    TestEqual(TEXT("No mana at launch"), A->CurrentMana, 0.f);
    A->FinishAction(Id);
    FUnitStatModifier Stun; Stun.bStun = true; A->AddModifier(TEXT("Stun"), Stun, 10.f);
    A->Tick(0.6f);
    TestEqual(TEXT("Launched projectile survives caster stun"), B->CurrentHP, 900.f);
    TestEqual(TEXT("Mana granted on arrival"), A->CurrentMana, 10.f);
    A->RemoveEffect(TEXT("Stun"));
    for (int32 Hit = 1; Hit < 3; ++Hit)
    {
        A->Tick(1.1f);
        if (A->ActionState == EUnitActionState::Idle) A->TryBasicAttack();
        Id = A->ActiveActionId;
        TestTrue(TEXT("Next ranged hit commits"), A->CommitAttackHit(Id));
        A->FinishAction(Id); A->Tick(0.6f);
    }
    TestEqual(TEXT("Three hit counter"), A->BasicHitCount, 3);
    TestEqual(TEXT("Every third hit grows once"), A->PermanentGrowth.AttackDamage, 1.f);
    TestEqual(TEXT("Passive-only unit has no mana"), B->GainMana(100.f), 0.f);
    A->Tick(1.1f);
    if (A->ActionState == EUnitActionState::Idle) A->TryBasicAttack();
    A->CommitAttackHit(A->ActiveActionId);
    const float Health = B->CurrentHP;
    A->StopCombat(); A->Tick(10.f);
    TestEqual(TEXT("Combat stop cancels pending projectiles"), B->CurrentHP, Health);
    B->StopCombat(); A->ResetForRound(); B->ResetForRound();
    A->StartCombat(); B->StartCombat(); A->CurrentTarget = B;
    A->TryBasicAttack(); A->CommitAttackHit(A->ActiveActionId); A->FinishAction(A->ActiveActionId);
    FUnitDamageRequest Lethal; Lethal.Attacker = A; Lethal.RawDamage = 10000.f;
    B->ReceiveDamage(Lethal); A->Tick(1.f);
    TestEqual(TEXT("Dead target projectile does not grant basic hit"), A->BasicHitCount, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitEffectsRulesTest, "ProjectA.Unit.EffectsAndChains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitEffectsRulesTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FUnitDefinition Data = FFixture::UnitData(); Data.BaseStats.Armor = 100.f;
    FUnitSkillDefinition Bonus; Bonus.SkillId = TEXT("BonusDamage"); Bonus.Trigger = EUnitTrigger::DamageDealt;
    FUnitSkillEffect Damage; Damage.Kind = EUnitEffectKind::Damage; Damage.BaseValue = 20.f; Damage.DamageType = EUnitDamageType::TrueDamage;
    Bonus.Effects.Add(Damage); Data.Skills.Add(Bonus);
    ABattleUnitBase* A = F.Spawn(TEXT("A"), Data, EBattleTeam::Player, 0);
    Data.Skills.Empty(); Data.bIsBoss = true;
    ABattleUnitBase* B = F.Spawn(TEXT("Boss"), Data, EBattleTeam::Enemy, 1);
    if (!A || !B) return false;
    A->StartCombat(); B->StartCombat(); A->CurrentTarget = B;
    FUnitStatModifier Weak, Strong; Weak.ArmorReduction = 0.3f; Strong.ArmorReduction = 0.5f;
    B->AddModifier(TEXT("Weak"), Weak, 5.f); B->AddModifier(TEXT("Strong"), Strong, 0.2f);
    TestEqual(TEXT("Strongest armor reduction wins"), B->FinalStats.Armor, 50.f);
    B->Tick(0.3f);
    TestEqual(TEXT("Weaker effect returns after strong expiry"), B->FinalStats.Armor, 70.f);
    B->RemoveEffect(TEXT("Weak"));
    FUnitStatModifier Stun; Stun.bStun = true;
    B->AddModifier(TEXT("Stun"), Stun, 4.f);
    B->AddModifier(TEXT("Stun"), Stun, 2.f);
    TestEqual(TEXT("Boss halves stun and refresh keeps longest"), B->ActiveModifiers[0].RemainingSeconds, 2.f);
    B->RemoveEffect(TEXT("Stun"));
    FUnitStatModifier Combined; Combined.bStun = true; Combined.AttackSpeedSlow = 0.5f;
    B->AddModifier(TEXT("Combined"), Combined, 2.f);
    B->Tick(1.1f);
    TestFalse(TEXT("Combined boss stun expires at half duration"), B->IsStunned());
    TestEqual(TEXT("Combined attack slow keeps original duration"), B->FinalStats.AttackSpeed, 0.5f);
    FUnitStatModifier Block; Block.bBlockManaGain = true;
    A->AddModifier(TEXT("RapidFire"), Block, 3.f);
    TestEqual(TEXT("Rapid fire blocks all mana gain"), A->GainMana(50.f), 0.f);
    A->RemoveEffect(TEXT("RapidFire"));
    FUnitDamageRequest Request; Request.Attacker = A; Request.Victim = B; Request.RawDamage = 100.f; Request.Type = EUnitDamageType::TrueDamage;
    B->ReceiveDamage(Request);
    TestEqual(TEXT("Damage trigger adds once without recursive loop"), B->CurrentHP, 880.f);
    FUnitDefinition RapidFire = FFixture::UnitData();
    FUnitSkillEffect& RapidEffect = RapidFire.Skills[0].Effects[0];
    RapidEffect.Kind = EUnitEffectKind::Modifier;
    RapidEffect.Modifier.AttackSpeedBonus = 0.2f; RapidEffect.Modifier.bBlockManaGain = true;
    RapidEffect.bOverrideModifierAtTwoStars = true;
    RapidEffect.TwoStarModifier.AttackSpeedBonus = 0.5f; RapidEffect.TwoStarModifier.bBlockManaGain = true;
    ABattleUnitBase* Archer = F.Spawn(TEXT("TwoStarArcher"), RapidFire, EBattleTeam::Player, 2);
    if (!Archer) return false;
    Archer->SetStarLevel(2); Archer->StartCombat(); Archer->GainMana(100.f);
    TestTrue(TEXT("Rapid fire starts with full mana"), Archer->TryCastSkill());
    Archer->CommitSkillEffect(Archer->ActiveActionId); Archer->FinishAction(Archer->ActiveActionId);
    TestEqual(TEXT("Two-star can override buff rate without changing duration"), Archer->FinalStats.AttackSpeed, 1.5f);
    TestEqual(TEXT("Rapid fire skill blocks mana while active"), Archer->GainMana(100.f), 0.f);
    Archer->Tick(3.1f);
    TestEqual(TEXT("Mana gain resumes after rapid fire expires"), Archer->GainMana(10.f), 10.f);
    FUnitDefinition TargetData = FFixture::UnitData(); TargetData.Skills.Empty();
    ABattleUnitBase* Hunter = F.Spawn(TEXT("Hunter"), TargetData, EBattleTeam::Player, 10);
    ABattleUnitBase* First = F.Spawn(TEXT("First"), TargetData, EBattleTeam::Enemy, 12);
    ABattleUnitBase* Second = F.Spawn(TEXT("Second"), TargetData, EBattleTeam::Enemy, 13);
    if (!Hunter || !First || !Second) return false;
    Hunter->SetCombatRoster({Hunter, First, Second});
    Hunter->StartCombat(); First->StartCombat(); Second->StartCombat();
    Hunter->Tick(0.01f);
    TestTrue(TEXT("Roster chooses nearest enemy by hex distance"), Hunter->CurrentTarget == First);
    ABoardTile* CloserTile = F.World->SpawnActor<ABoardTile>(); CloserTile->HexQ = 11; CloserTile->HexR = 0;
    CloserTile->SetOccupiedUnit(Second);
    Hunter->FinishAction(Hunter->ActiveActionId); Hunter->Tick(0.01f);
    TestTrue(TEXT("Closer enemy does not steal valid target"), Hunter->CurrentTarget == First);
    FUnitDamageRequest Kill; Kill.Attacker = Hunter; Kill.RawDamage = 10000.f;
    First->ReceiveDamage(Kill); Hunter->Tick(0.4f);
    TestTrue(TEXT("Dead target causes retarget"), Hunter->CurrentTarget == Second);
    return true;
}
#endif
