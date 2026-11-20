#include "BattleUnitBase.h"
#include "Skills/UnitSkill.h"
#include "BoardTile.h"
#include "BoardManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/WidgetComponent.h"
#include "UI/UnitStatusWidget.h"

namespace
{
    float Safe(float Value, float Fallback = 0.f) { return FMath::IsFinite(Value) ? Value : Fallback; }
    float Positive(float Value) { return FMath::Max(0.f, Safe(Value)); }
    float Fraction(float Value) { return FMath::Clamp(Safe(Value), 0.f, 1.f); }
    void FaceLocation(AActor& Actor, const FVector& Location)
    {
        // 액터의 논리적 정면(+X)만 돌린다. 모델 정면 보정은 DT의 VisualTransform을 유지한다.
        FVector Direction = Location - Actor.GetActorLocation();
        Direction.Z = 0.f;
        if (!Direction.IsNearlyZero()) Actor.SetActorRotation(Direction.Rotation());
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
    // 모델 회전/크기 보정과 분리한 화면 공간 위젯이다. 선택 클릭을 가로채지 않는다.
    StatusWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("StatusWidget"));
    StatusWidget->SetupAttachment(SceneRoot);
    StatusWidget->SetWidgetSpace(EWidgetSpace::Screen);
    StatusWidget->SetWidgetClass(UUnitStatusWidget::StaticClass());
    StatusWidget->SetDrawSize(FVector2D(128.f, 42.f));
    StatusWidget->SetDrawAtDesiredSize(true);
    StatusWidget->SetPivot(FVector2D(0.5f, 1.f));
    StatusWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StatusWidget->SetGenerateOverlapEvents(false);
    StatusWidget->SetVisibility(false);
    StatusWidgetClass = TSoftClassPtr<UUnitStatusWidget>(FSoftObjectPath(TEXT("/Game/BluePrints/Units/UI/WBP_UnitStatus.WBP_UnitStatus_C")));
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
    RefreshStatusWidget();
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
    TArray<TObjectPtr<UUnitSkillBase>> NewSkills;
    if (Row)
    {
        TSet<FName> SkillIds;
        int32 ActiveCount = 0;
        const auto AddSkill = [&](UUnitSkillBase* Instance, const FUnitSkillDefinition& Config)
        {
            if (!Instance) { Error = TEXT("Skill class could not be instantiated."); return; }
            const bool bActive = Instance->IsA<UUnitActiveSkill>();
            if (!bActive && !Instance->IsA<UUnitPassiveSkill>()) Error = TEXT("Skill must derive from ActiveSkill or PassiveSkill.");
            if (bActive && ++ActiveCount > 1) Error = TEXT("Only one active skill is supported per unit.");
            if (Config.SkillId.IsNone() || SkillIds.Contains(Config.SkillId)) Error = TEXT("Every skill needs a unique non-empty SkillId.");
            if (bActive != (Config.Trigger == EUnitTrigger::ManaFull)) Error = TEXT("Active skills use ManaFull; passive skills use a combat event.");
            SkillIds.Add(Config.SkillId);
            Instance->Settings = Config;
            NewSkills.Add(Instance);
        };
        for (const TSubclassOf<UUnitSkillBase>& Class : Row->SkillClasses)
        {
            if (!Class || Class->HasAnyClassFlags(CLASS_Abstract)) { Error = TEXT("Choose a concrete skill class."); continue; }
            UUnitSkillBase* Instance = NewObject<UUnitSkillBase>(this, Class);
            Instance->Settings = Instance->BuildSettings();
            AddSkill(Instance, Instance->Settings);
        }
    }
    if (!Error.IsEmpty()) { LastValidationMessage = Error; return false; }
    // 종류 교체와 승급은 다른 작업이다. 종류 교체는 소유 상태를 새로 만들고 승급은 성장/아이템을 보존한다.
    UnitData = Data; UnitID = Data.RowName; Definition = *Row; TeamType = Team; StarLevel = Stars;
    bInitialized = true; bIsDead = false; bCombatStartedForRound = false;
    Upgrades.Empty(); PermanentGrowth = FUnitStatModifier(); GrowthThisCombat.Empty();
    ActiveModifiers.Empty(); Shields.Empty(); AttackEnhancements.Empty(); AppliedDamage.Empty(); DamageContributors.Empty();
    PeriodicEffects.Reset();
    PendingTileImpacts.Reset();
    for (UUnitSkillBase* Old : SkillInstances) { Old->ResetRuntime(); Old->OwnerUnit = nullptr; }
    SkillInstances = MoveTemp(NewSkills);
    for (UUnitSkillBase* Instance : SkillInstances) { Instance->OwnerUnit = this; Instance->ResetRuntime(); }
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
    FinalStats.MaxHealth = Positive(FinalStats.MaxHealth) * UnitProgression::StatMultiplier(StarLevel);
    FinalStats.AttackDamage = Positive(FinalStats.AttackDamage) * UnitProgression::StatMultiplier(StarLevel);
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
    float PeriodicAttackDamageBonus = 0.f;
    PeriodicAbilityPowerBonus = 0.f;
    for (const auto& Entry : PeriodicEffects)
    {
        MRReduction = FMath::Max(MRReduction, Entry.Value.MagicResistanceReduction);
        PeriodicAttackDamageBonus += Entry.Value.AttackDamageBonus;
        PeriodicAbilityPowerBonus += Entry.Value.AbilityPowerBonus;
    }
    FinalStats.MaxHealth = FMath::Max(1.f, Safe(FinalStats.MaxHealth, 1.f));
    FinalStats.AttackDamage = Positive(FinalStats.AttackDamage) * (1.f - ADReduction) * (1.f + PeriodicAttackDamageBonus);
    AbilityPowerBeforePeriodicBonuses = Positive(FinalStats.AbilityPower) * (1.f - APReduction);
    FinalStats.AbilityPower = AbilityPowerBeforePeriodicBonuses * (1.f + PeriodicAbilityPowerBonus);
    FinalStats.Armor = Safe(FinalStats.Armor) * (1.f - ArmorReduction);
    FinalStats.MagicResistance = Safe(FinalStats.MagicResistance) * (1.f - MRReduction);
    AttackSpeedBonusBeforeSkills = SpeedBonus;
    FinalStats.AttackRange = FMath::Max(0, FinalStats.AttackRange);
    FinalStats.MovementSpeed = Positive(FinalStats.MovementSpeed);
    FinalStats.CriticalChance = Fraction(FinalStats.CriticalChance);
    FinalStats.CriticalMultiplier = FMath::Max(1.f, Safe(FinalStats.CriticalMultiplier, 1.5f));
    for (const UUnitSkillBase* Skill : SkillInstances) Skill->ModifyMaxMana(FinalStats.MaxMana);
    FinalStats.MaxMana = Positive(FinalStats.MaxMana);
    FinalStats.StartMana = FMath::Clamp(Positive(FinalStats.StartMana), 0.f, FinalStats.MaxMana);
    CurrentHP = bIsDead ? 0.f : FinalStats.MaxHealth * HealthRatio;
    RefreshAttackSpeed();
    CurrentMana = UsesMana() ? FMath::Clamp(Positive(CurrentMana), 0.f, FinalStats.MaxMana) : 0.f;
    OnStatsChanged.Broadcast(this); OnHealthChanged.Broadcast(this); OnManaChanged.Broadcast(this);
}

bool ABattleUnitBase::RefreshAttackSpeed()
{
    float SpeedBonus = AttackSpeedBonusBeforeSkills;
    if (CurrentHP > 0.f && !bIsDead)
    {
        const float HealthRatio = Fraction(CurrentHP / FMath::Max(1.f, FinalStats.MaxHealth));
        for (const UUnitSkillBase* Skill : SkillInstances)
            SpeedBonus += Safe(Skill->GetHealthBasedAttackSpeedBonus(HealthRatio));
    }
    const float Previous = FinalStats.AttackSpeed;
    FinalStats.AttackSpeed = FMath::Clamp(
        Positive(Definition.BaseStats.AttackSpeed) * FMath::Max(0.f, 1.f + Safe(SpeedBonus)) * (1.f - AttackSlow),
        0.f, FMath::Max(0.01f, Safe(CombatPolicy.MaxAttackSpeed, 5.f)));
    return Previous != FinalStats.AttackSpeed;
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
    ResetDeathVisual();
    DeathFadeMaterials.Empty();
    // 메시를 바꿔도 머티리얼 오버라이드는 남을 수 있으므로 반드시 비운다.
    if (Body->GetAnimationMode() == EAnimationMode::AnimationSingleNode) Body->Stop();
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
    USkeletalMesh* BodyMesh = Visuals.SkeletalMesh;
    for (const UUnitSkillBase* Skill : SkillInstances)
    {
        USkeletalMesh* Override = Skill->GetPersistentBodyMeshOverride();
        if (!IsValid(Override) || Override == BodyMesh) continue;
        if (!BodyMesh || !BodyMesh->GetSkeleton() || BodyMesh->GetSkeleton() != Override->GetSkeleton())
        {
            LastValidationMessage += FString::Printf(TEXT("Body mesh override requires the base skeleton: %s. "), *Override->GetName());
            continue;
        }
        BodyMesh = Override;
    }
    // 진화 메시를 먼저 정한 뒤, 해당 메시의 손 소켓에 기존 무기를 다시 붙인다.
    Body->SetSkeletalMesh(BodyMesh);
    if (!BodyMesh) LastValidationMessage += TEXT("No skeletal mesh assigned. ");
    for (int32 Index = 0; Index < Visuals.MaterialOverrides.Num(); ++Index)
    {
        if (Visuals.MaterialOverrides[Index] && Index < Body->GetNumMaterials())
            Body->SetMaterial(Index, Visuals.MaterialOverrides[Index]);
    }
    ApplyWeapon(MainHand, Visuals.MainHand);
    ApplyWeapon(OffHand, Visuals.OffHand);
    for (UUnitSkillBase* Skill : SkillInstances) Skill->ApplyPersistentVisuals();
    const FUnitAnimationDefinition& Animations = Definition.Animations;
    if (Animations.AnimClass)
    {
        Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        Body->SetAnimInstanceClass(Animations.AnimClass);
    }
    else if (Animations.IdleAnimation && BodyMesh)
    {
        Body->PlayAnimation(Animations.IdleAnimation, true);
    }
    // 에디터 미리보기에는 동적 머티리얼을 남기지 않는다.
    if (GetWorld() && GetWorld()->IsGameWorld()) CreateDeathFadeMaterials();
    OnVisualsApplied.Broadcast(this);
    RefreshStatusWidget();
}

void ABattleUnitBase::RefreshStatusWidget()
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || !HasActorBegunPlay() || !StatusWidget) return;
    StatusWidget->SetVisibility(bShowStatusWidget && bInitialized);
    if (!bShowStatusWidget || !bInitialized) return;
    UClass* WidgetClass = StatusWidgetClass.IsNull() ? nullptr : StatusWidgetClass.LoadSynchronous();
    StatusWidget->SetWidgetClass(WidgetClass ? WidgetClass : UUnitStatusWidget::StaticClass());
    StatusWidget->InitWidget();
    if (UUnitStatusWidget* Widget = Cast<UUnitStatusWidget>(StatusWidget->GetUserWidgetObject()))
    {
        Widget->SetUnit(this);
        StatusWidget->SetDrawSize(Widget->GetStatusDrawSize());
    }
    float Top = GetActorLocation().Z + GetHealthBarHeight();
    if (Body->GetSkeletalMeshAsset())
    {
        const FBoxSphereBounds BodyBounds = Body->CalcBounds(Body->GetComponentTransform());
        Top = BodyBounds.Origin.Z + BodyBounds.BoxExtent.Z;
    }
    // 실제 몸체의 월드 높이로 계산해 DT Scale이 달라도 머리 위에 놓는다.
    StatusWidget->SetWorldLocation(FVector(GetActorLocation().X, GetActorLocation().Y, Top + StatusWidgetHeightOffset));
}

void ABattleUnitBase::CreateDeathFadeMaterials()
{
    DeathFadeMaterials.Empty();
    // 인스턴스를 기물별로 분리해 같은 머티리얼을 쓰는 생존자에게 페이드가 전파되지 않게 한다.
    for (UMeshComponent* Mesh : {static_cast<UMeshComponent*>(Body.Get()), static_cast<UMeshComponent*>(MainHand.Get()), static_cast<UMeshComponent*>(OffHand.Get())})
        for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
            if (Mesh->GetMaterial(Index))
            {
                float DefaultOpacity = 1.f;
                if (Mesh->GetMaterial(Index)->GetScalarParameterValue(FMaterialParameterInfo(TEXT("UnitOpacity")), DefaultOpacity))
                    if (UMaterialInstanceDynamic* Material = Mesh->CreateDynamicMaterialInstance(Index))
                        DeathFadeMaterials.Add(Material);
            }
    SetDeathOpacity(1.f);
}

void ABattleUnitBase::SetDeathOpacity(float Opacity)
{
    DeathOpacity = Fraction(Opacity);
    for (UMaterialInstanceDynamic* Material : DeathFadeMaterials)
        if (IsValid(Material)) Material->SetScalarParameterValue(TEXT("UnitOpacity"), DeathOpacity);
}

void ABattleUnitBase::ResetDeathVisual()
{
    bDeathVisualActive = false;
    DeathVisualElapsed = DeathAnimationSeconds = 0.f;
    SetDeathOpacity(1.f);
    SetActorHiddenInGame(false);
}

void ABattleUnitBase::TickDeathVisual(float Delta)
{
    if (!bIsDead || !bDeathVisualActive) return;
    DeathVisualElapsed += Delta;
    const float FadeElapsed = DeathVisualElapsed - DeathAnimationSeconds - Positive(DeathPoseHoldSeconds);
    if (FadeElapsed < 0.f) return;
    const float FadeDuration = Positive(DeathFadeSeconds);
    SetDeathOpacity(FadeDuration > 0.f ? 1.f - FadeElapsed / FadeDuration : 0.f);
    if (DeathOpacity <= 0.f)
    {
        // 외형만 숨긴다. 보유 기물과 준비 위치는 ResetForRound에서 재사용한다.
        SetActorHiddenInGame(true);
        bDeathVisualActive = false;
    }
}


float ABattleUnitBase::PlaySpawnAnimation()
{
    if (!IsAlive() || bInCombat || ActionState != EUnitActionState::Idle || !Definition.Animations.SpawnMontage) return 0.f;
    if (UAnimInstance* Anim = Body->GetAnimInstance())
        return Anim->Montage_Play(Definition.Animations.SpawnMontage);
    return 0.f;
}

bool ABattleUnitBase::IsAlive() const { return bInitialized && !bIsDead && CurrentHP > 0.f; }
bool ABattleUnitBase::UsesMana() const
{
    return SkillInstances.ContainsByPredicate([](const UUnitSkillBase* S) { return S && S->IsA<UUnitActiveSkill>(); });
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
        BoardManager = Tile->OwningBoard;
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
bool ABattleUnitBase::SetBoardManager(ABoardManager* Board)
{
    if (bInCombat || (Board && (!IsValid(Board) || Board->GetWorld() != GetWorld()))) return false;
    if (IsValid(CurrentTile) && IsValid(CurrentTile->OwningBoard) && CurrentTile->OwningBoard != Board) return false;
    BoardManager = Board; return true;
}
bool ABattleUnitBase::CancelBoardMove(int32 MoveId)
{
    if (bReleasingMove || MoveId <= 0 || ActiveActionId != MoveId || ActionState != EUnitActionState::Moving) return false;
    CancelAction(); RetargetTimer = 0.f; return true;
}
EUnitPathResult ABattleUnitBase::RequestCombatStep_Implementation(ABattleUnitBase* Target, int32 MoveId, ABoardTile*& NextTile)
{
    NextTile = nullptr;
    if (!IsValid(BoardManager) && IsValid(CurrentTile)) BoardManager = CurrentTile->OwningBoard;
    return IsValid(BoardManager) ? BoardManager->TryReserveNextStep(this, Target, MoveId, NextTile) : EUnitPathResult::Unavailable;
}
void ABattleUnitBase::MovementReservationReleased_Implementation(ABoardTile* Tile, int32 MoveId, bool bCompleted)
{
    // 표시/디버그용 확장 지점이다. 도착 확정과 예약 해제는 알림 전에 C++에서 끝낸다.
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
    PrepareSkillsForRound();
    if (IsValid(CurrentTile->OwningBoard)) BoardManager = CurrentTile->OwningBoard;
    // 명시적으로 받은 명단이 없으면 시작 시점의 보드 유닛을 한 번만 수집한다.
    if (CombatRoster.IsEmpty() && IsValid(BoardManager)) SetCombatRoster(BoardManager->GetUnitsOnBoard());
    bCombatStartedForRound = true; bInCombat = true; ++CombatGeneration;
    PreparationTransform = GetActorTransform(); PreparationTile = CurrentTile; PreparationPlacement = Placement;
    ActiveModifiers.Empty(); Shields.Empty(); AttackEnhancements.Empty(); AppliedDamage.Empty(); DamageContributors.Empty(); GrowthThisCombat.Empty();
    PeriodicEffects.Reset();
    for (UUnitSkillBase* Instance : SkillInstances) Instance->ResetRuntime();
    BasicAttackCount = BasicHitCount = 0; AttackCooldown = RetargetTimer = BlockedSeconds = 0.f;
    UnreachableTargets.Empty(); CurrentTarget = nullptr;
    CurrentHP = FinalStats.MaxHealth;
    RecalculateStats(); CurrentHP = FinalStats.MaxHealth; CurrentMana = UsesMana() ? FinalStats.StartMana : 0.f;
    RefreshShieldTotal(); OnHealthChanged.Broadcast(this); OnManaChanged.Broadcast(this);
    // 전원이 StartCombat을 받은 뒤 첫 Tick에서 시작 효과를 한 번 적용한다.
    bCombatStartPending = true;
}
void ABattleUnitBase::StopCombat()
{
    const bool bWasInCombat = bInCombat;
    bInCombat = false; bCombatStartPending = false; ++CombatGeneration;
    DamageContributors.Empty();
    CancelAction(); CurrentTarget = nullptr;
    // 전원 정지 후 준비 위치를 복구할 수 있게 전투 타일 점유를 먼저 반환한다.
    if (bWasInCombat)
    {
        if (IsValid(BoardManager)) BoardManager->ReleaseUnitTiles(this);
        else if (IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    }
    PendingTileImpacts.Reset();
    TArray<FPendingProjectile> Removed = MoveTemp(Projectiles);
    for (const FPendingProjectile& Shot : Removed) ProjectileRemoved(Shot.Id, false);
    ActiveModifiers.Empty(); Shields.Empty(); AttackEnhancements.Empty();
    PeriodicEffects.Reset();
    OvertimeAttackSpeed = 0.f; OvertimeDamageMultiplier = 1.f;
    for (UUnitSkillBase* Instance : SkillInstances) Instance->ResetRuntime();
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
    CurrentHP = FinalStats.MaxHealth;
    RecalculateStats(); CurrentHP = FinalStats.MaxHealth; CurrentMana = UsesMana() ? FinalStats.StartMana : 0.f;
    SetActorTransform(PreparationTransform);
    Placement = PreparationPlacement; bIsDeployed = Placement == EUnitPlacement::Board;
    if (bIsDeployed)
    {
        PreparationTile->SetOccupiedUnit(this);
        if (CurrentTile != PreparationTile) return false;
        BoardManager = PreparationTile->OwningBoard;
    }
    if (!PrepareSkillsForRound()) ApplyVisuals();
    OnHealthChanged.Broadcast(this); OnManaChanged.Broadcast(this);
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
    if (bReleasingMove) return;
    TGuardValue<bool> Guard(bReleasingMove, true);
    ABoardTile* Tile = ReservedTile;
    const int32 MoveId = ActiveActionId;
    ReservedTile = nullptr;
    if (!bCompleted)
    {
        SetActorLocation(MoveStart);
        if (IsValid(BoardManager)) BoardManager->CancelReservedMove(this, MoveId);
    }
    // BP가 이 알림을 재정의해도 C++ 예약 정리는 이미 끝난 상태다.
    if (IsValid(Tile)) MovementReservationReleased(Tile, MoveId, bCompleted);
}
void ABattleUnitBase::CancelAction(bool bRefundMana)
{
    if (ActionState == EUnitActionState::Moving) ReleaseMove(false);
    const bool bCasting = ActionState == EUnitActionState::Casting;
    UUnitSkillBase* CancelledSkill = bCasting && SkillInstances.IsValidIndex(ActiveSkillIndex) ? SkillInstances[ActiveSkillIndex].Get() : nullptr;
    const int32 CancelledId = ActiveActionId;
    const bool bRefund = bRefundMana && bCasting && !bActionCommitted;
    ActiveActionId = 0; ActiveSkillIndex = INDEX_NONE; ActionTarget.Reset(); bActionCommitted = false;
    if (UAnimInstance* Anim = Body->GetAnimInstance())
    {
        // 사망 다음 프레임의 StopCombat도 이 함수를 호출한다.
        // 이미 재생 중인 사망 연출은 유지하고, 사망 전 공격/시전 취소는 기존대로 처리한다.
        const bool bPlayingDeath = bIsDead && Definition.Animations.DeathMontage
            && Anim->Montage_IsActive(Definition.Animations.DeathMontage);
        if (!bPlayingDeath) Anim->Montage_Stop(0.1f);
    }
    ChangeAction(bIsDead ? EUnitActionState::Dead : EUnitActionState::Idle);
    if (bRefund) { CurrentMana = FMath::Min(PreCastMana, FinalStats.MaxMana); OnManaChanged.Broadcast(this); }
    if (CancelledSkill) { CancelledSkill->ClearExecution(); CancelledSkill->OnCastCancelled(CancelledId); }
}
void ABattleUnitBase::Die()
{
    if (bIsDead) return;
    bIsDead = true; CurrentHP = 0.f;
    // 외부 사망 콜백이 라운드를 종료하기 전에 관여 기록을 한 번 확정한다.
    NotifyKillParticipants();
    if (!PeriodicEffects.IsEmpty())
    {
        PeriodicEffects.Reset();
        RecalculateStats();
    }
    AttackEnhancements.RemoveAll([](const FAttackEnhancement& E) { return E.bConsumeOnHit; });
    CancelAction(); CurrentTarget = nullptr;
    for (UUnitSkillBase* Instance : SkillInstances) { Instance->ClearExecution(); Instance->OnOwnerDied(); }
    if (IsValid(BoardManager)) BoardManager->ReleaseUnitTiles(this);
    else if (IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    SelectionCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DeathVisualElapsed = DeathAnimationSeconds = 0.f;
    SetDeathOpacity(1.f);
    bDeathVisualActive = true;
    if (Definition.Animations.DeathMontage)
        if (UAnimInstance* Anim = Body->GetAnimInstance())
        {
            DeathAnimationSeconds = Positive(Anim->Montage_Play(Definition.Animations.DeathMontage));
            // 공유 몽타주 에셋 대신 이 기물의 재생 인스턴스만 마지막 자세를 유지한다.
            if (FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Definition.Animations.DeathMontage))
                Instance->bEnableAutoBlendOut = false;
        }
    // Actor는 파괴하지 않는다. 이미 발사한 탄은 유지하고 전투 종료 때 일괄 취소한다.
    OnUnitDied.Broadcast(this);
}

void ABattleUnitBase::NotifyKillParticipants()
{
    // 먼저 비워서 알림 도중의 재진입/같은 사망의 재전달로 중복 집계하지 않게 한다.
    const TMap<TWeakObjectPtr<ABattleUnitBase>, int32> Contributors = MoveTemp(DamageContributors);
    const int32 DefeatedGeneration = CombatGeneration;
    for (const auto& Entry : Contributors)
    {
        if (!bInCombat || CombatGeneration != DefeatedGeneration) break;
        ABattleUnitBase* Contributor = Entry.Key.Get();
        if (!IsValid(Contributor) || !Contributor->bInCombat || Contributor->CombatGeneration != Entry.Value
            || Contributor->TeamType == TeamType || Contributor->GetWorld() != GetWorld()) continue;
        const auto Skills = Contributor->SkillInstances;
        for (UUnitSkillBase* Skill : Skills)
        {
            if (!bInCombat || CombatGeneration != DefeatedGeneration || !Contributor->bInCombat
                || Contributor->CombatGeneration != Entry.Value) break;
            Skill->OnKillParticipation(this);
        }
    }
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
    // 기절 저항은 전투 정책에 둔다. 복합 효과의 공속 감소 등은 원래 시간을 유지한다.
    const float StunMultiplier = Fraction(CombatPolicy.StunDurationMultiplier);
    const bool bHasOtherStats = Modifier.MaxHealth != 0.f || Modifier.AttackDamage != 0.f || Modifier.AbilityPower != 0.f
        || Modifier.Armor != 0.f || Modifier.MagicResistance != 0.f || Modifier.AttackSpeedBonus != 0.f
        || Modifier.AttackSpeedSlow != 0.f || Modifier.DamageIncrease != 0.f || Modifier.DamageReduction != 0.f
        || Modifier.ArmorReduction != 0.f || Modifier.MagicResistanceReduction != 0.f
        || Modifier.AttackDamageReduction != 0.f || Modifier.AbilityPowerReduction != 0.f
        || Modifier.CriticalChance != 0.f || Modifier.CriticalMultiplier != 0.f || Modifier.bBlockManaGain;
    if (StunMultiplier != 1.f && Modifier.bStun && bHasOtherStats)
    {
        FUnitStatModifier StatsOnly = Modifier; StatsOnly.bStun = false;
        FUnitStatModifier StunOnly; StunOnly.bStun = true;
        const bool bStats = AddModifier(EffectId, StatsOnly, Duration);
        const bool bStun = AddModifier(FName(*(EffectId.ToString() + TEXT(".Stun"))), StunOnly, Duration);
        return bStats && (bStun || StunMultiplier == 0.f);
    }
    FUnitStatModifier Applied = Modifier;
    float Seconds = Positive(Duration);
    if (Applied.bStun) Seconds *= StunMultiplier;
    if (Seconds <= 0.f) return false;
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
        else if (ActionState == EUnitActionState::Casting && SkillInstances.IsValidIndex(ActiveSkillIndex))
        {
            const FUnitSkillDefinition& Skill = SkillInstances[ActiveSkillIndex]->Settings;
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
        + AttackEnhancements.RemoveAll([&](const FAttackEnhancement& E) { return E.Id == EffectId; })
        + PeriodicEffects.Remove(EffectId);
    if (Removed == 0) return false;
    RefreshShieldTotal(); RecalculateStats(); return true;
}

float ABattleUnitBase::GetPeriodicDamageRemaining(FName EffectId) const
{
    const FPeriodicEffect* Effect = PeriodicEffects.Find(EffectId);
    return Effect ? float(FMath::Max(0.0, Effect->RemainingSeconds)) : -1.f;
}

bool ABattleUnitBase::ApplyPeriodicMagicDamage(FName EffectId, ABattleUnitBase* Source, int32 ActionId,
    float DamagePerTick, float SecondsPerTick, int32 TickCount, float MagicResistanceReduction)
{
    if (!IsValid(Source) || !Source->bInCombat || !Source->IsAlive() || Source->GetWorld() != GetWorld()
        || !Source->IsEnemyTarget(this) || EffectId.IsNone()
        || !FMath::IsFinite(SecondsPerTick) || SecondsPerTick <= 0.f || TickCount <= 0) return false;

    FPeriodicEffect& Effect = PeriodicEffects.FindOrAdd(EffectId);
    const bool bRefresh = Effect.Revision != 0;
    if (bRefresh && Effect.bHealing) return false;
    const float Reduction = Fraction(MagicResistanceReduction);
    const bool bStatsChanged = Effect.MagicResistanceReduction != Reduction;
    // 같은 ID는 다음 피해까지 남은 시간을 보존하고, 새 시전의 수치와 소유자만 이어받는다.
    Effect.TimeUntilTick = bRefresh ? FMath::Max(0.0, Effect.TimeUntilTick) : SecondsPerTick;
    Effect.Revision = ++PeriodicEffectRevision;
    Effect.Source = Source;
    Effect.SourceGeneration = Source->CombatGeneration;
    Effect.ActionId = ActionId;
    Effect.AmountPerTick = Positive(DamagePerTick);
    Effect.OffenseMultiplier = Source->MakeDamage(0.f, EUnitDamageType::Magical,
        EUnitDamageSource::DamageOverTime, EffectId, ActionId, false).OffenseMultiplier;
    Effect.MagicResistanceReduction = Reduction;
    Effect.Interval = SecondsPerTick;
    Effect.RemainingSeconds = Effect.Interval * TickCount;
    if (bStatsChanged) RecalculateStats();
    return true;
}

bool ABattleUnitBase::ApplyPeriodicHealing(FName EffectId, ABattleUnitBase* Source, float HealingPerTick,
    int32 TickCount, float AttackDamageBonus, float AbilityPowerBonus)
{
    if (!bInCombat || !IsAlive() || !IsValid(Source) || !Source->bInCombat || !Source->IsAlive()
        || Source->TeamType != TeamType || Source->GetWorld() != GetWorld() || EffectId.IsNone() || TickCount <= 0) return false;

    FPeriodicEffect& Effect = PeriodicEffects.FindOrAdd(EffectId);
    const bool bRefresh = Effect.Revision != 0;
    if (bRefresh && !Effect.bHealing) return false;
    const float Healing = Positive(HealingPerTick);
    // 같은 축복은 가장 큰 회복량을 유지하고 다음 회복까지의 시간은 보존한다.
    if (!bRefresh || Healing >= Effect.AmountPerTick)
    {
        Effect.Source = Source;
        Effect.SourceGeneration = Source->CombatGeneration;
        Effect.AmountPerTick = Healing;
    }
    const float NewADBonus = FMath::Max(Effect.AttackDamageBonus, Positive(AttackDamageBonus));
    const float NewAPBonus = FMath::Max(Effect.AbilityPowerBonus, Positive(AbilityPowerBonus));
    const bool bStatsChanged = NewADBonus != Effect.AttackDamageBonus || NewAPBonus != Effect.AbilityPowerBonus;
    Effect.AttackDamageBonus = NewADBonus;
    Effect.AbilityPowerBonus = NewAPBonus;
    Effect.bHealing = true;
    Effect.TimeUntilTick = bRefresh ? FMath::Max(0.0, Effect.TimeUntilTick) : 1.0;
    Effect.Revision = ++PeriodicEffectRevision;
    Effect.Interval = 1.0;
    Effect.RemainingSeconds = TickCount;
    if (bStatsChanged) RecalculateStats();
    return true;
}

float ABattleUnitBase::GetAbilityPowerWithoutPeriodicEffect(FName EffectId) const
{
    const FPeriodicEffect* Effect = PeriodicEffects.Find(EffectId);
    const float ExcludedBonus = Effect ? Effect->AbilityPowerBonus : 0.f;
    return AbilityPowerBeforePeriodicBonuses * (1.f + PeriodicAbilityPowerBonus - ExcludedBonus);
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
    if (Actual > 0.f)
    {
        if (RefreshAttackSpeed()) OnStatsChanged.Broadcast(this);
        OnHealthChanged.Broadcast(this);
        OnHealed.Broadcast(this, Healer, Actual);
    }
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
    const bool bAttackSpeedChanged = Result.HealthDamage > 0.f && RefreshAttackSpeed();
    Result.bKilled = CurrentHP <= 0.f;
    ABattleUnitBase* Contributor = Request.Attacker;
    if (Result.ShieldDamage + Result.HealthDamage > 0.f && IsValid(Contributor) && Contributor->bInCombat
        && Contributor->TeamType != TeamType && Contributor->GetWorld() == GetWorld())
        DamageContributors.Add(TWeakObjectPtr<ABattleUnitBase>(Contributor), Contributor->CombatGeneration);
    if (Result.bKilled) Die();
    // 원자적 체력 갱신을 마친 뒤 트리거의 추가 피해는 별도 요청으로 허용한다.
    bResolvingDamage = false;
    RefreshShieldTotal();
    if (bAttackSpeedChanged) OnStatsChanged.Broadcast(this);
    OnHealthChanged.Broadcast(this);
    // 보호막 피해도 피격 마나에 포함한다. 과잉 살상 피해는 포함하지 않는다.
    if (IsAlive()) GainMana(FMath::Min((Result.ShieldDamage + Result.HealthDamage) * Positive(CombatPolicy.DamageTakenManaRatio), Positive(CombatPolicy.DamageTakenManaCapPerHit)));
    OnDamageReceived.Broadcast(this, Request, Result);
    if (bInCombat && IsAlive()) DispatchTrigger(EUnitTrigger::DamageTaken, Request, Result);
    ABattleUnitBase* Source = Request.Attacker;
    if (IsValid(Source) && Source->bInCombat && Source->IsAlive())
    {
        Source->DispatchTrigger(EUnitTrigger::DamageDealt, Request, Result);
        if (Result.bKilled) Source->DispatchTrigger(EUnitTrigger::Kill, Request, Result);
    }
    return Result;
}


TArray<UUnitSkillBase*> ABattleUnitBase::GetEquippedSkills() const
{
    TArray<UUnitSkillBase*> Result;
    for (UUnitSkillBase* Skill : SkillInstances) Result.Add(Skill);
    return Result;
}

bool ABattleUnitBase::CaptureSkillProgress(TArray<FUnitSkillProgress>& States) const
{
    TArray<FUnitSkillProgress> Result;
    for (UUnitSkillBase* Skill : SkillInstances)
    {
        FUnitSkillProgress State = Skill->CapturePersistentProgress();
        if (!State.SkillId.IsNone()) Result.Add(MoveTemp(State));
    }
    if (!bInitialized || !UnitSkillProgress::Validate(Result)) return false;
    States = MoveTemp(Result); return true;
}

bool ABattleUnitBase::CanRestoreSkillProgress(const TArray<FUnitSkillProgress>& States) const
{
    if (!bInitialized || bInCombat || !UnitSkillProgress::Validate(States)) return false;
    for (const FUnitSkillProgress& State : States)
    {
        const auto* Skill = SkillInstances.FindByPredicate([&](const UUnitSkillBase* S) { return S->Settings.SkillId == State.SkillId; });
        if (!Skill || !(*Skill)->IsA(State.SkillClass) || !(*Skill)->CanRestorePersistentProgress(State)) return false;
    }
    return true;
}

bool ABattleUnitBase::RestoreSkillProgress(const TArray<FUnitSkillProgress>& States)
{
    if (!CanRestoreSkillProgress(States)) return false;
    bool bRefreshVisuals = false;
    for (const FUnitSkillProgress& State : States)
        for (UUnitSkillBase* Skill : SkillInstances)
            if (Skill->Settings.SkillId == State.SkillId)
                bRefreshVisuals |= Skill->RestorePersistentProgress(State);
    if (bRefreshVisuals) ApplyVisuals();
    return true;
}

bool ABattleUnitBase::PrepareSkillsForRound()
{
    if (!bInitialized || bInCombat || bIsDead) return false;
    bool bRefreshVisuals = false;
    for (UUnitSkillBase* Skill : SkillInstances)
        bRefreshVisuals |= Skill->AdvancePreparation();
    if (bRefreshVisuals) ApplyVisuals();
    return bRefreshVisuals;
}
bool ABattleUnitBase::EmpowerNextAttack(FName EffectId, float Multiplier, float FlatDamage, float Duration)
{
    if (!bInCombat || !IsAlive() || EffectId.IsNone() || Positive(Duration) <= 0.f) return false;
    AttackEnhancements.RemoveAll([&](const FAttackEnhancement& E) { return E.Id == EffectId; });
    FAttackEnhancement Enhancement; Enhancement.Id = EffectId;
    Enhancement.Multiplier = FMath::Max(1.f, Safe(Multiplier)); Enhancement.Flat = Positive(FlatDamage);
    Enhancement.Remaining = Positive(Duration); AttackEnhancements.Add(Enhancement); return true;
}

bool ABattleUnitBase::HasArmedBasicAttack(const UUnitSkillBase* SourceSkill) const
{
    return SourceSkill && AttackEnhancements.ContainsByPredicate([&](const FAttackEnhancement& E)
    {
        return E.bConsumeOnHit && E.SourceSkill.Get() == SourceSkill;
    });
}

bool ABattleUnitBase::ArmNextBasicAttack(UUnitSkillBase* SourceSkill, float TotalAttackDamageRatio)
{
    if (!bInCombat || !IsAlive() || !IsValid(SourceSkill) || SourceSkill->OwnerUnit != this
        || !SkillInstances.Contains(SourceSkill) || SourceSkill->Settings.SkillId.IsNone()
        || !FMath::IsFinite(TotalAttackDamageRatio) || TotalAttackDamageRatio <= 0.f || HasArmedBasicAttack(SourceSkill)) return false;
    FAttackEnhancement Enhancement;
    Enhancement.Id = SourceSkill->Settings.SkillId;
    Enhancement.bConsumeOnHit = true;
    Enhancement.TotalAttackDamageRatio = TotalAttackDamageRatio;
    Enhancement.SourceSkill = SourceSkill;
    AttackEnhancements.Add(Enhancement);
    return true;
}

void ABattleUnitBase::ReleaseArmedBasicAttack(int32 ActionId)
{
    // 비행 실패/대상 소멸은 명중이 아니다. 강화는 다음 유효한 공격에 다시 사용할 수 있다.
    if (ActionId <= 0) return;
    for (FAttackEnhancement& E : AttackEnhancements)
        if (E.bConsumeOnHit && E.PendingActionId == ActionId) E.PendingActionId = 0;
}

void ABattleUnitBase::ResolveArmedBasicAttack(const FUnitDamageRequest& Request, const FUnitDamageResult& Result)
{
    if (Request.ActionId <= 0 || Request.Attacker != this || Request.Source != EUnitDamageSource::BasicAttack) return;
    if (!Result.bApplied) { ReleaseArmedBasicAttack(Request.ActionId); return; }
    TArray<TWeakObjectPtr<UUnitSkillBase>> Sources;
    AttackEnhancements.RemoveAll([&](const FAttackEnhancement& E)
    {
        if (!E.bConsumeOnHit || E.PendingActionId != Request.ActionId) return false;
        Sources.AddUnique(E.SourceSkill);
        return true;
    });
    // 먼저 소모하여 콜백/Notify가 재진입해도 같은 타격으로 두 번 회복하지 않게 한다.
    const int32 Generation = CombatGeneration;
    for (const TWeakObjectPtr<UUnitSkillBase>& Source : Sources)
    {
        if (!bInCombat || !IsAlive() || Generation != CombatGeneration) break;
        if (UUnitSkillBase* Skill = Source.Get()) Skill->OnEmpoweredAttackHit(Request, Result);
    }
}

void ABattleUnitBase::TickEffects(float Delta)
{
    bool bStatsChanged = false, bShieldsChanged = false;
    for (FUnitTimedModifier& Effect : ActiveModifiers) Effect.RemainingSeconds -= Delta;
    bStatsChanged = ActiveModifiers.RemoveAll([](const FUnitTimedModifier& E) { return E.RemainingSeconds <= 0.f; }) > 0;
    for (FUnitShield& Shield : Shields) Shield.RemainingSeconds -= Delta;
    bShieldsChanged = Shields.RemoveAll([](const FUnitShield& S) { return S.RemainingSeconds <= 0.f; }) > 0;
    for (FAttackEnhancement& E : AttackEnhancements) if (!E.bConsumeOnHit) E.Remaining -= Delta;
    AttackEnhancements.RemoveAll([](const FAttackEnhancement& E) { return !E.bConsumeOnHit && E.Remaining <= 0.f; });
    for (UUnitSkillBase* Instance : SkillInstances) Instance->Cooldown = FMath::Max(0.f, Instance->Cooldown - Delta);
    if (bStatsChanged) RecalculateStats();
    if (bShieldsChanged) RefreshShieldTotal();
}

void ABattleUnitBase::TickPeriodicEffects(float Delta)
{
    if (PeriodicEffects.IsEmpty()) return;
    TArray<FName> EffectIds;
    PeriodicEffects.GenerateKeyArray(EffectIds);
    const int32 Generation = CombatGeneration;
    for (FName EffectId : EffectIds)
    {
        if (CombatGeneration != Generation || !bInCombat || !IsAlive()) return;
        FPeriodicEffect* Effect = PeriodicEffects.Find(EffectId);
        if (!Effect) continue;

        const uint32 Revision = Effect->Revision;
        // 큰 프레임에서도 만료 전까지 예정된 피해만 실행한다. 마지막 피해 뒤에 마저 감소를 해제한다.
        Effect->TimeUntilTick -= FMath::Min(double(Delta), Effect->RemainingSeconds);
        Effect->RemainingSeconds = FMath::Max(0.0, Effect->RemainingSeconds - Delta);
        while (Effect && Effect->Revision == Revision && Effect->TimeUntilTick <= UE_DOUBLE_SMALL_NUMBER)
        {
            Effect->TimeUntilTick += Effect->Interval;
            ABattleUnitBase* Source = Effect->Source.Get();
            FUnitDamageRequest Request;
            if (Source && Source->bInCombat && Source->CombatGeneration == Effect->SourceGeneration)
                Request.Attacker = Source;
            Request.Victim = this;
            Request.RawDamage = Effect->AmountPerTick;
            Request.OffenseMultiplier = Effect->OffenseMultiplier;
            Request.ActionId = Effect->ActionId;
            Request.SkillId = EffectId;
            Request.Type = EUnitDamageType::Magical;
            Request.Source = EUnitDamageSource::DamageOverTime;
            if (Effect->bHealing) ReceiveHealing(Effect->AmountPerTick, Request.Attacker);
            else ReceiveDamage(Request);
            if (CombatGeneration != Generation || !bInCombat || !IsAlive()) return;
            // 피해 콜백에서 상태를 제거하거나 갱신할 수 있으므로 이전 포인터를 다시 쓰지 않는다.
            Effect = PeriodicEffects.Find(EffectId);
        }
        if (Effect && Effect->Revision == Revision && Effect->RemainingSeconds <= 0.0)
        {
            const bool bStatsChanged = Effect->MagicResistanceReduction > 0.f
                || Effect->AttackDamageBonus > 0.f || Effect->AbilityPowerBonus > 0.f;
            PeriodicEffects.Remove(EffectId);
            if (bStatsChanged) RecalculateStats();
        }
    }
}

bool ABattleUnitBase::QueueTileImpact(UUnitSkillBase* Skill, const TArray<ABoardTile*>& Tiles, float Delay, int32 ActionId,
    const TArray<float>& DamageScales)
{
    if (!bInCombat || !IsAlive() || ActionState != EUnitActionState::Casting || ActiveActionId != ActionId
        || !SkillInstances.IsValidIndex(ActiveSkillIndex) || SkillInstances[ActiveSkillIndex] != Skill) return false;

    FPendingTileImpact Impact;
    if (!DamageScales.IsEmpty() && DamageScales.Num() != Tiles.Num()) return false;
    for (int32 Index = 0; Index < Tiles.Num(); ++Index)
    {
        ABoardTile* Tile = Tiles[Index];
        if (!IsValid(Tile) || Tile->OwningBoard != BoardManager || Impact.Tiles.Contains(Tile)) continue;
        Impact.Tiles.Add(Tile);
        Impact.DamageScales.Add(DamageScales.IsEmpty() ? 1.f : Positive(DamageScales[Index]));
    }
    if (Impact.Tiles.IsEmpty()) return false;

    Impact.Skill = Skill;
    Impact.Config = Skill->Settings;
    Impact.Values = CalculateEffectValues(Impact.Config);
    Skill->CustomizeEffectValues(Impact.Values);
    Impact.Context = MakeDamage(0.f, EUnitDamageType::Physical, EUnitDamageSource::Skill,
        Impact.Config.SkillId, ActionId, false);
    Impact.Remaining = FMath::Max(0.01f, Positive(Delay));
    PendingTileImpacts.Add(MoveTemp(Impact));
    return true;
}

void ABattleUnitBase::TickTileImpacts(float Delta)
{
    if (!bInCombat || PendingTileImpacts.IsEmpty()) return;
    TArray<FPendingTileImpact> Pending = MoveTemp(PendingTileImpacts);
    const int32 Generation = CombatGeneration;
    for (FPendingTileImpact& Impact : Pending)
    {
        if (!bInCombat || CombatGeneration != Generation) return;
        Impact.Remaining -= Delta;
        if (Impact.Remaining > UE_SMALL_NUMBER)
        {
            PendingTileImpacts.Add(MoveTemp(Impact));
            continue;
        }

        TArray<TPair<TWeakObjectPtr<ABattleUnitBase>, float>> Targets;
        for (int32 Index = 0; Index < Impact.Tiles.Num(); ++Index)
        {
            const TWeakObjectPtr<ABoardTile>& Tile = Impact.Tiles[Index];
            if (!Tile.IsValid()) continue;
            ABattleUnitBase* Unit = Cast<ABattleUnitBase>(Tile->OccupiedUnit);
            if (IsEnemyTarget(Unit) && Unit->CurrentTile == Tile.Get()) Targets.Emplace(Unit, Impact.DamageScales[Index]);
        }
        for (const auto& Target : Targets)
        {
            if (!bInCombat || CombatGeneration != Generation) return;
            if (UUnitSkillBase* Skill = Impact.Skill.Get(); Skill && IsEnemyTarget(Target.Key.Get()))
            {
                TArray<float> Values = Impact.Values;
                for (int32 Index = 0; Index < Values.Num(); ++Index)
                    if (Impact.Config.Effects[Index].Kind == EUnitEffectKind::Damage) Values[Index] *= Target.Value;
                Skill->ApplyImpact(Impact.Config, Target.Key.Get(), Values, Impact.Context);
            }
        }
    }
}
void ABattleUnitBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (DeltaSeconds <= 0.f || !FMath::IsFinite(DeltaSeconds)) return;
    // 마지막 적 사망으로 StopCombat이 호출된 뒤에도 사망 연출은 끝까지 진행한다.
    TickDeathVisual(DeltaSeconds);
    if (!bInCombat) return;
    TickProjectiles(DeltaSeconds);
    TickTileImpacts(DeltaSeconds);
    if (!bInCombat || !IsAlive()) return;
    if (bCombatStartPending)
    {
        bCombatStartPending = false;
        DispatchTrigger(EUnitTrigger::CombatStart, FUnitDamageRequest());
        if (!bInCombat || !IsAlive()) return;
    }
    TickEffects(DeltaSeconds);
    if (!bInCombat || !IsAlive()) return;
    TickPeriodicEffects(DeltaSeconds);
    if (!bInCombat || !IsAlive()) return;
    AttackCooldown = FMath::Max(0.f, AttackCooldown - DeltaSeconds);
    RetargetTimer = FMath::Max(0.f, RetargetTimer - DeltaSeconds);
    TickAction(DeltaSeconds);
    if (bInCombat && IsAlive()) Think();
}

bool ABattleUnitBase::IsEnemyTarget(const ABattleUnitBase* Target) const
{
    return IsValid(Target) && Target != this && Target->TeamType != TeamType && Target->IsAlive() && Target->bInCombat
        && Target->Placement == EUnitPlacement::Board && IsValid(Target->CurrentTile);
}

bool ABattleUnitBase::IsEnemyAttackTarget(const ABattleUnitBase* Target) const
{
    return IsEnemyTarget(Target) && !Target->IsCombatStealthed();
}

bool ABattleUnitBase::IsCombatStealthed() const
{
    return bInCombat && IsAlive() && ActionState == EUnitActionState::Casting
        && !bActionCommitted && StealthActionId > 0 && StealthActionId == ActiveActionId;
}

void ABattleUnitBase::BeginCastStealth(int32 ActionId)
{
    if (bInCombat && IsAlive() && ActionId > 0 && ActiveActionId == ActionId)
        StealthActionId = ActionId;
}
bool ABattleUnitBase::IsInRange(const ABattleUnitBase* Target, int32 Range) const
{
    return IsValid(Target) && HexDistance(CurrentTile, Target->CurrentTile) <= FMath::Max(0, Range);
}
void ABattleUnitBase::FindTarget()
{
    ABattleUnitBase* OldTarget = CurrentTarget;
    CurrentTarget = nullptr;
    const bool bCompareRoutes = Definition.BasicAttack.Delivery == EUnitAttackDelivery::Melee && IsValid(BoardManager);
    int32 BestSteps = MAX_int32;
    int32 BestDistance = MAX_int32;
    TArray<ABoardTile*> Path;
    for (int32 Index = 0; Index < CombatRoster.Num(); ++Index)
    {
        ABattleUnitBase* Unit = CombatRoster[Index];
        if (!IsEnemyAttackTarget(Unit) || UnreachableTargets.Contains(Index)) continue;
        const int32 Distance = HexDistance(CurrentTile, Unit->CurrentTile);
        int32 Steps = Distance;
        if (bCompareRoutes)
        {
            // 실제 보드에서 점유/예약을 피해 사거리까지 가는 칸 수를 비교한다.
            if (Distance <= FinalStats.AttackRange) Steps = 0;
            else Steps = BoardManager->FindPathToAttackRange(this, Unit, Path) ? Path.Num() : MAX_int32;
        }
        if (Steps > BestSteps) continue;
        if (Steps == BestSteps)
        {
            // 같은 칸 수로 갈 수 있으면 현재 대상을 유지한다.
            if (bCompareRoutes && Steps < MAX_int32 && CurrentTarget == OldTarget) continue;
            if (Distance >= BestDistance && !(bCompareRoutes && Steps < MAX_int32 && Unit == OldTarget)) continue;
        }
        BestSteps = Steps;
        BestDistance = Distance;
        CurrentTarget = Unit;
    }
    if (OldTarget != CurrentTarget)
    {
        for (int32 Index = 0; Index < SkillInstances.Num(); ++Index)
            if (SkillInstances[Index]->Settings.bResetCountOnTargetChange && SkillInstances.IsValidIndex(Index)) SkillInstances[Index]->Counter = 0;
        BlockedSeconds = 0.f;
    }
}
void ABattleUnitBase::Think()
{
    if (IsStunned() || ActionState != EUnitActionState::Idle) return;
    if (!IsEnemyAttackTarget(CurrentTarget))
    {
        if (!CurrentTarget && RetargetTimer > 0.f) return;
        RetargetTimer = 0.f;
        UnreachableTargets.Empty(); FindTarget();
    }
    else if (Definition.BasicAttack.Delivery == EUnitAttackDelivery::Melee && IsValid(BoardManager)
        && RetargetTimer <= 0.f && !IsInRange(CurrentTarget, FinalStats.AttackRange))
    {
        // 이동 중에는 예약을 유지하고, 도착한 뒤 다음 칸을 정하기 전에 다시 비교한다.
        FindTarget();
    }
    // 액티브는 현재 평타가 끝난 뒤 우선한다. 유효한 스킬 대상이 없으면 마나를 보존한다.
    if (TryCastSkill()) return;
    if (!IsEnemyAttackTarget(CurrentTarget)) { RetargetTimer = FMath::Max(0.05f, Positive(CombatPolicy.RetargetInterval)); return; }
    if (IsInRange(CurrentTarget, FinalStats.AttackRange)) { BlockedSeconds = 0.f; TryBasicAttack(); return; }
    if (RetargetTimer > 0.f || FinalStats.MovementSpeed <= 0.f) return;
    const int32 MoveId = ++ActionSequence;
    ABoardTile* Next = nullptr;
    const EUnitPathResult Path = RequestCombatStep(CurrentTarget, MoveId, Next);
    if (!bInCombat || !IsAlive() || IsStunned() || ActionState != EUnitActionState::Idle)
    {
        if (Path == EUnitPathResult::ReservedStep)
        {
            if (IsValid(BoardManager)) BoardManager->CancelReservedMove(this, MoveId);
            if (IsValid(Next)) MovementReservationReleased(Next, MoveId, false);
        }
        return;
    }
    if (Path == EUnitPathResult::AlreadyInRange) { TryBasicAttack(); return; }
    // 길이 막혔을 때만 재시도를 늦춘다. 타일에 도착하면 같은 틱에 다음 칸을 예약할 수 있다.
    if (Path != EUnitPathResult::ReservedStep)
        RetargetTimer = FMath::Max(0.05f, Positive(CombatPolicy.RetargetInterval));
    if (Path == EUnitPathResult::ReservedStep)
    {
        // 예약은 보드가 수행하고 유닛은 예약된 인접 빈 타일로만 이동한다.
        if (!IsValid(BoardManager) || !IsValid(Next) || Next == CurrentTile || HexDistance(CurrentTile, Next) != 1
            || !BoardManager->IsMoveReservationValid(this, MoveId, Next))
        {
            RetargetTimer = FMath::Max(0.05f, Positive(CombatPolicy.RetargetInterval));
            LastValidationMessage = TEXT("Board must reserve an empty adjacent tile before returning ReservedStep.");
            if (IsValid(BoardManager)) BoardManager->CancelReservedMove(this, MoveId);
            if (IsValid(Next)) MovementReservationReleased(Next, MoveId, false);
            return;
        }
        ActiveActionId = MoveId; ReservedTile = Next; MoveStart = GetActorLocation();
        FaceLocation(*this, Next->GetSurfaceLocation());
        UnreachableTargets.Reset();
        BlockedSeconds = 0.f; ChangeAction(EUnitActionState::Moving);
    }
    else if (Path == EUnitPathResult::Unavailable)
        LastValidationMessage = TEXT("Place the unit on a BoardManager tile, or assign the board before requesting movement.");
    else
    {
        BlockedSeconds += RetargetTimer;
        if (Path == EUnitPathResult::Unreachable || BlockedSeconds >= Positive(CombatPolicy.BlockedTargetGraceSeconds))
        {
            const int32 Index = CombatRoster.IndexOfByKey(CurrentTarget);
            if (Index != INDEX_NONE) UnreachableTargets.Add(Index);
            FindTarget();
            if (CurrentTarget) RetargetTimer = 0.f;
        }
    }
}

bool ABattleUnitBase::TryBasicAttack()
{
    if (!bInCombat || !IsAlive() || IsStunned() || ActionState != EUnitActionState::Idle || AttackCooldown > 0.f
        || FinalStats.AttackSpeed <= 0.f || !IsEnemyAttackTarget(CurrentTarget) || !IsInRange(CurrentTarget, FinalStats.AttackRange)) return false;
    // 이동 없이 공격하거나 타깃이 바뀌어도 모션/공격 시작 이벤트 전에 적을 바라본다.
    FaceLocation(*this, CurrentTarget->GetActorLocation());
    ActiveActionId = ++ActionSequence; ActiveSkillIndex = INDEX_NONE; ActionTarget = CurrentTarget;
    bActionCommitted = false; ActionElapsed = 0.f;
    bHasFollowupAttack = bFollowupAttackCommitted = bMeleeHitReported = false;
    bFollowupUsesNotify = false;
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
    ActiveAttackDamageStat = FinalStats.AttackDamage;
    ActiveAttackCriticalMultiplier = FinalStats.CriticalMultiplier;
    float FirstDamage = ActiveAttackDamageStat * Positive(Definition.BasicAttack.AttackDamageRatio) + Positive(Definition.BasicAttack.FlatDamage);
    float FollowupDamage = 0.f;
    FollowupAttackTimeRatio = 0.f;
    if (Definition.BasicAttack.Delivery == EUnitAttackDelivery::Melee)
        for (const UUnitSkillBase* Skill : SkillInstances)
            Skill->ConfigureMeleeCombo(ActiveAttackDamageStat, FinalStats.AbilityPower, FirstDamage,
                FollowupDamage, FollowupAttackTimeRatio, bFollowupUsesNotify);
    ActiveAttackDamage = MakeDamage(FirstDamage,
        EUnitDamageType::Physical, EUnitDamageSource::BasicAttack, NAME_None, Id, true);
    if (Positive(FollowupDamage) > 0.f)
    {
        bHasFollowupAttack = true;
        FollowupAttackDamage = ActiveAttackDamage;
        FollowupAttackDamage.RawDamage = FollowupDamage;
        FollowupAttackDamage.Type = EUnitDamageType::Magical;
        FollowupAttackDamage.DamageId = FGuid::NewGuid();
    }
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
    float ExtraMultiplier = 0.f, ExtraFlat = 0.f, RatioOverride = 0.f;
    for (FAttackEnhancement& E : AttackEnhancements)
    {
        if (E.bConsumeOnHit)
        {
            if (E.PendingActionId != 0) continue; // 다른 비행 중 타격이 이미 사용한 강화다.
            E.PendingActionId = ActionId;
            RatioOverride = FMath::Max(RatioOverride, E.TotalAttackDamageRatio);
        }
        else { ExtraMultiplier += FMath::Max(0.f, E.Multiplier - 1.f); ExtraFlat += E.Flat; }
    }
    // 기존 도적의 시간제 강화는 기존 방식대로 소비한다. 새 스킬 출처 강화만 명중까지 보존한다.
    AttackEnhancements.RemoveAll([](const FAttackEnhancement& E) { return !E.bConsumeOnHit; });
    const float BaseDamage = RatioOverride > 0.f ? ActiveAttackDamageStat * RatioOverride : Damage.RawDamage;
    Damage.RawDamage = BaseDamage;
    for (const UUnitSkillBase* Skill : SkillInstances)
        Skill->ModifyBasicAttackRelease(ActiveAttackDamageStat, ActiveAttackCriticalMultiplier, Damage);
    Damage.RawDamage = Positive(Damage.RawDamage * (1.f + ExtraMultiplier) + ExtraFlat);
    Damage.Victim = Target;
    if (Definition.BasicAttack.Delivery == EUnitAttackDelivery::Projectile)
    {
        FPendingProjectile Shot; Shot.bBasic = true; Shot.Target = Target; Shot.Damage = Damage;
        Shot.Speed = Definition.BasicAttack.ProjectileSpeed; Shot.Lifetime = Definition.BasicAttack.ProjectileMaxLifetime;
        Shot.Visual = Definition.BasicAttack.ProjectileVisual;
        const int32 Generation = CombatGeneration;
        if (!LaunchProjectile(MoveTemp(Shot))) ReleaseArmedBasicAttack(ActionId);
        else if (bInCombat && CombatGeneration == Generation)
            for (UUnitSkillBase* Skill : SkillInstances) Skill->OnBasicAttackReleased();
    }
    else
    {
        for (UUnitSkillBase* Skill : SkillInstances) Skill->OnBasicAttackReleased();
        TGuardValue<bool> Guard(bMeleePrimaryResolving, true);
        const FUnitDamageResult Result = Target->ReceiveDamage(Damage);
        if (ActiveActionId == ActionId) bMeleeHitReported = Result.bApplied;
        ReportBasicHit(Target, Damage, Result);
    }
    return true;
}

bool ABattleUnitBase::CommitFollowupAttackHit(int32 ActionId)
{
    if (!bInCombat || !IsAlive() || IsStunned() || ActionState != EUnitActionState::BasicAttack
        || ActiveActionId != ActionId || ActionId <= 0 || !bActionCommitted || !bHasFollowupAttack
        || bFollowupAttackCommitted || bMeleePrimaryResolving) return false;

    bFollowupAttackCommitted = true;
    ABattleUnitBase* Target = ActionTarget.Get();
    if (!IsEnemyTarget(Target) || !IsInRange(Target, FinalStats.AttackRange)) return false;
    FUnitDamageRequest Damage = FollowupAttackDamage;
    Damage.Victim = Target;
    const bool bWasReported = bMeleeHitReported;
    const FUnitDamageResult Result = Target->ReceiveDamage(Damage);
    if (!bWasReported) ReportBasicHit(Target, Damage, Result);
    return Result.bApplied;
}
void ABattleUnitBase::ReportBasicHit(ABattleUnitBase* Target, const FUnitDamageRequest& Request, const FUnitDamageResult& Result)
{
    ResolveArmedBasicAttack(Request, Result);
    if (!Result.bApplied || !bInCombat || !IsAlive()) return;
    ++BasicHitCount;
    GainMana(10.f); // 명세의 기본 공격 적중 마나. 원거리도 도착 시에만 한 번 지급한다.
    for (UUnitSkillBase* Skill : SkillInstances) Skill->OnBasicAttackLanded(Result);
    FUnitDamageRequest Context = Request; Context.Victim = Target;
    DispatchTrigger(EUnitTrigger::BasicAttackHit, Context, Result);
}
bool ABattleUnitBase::FinishAction(int32 ActionId)
{
    if (ActionId <= 0 || ActionId != ActiveActionId || ActionState == EUnitActionState::Moving) return false;
    UUnitSkillBase* Finished = ActionState == EUnitActionState::Casting && SkillInstances.IsValidIndex(ActiveSkillIndex) ? SkillInstances[ActiveSkillIndex].Get() : nullptr;
    ActiveActionId = 0; ActiveSkillIndex = INDEX_NONE; ActionTarget.Reset();
    ChangeAction(bIsDead ? EUnitActionState::Dead : EUnitActionState::Idle);
    if (Finished) Finished->OnCastFinished(ActionId);
    return true;
}
void ABattleUnitBase::TickAction(float Delta)
{
    const int32 Id = ActiveActionId;
    if (Id <= 0) return;
    if (ActionState == EUnitActionState::Moving)
    {
        if (!IsValid(BoardManager) || !BoardManager->IsMoveReservationValid(this, Id, ReservedTile)
            || !IsEnemyTarget(CurrentTarget)) { CancelAction(); return; }
        const FVector Goal = ReservedTile->GetSurfaceLocation() + FVector(0, 0, 1.f);
        FaceLocation(*this, Goal);
        SetActorLocation(FMath::VInterpConstantTo(GetActorLocation(), Goal, Delta, FinalStats.MovementSpeed));
        if (GetActorLocation().Equals(Goal, 0.1f))
        {
            if (!BoardManager->CommitReservedMove(this, Id)) { CancelAction(); return; }
            ReleaseMove(true);
            // 도착 알림에서 새 행동을 시작했으면 그 행동을 덮어쓰지 않는다.
            if (ActiveActionId == Id) { ActiveActionId = 0; ChangeAction(EUnitActionState::Idle); }
        }
        return;
    }
    ActionElapsed += Delta;
    if (ActionState == EUnitActionState::BasicAttack)
    {
        if (!Definition.BasicAttack.bUseAnimNotify && !bActionCommitted && ActionElapsed >= ActionDuration * Fraction(Definition.BasicAttack.HitTimeRatio)) CommitAttackHit(Id);
        if (Id == ActiveActionId && bHasFollowupAttack && !bFollowupUsesNotify
            && ActionElapsed >= ActionDuration * Fraction(FollowupAttackTimeRatio)) CommitFollowupAttackHit(Id);
        if (Id == ActiveActionId && ActionElapsed >= ActionDuration + Positive(CombatPolicy.NotifyTimeoutGraceSeconds))
        {
            if (!bActionCommitted) LastValidationMessage = TEXT("Attack Notify missing: action ended without dealing damage.");
            FinishAction(Id);
        }
    }
    else if (ActionState == EUnitActionState::Casting && SkillInstances.IsValidIndex(ActiveSkillIndex))
    {
        const FUnitSkillDefinition Skill = SkillInstances[ActiveSkillIndex]->Settings;
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
    if (Skill.Target == EUnitSkillTarget::CurrentEnemy) return IsEnemyAttackTarget(CurrentTarget) && IsInRange(CurrentTarget, Skill.CastRange) ? CurrentTarget.Get() : nullptr;
    ABattleUnitBase* Best = nullptr;
    float BestScore = MAX_flt;
    const auto Consider = [&](ABattleUnitBase* Unit)
    {
        if (!IsValid(Unit) || !Unit->IsAlive() || !Unit->bInCombat || Unit->Placement != EUnitPlacement::Board || !IsInRange(Unit, Skill.CastRange)) return;
        const bool bAlly = Unit->TeamType == TeamType;
        if ((Skill.Target == EUnitSkillTarget::LowestHealthAlly) != bAlly) return;
        if (!bAlly && Unit->IsCombatStealthed()) return;
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
    for (int32 Index = 0; Index < SkillInstances.Num(); ++Index)
    {
        const FUnitSkillDefinition& Skill = SkillInstances[Index]->Settings;
        if (Skill.Trigger != EUnitTrigger::ManaFull || !SkillInstances.IsValidIndex(Index) || SkillInstances[Index]->Cooldown > 0.f) continue;
        ABattleUnitBase* Candidate = SkillInstances[Index]->SelectTarget();
        if (Candidate && !SkillInstances[Index]->CanUse(Candidate)) Candidate = nullptr;
        if (Candidate && (Chosen == INDEX_NONE || Skill.Priority > SkillInstances[Chosen]->Settings.Priority)) { Chosen = Index; Target = Candidate; }
    }
    if (Chosen == INDEX_NONE) return false;
    const FUnitSkillDefinition& Skill = SkillInstances[Chosen]->Settings;
    // 자기 자신과 아군 대상 스킬은 현재 방향을 유지한다.
    if (IsEnemyTarget(Target)) FaceLocation(*this, Target->GetActorLocation());
    ActiveActionId = ++ActionSequence; ActiveSkillIndex = Chosen; ActionTarget = Target;
    ActionElapsed = 0.f; ActionDuration = FMath::Max(0.01f, Positive(Skill.CastSeconds)); bActionCommitted = false;
    PreCastMana = CurrentMana; CurrentMana = 0.f;
    const int32 CastId = ActiveActionId;
    SkillInstances[Chosen]->PrepareCast(Target, CastId);
    SkillInstances[Chosen]->Cooldown = Positive(Skill.InternalCooldown);
    ChangeAction(EUnitActionState::Casting);
    if (ActiveActionId != CastId || !SkillInstances.IsValidIndex(Chosen)) return true;
    SkillInstances[Chosen]->BeginCast(CastId);
    if (ActiveActionId != CastId || !SkillInstances.IsValidIndex(Chosen)) return true;
    if (Skill.CastMontage)
        if (UAnimInstance* Anim = Body->GetAnimInstance()) Anim->Montage_Play(Skill.CastMontage, Skill.CastMontage->GetPlayLength() / ActionDuration);
    SkillInstances[Chosen]->OnCastStarted(CastId);
    if (ActiveActionId == CastId) { OnManaChanged.Broadcast(this); OnSkillStarted.Broadcast(this, CastId); }
    return true;
}
bool ABattleUnitBase::CommitSkillEffect(int32 ActionId)
{
    if (!bInCombat || !IsAlive() || ActionState != EUnitActionState::Casting || ActiveActionId != ActionId || ActionId <= 0
        || bActionCommitted || !SkillInstances.IsValidIndex(ActiveSkillIndex)) return false;
    bActionCommitted = true;
    ABattleUnitBase* Target = ActionTarget.Get();
    // 시전 대상이 사망해도 자동 이전하지 않는다. 일반 시전 불발은 마나 미환불이 임시 정책이다.
    if (SkillInstances[ActiveSkillIndex]->RequiresLivingCastTarget()
        && (!IsValid(Target) || !Target->IsAlive() || !Target->bInCombat)) return false;
    ExecuteSkill(SkillInstances[ActiveSkillIndex]->Settings, Target, ActionId);
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
    const int32 Index = SkillInstances.IndexOfByPredicate([&](const UUnitSkillBase* S) { return S->Settings.SkillId == Skill.SkillId; });
    if (SkillInstances.IsValidIndex(Index)) SkillInstances[Index]->Execute(Target, ActionId);
}
void ABattleUnitBase::ExecuteSkillPayload(UUnitSkillBase* Instance, ABattleUnitBase* Target, int32 ActionId)
{
    const int32 Slot = SkillInstances.IndexOfByKey(Instance);
    if (!SkillInstances.IsValidIndex(Slot)) return;
    const FUnitSkillDefinition Skill = SkillInstances[Slot]->Settings;
    TArray<float> Values = CalculateEffectValues(Skill);
    Instance->CustomizeEffectValues(Values);
    const FUnitDamageRequest Context = MakeDamage(0.f, EUnitDamageType::Magical,
        Skill.Trigger == EUnitTrigger::ManaFull ? EUnitDamageSource::Skill : EUnitDamageSource::Triggered, Skill.SkillId, ActionId, false);
    if (Skill.bProjectile && Target != this)
    {
        FPendingProjectile Shot; Shot.Target = Target; Shot.Skill = Skill; Shot.SkillInstance = Instance; Shot.EffectValues = Values; Shot.Damage = Context;
        Shot.Speed = Skill.ProjectileSpeed; Shot.Lifetime = Skill.ProjectileMaxLifetime;
        Shot.Visual = Skill.ProjectileVisual;
        LaunchProjectile(MoveTemp(Shot));
    }
    else Instance->ApplyImpact(Skill, Target, Values, Context);
}
void ABattleUnitBase::DispatchTrigger(EUnitTrigger Trigger, const FUnitDamageRequest& Context, const FUnitDamageResult& Result)
{
    if (!bInCombat || !IsAlive() || Context.TriggerDepth >= FMath::Clamp(CombatPolicy.MaxTriggerDepth, 0, 32)) return;
    for (int32 Index = 0; Index < SkillInstances.Num(); ++Index)
    {
        const FUnitSkillDefinition Skill = SkillInstances[Index]->Settings;
        UUnitPassiveSkill* Passive = SkillInstances.IsValidIndex(Index) ? Cast<UUnitPassiveSkill>(SkillInstances[Index]) : nullptr;
        if (!Passive || !Passive->ShouldActivate(Trigger, Context, Result)) continue;
        ABattleUnitBase* Target = nullptr;
        if (Skill.Target == EUnitSkillTarget::CurrentEnemy)
        {
            ABattleUnitBase* Other = Trigger == EUnitTrigger::DamageTaken ? Context.Attacker.Get() : Context.Victim.Get();
            if (IsEnemyTarget(Other) && IsInRange(Other, Skill.CastRange)) Target = Other;
        }
        if (!Target) Target = Passive->SelectTarget();
        if (!Target || !Passive->CanUse(Target)) continue;
        SkillInstances[Index]->Cooldown = Positive(Skill.InternalCooldown);
        FUnitDamageRequest NextContext = Context;
        ++NextContext.TriggerDepth; NextContext.TriggerChain.Add(Skill.SkillId);
        TGuardValue<FUnitDamageRequest> Guard(TriggerContext, NextContext);
        ExecuteSkill(Skill, Target, ++ActionSequence);
        if (!bInCombat || !IsAlive()) break;
    }
}

FUnitProjectileView ABattleUnitBase::MakeProjectileView(const FPendingProjectile& Projectile) const
{
    // 생성 순간과 비행 중에 같은 정보를 전달한다. 현재 행동으로 탄 종류를 추측하지 않는다.
    FUnitProjectileView View;
    View.ProjectileId = Projectile.Id;
    View.Position = Projectile.Position;
    View.Rotation = Projectile.Rotation;
    View.Target = Projectile.Target.Get();
    View.bBasicAttack = Projectile.bBasic;
    View.SkillId = Projectile.bBasic ? Projectile.Damage.SkillId : Projectile.Skill.SkillId;
    View.Visual = Projectile.Visual;
    return View;
}

FVector ABattleUnitBase::GetBasicProjectileLaunchLocation(const FVector& BodyFallback)
{
    const FUnitProjectileOrigin& Origin = Definition.BasicAttack.ProjectileOrigin;
    const FVector Offset = Origin.LocalOffset.ContainsNaN() ? FVector::ZeroVector : Origin.LocalOffset;
    if (Origin.Component == EUnitProjectileOriginComponent::Body && Origin.SocketName.IsNone())
        return BodyFallback + GetActorQuat().RotateVector(Offset);

    USceneComponent* Component = Body;
    if (Origin.Component != EUnitProjectileOriginComponent::Body)
    {
        UStaticMeshComponent* Weapon = Origin.Component == EUnitProjectileOriginComponent::MainHand ? MainHand : OffHand;
        Component = IsValid(Weapon) && Weapon->GetStaticMesh() && Weapon->IsVisible() ? Weapon : nullptr;
    }
    if (!IsValid(Component) || (!Origin.SocketName.IsNone() && !Component->DoesSocketExist(Origin.SocketName)))
    {
        LastValidationMessage = FString::Printf(TEXT("Basic projectile origin unavailable: %s. Using body launch point."),
            *Origin.SocketName.ToString());
        return BodyFallback;
    }
    const FTransform Transform = Origin.SocketName.IsNone()
        ? Component->GetComponentTransform() : Component->GetSocketTransform(Origin.SocketName);
    const FVector Location = Transform.TransformPosition(Offset);
    return Location.ContainsNaN() ? BodyFallback : Location;
}

bool ABattleUnitBase::LaunchProjectile(FPendingProjectile Projectile)
{
    if (!Projectile.Target.IsValid() || Positive(Projectile.Speed) <= 0.f || Positive(Projectile.Lifetime) <= 0.f)
    {
        LastValidationMessage = TEXT("Projectile needs a valid target, positive speed and lifetime."); return false;
    }
    Projectile.Id = ++ActionSequence; Projectile.TargetGeneration = Projectile.Target->CombatGeneration;
    const FVector BodyLaunchPoint = GetActorLocation() + FVector(0, 0, GetHealthBarHeight() * 0.5f);
    Projectile.Position = Projectile.bBasic ? GetBasicProjectileLaunchLocation(BodyLaunchPoint) : BodyLaunchPoint;
    const FVector Goal = Projectile.Target->GetActorLocation() + FVector(0, 0, Projectile.Target->GetHealthBarHeight() * 0.5f);
    const FVector Direction = Goal - Projectile.Position;
    if (!Direction.IsNearlyZero()) Projectile.Rotation = Direction.Rotation();
    Projectile.Speed = Positive(Projectile.Speed); Projectile.Lifetime = Positive(Projectile.Lifetime);
    Projectiles.Add(Projectile);
    ProjectileUpdated(MakeProjectileView(Projectile));
    return true;
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
        { ProjectileRemoved(Shot.Id, false); if (Shot.bBasic) ReleaseArmedBasicAttack(Shot.Damage.ActionId); continue; }
        const FVector Goal = Target->GetActorLocation() + FVector(0, 0, Target->GetHealthBarHeight() * 0.5f);
        const FVector PreviousPosition = Shot.Position;
        Shot.Position = FMath::VInterpConstantTo(Shot.Position, Goal, Delta, Shot.Speed);
        // 타깃이 움직여 경로가 바뀌면 실제 이번 프레임 이동 방향으로 탄을 돌린다.
        const FVector Travel = Shot.Position - PreviousPosition;
        if (!Travel.IsNearlyZero()) Shot.Rotation = Travel.Rotation();
        if (Shot.Position.Equals(Goal, 0.1f))
        {
            ProjectileRemoved(Shot.Id, true);
            if (!bInCombat || Generation != CombatGeneration) continue;
            if (Shot.bBasic)
            {
                const FUnitDamageResult Result = Target->ReceiveDamage(Shot.Damage);
                ReportBasicHit(Target, Shot.Damage, Result);
            }
            else if (UUnitSkillBase* Instance = Shot.SkillInstance.Get()) Instance->ApplyImpact(Shot.Skill, Target, Shot.EffectValues, Shot.Damage);
        }
        else
        {
            Projectiles.Add(Shot); ProjectileUpdated(MakeProjectileView(Shot));
        }
    }
}


#if WITH_DEV_AUTOMATION_TESTS
// 기존 전투 규칙 회귀 검사. 클래스 기반 스킬 검사는 별도 검사와 함께 실행한다.
// 게임 Shipping 빌드에는 포함되지 않는다. 화면/실제 BP 연결은 별도 에디터 확인 대상이다.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Tests/UnitSkillTestTypes.h"
#include "Skills/ArmorShredSkill.h"
#include "Skills/BerserkerRageSkill.h"
#include "Skills/PriestHealingLightSkill.h"
#include "Skills/ImperialBlessingSkill.h"
#include "Skills/IronwallGuardSkill.h"
#include "Skills/ThunderMageLightningSkill.h"
#include "Skills/ElementalistSkill.h"
#include "Skills/SpellbladeSkills.h"
#include "Skills/SniperPrecisionShotSkill.h"
#include "Skills/ShamanWeakeningCurseSkill.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/Material.h"
namespace UnitCombatTests
{
    // 검사 설정을 보관하는 로컬 타입. 실제 DT 구조체에는 포함하지 않는다.
    struct FTestUnitData : FUnitDefinition { TArray<FUnitSkillDefinition> TestSkills; };
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
        ABattleUnitBase* Spawn(FName Name, FTestUnitData Data, EBattleTeam Team, int32 Q)
        {
            for (const FUnitSkillDefinition& Config : Data.TestSkills)
                Data.SkillClasses.Add(Config.Trigger == EUnitTrigger::ManaFull ? UUnitSkillTestActive::StaticClass() : UUnitSkillTestPassive::StaticClass());
            Table->AddRow(Name, static_cast<const FUnitDefinition&>(Data));
            ABattleUnitBase* Unit = World->SpawnActor<ABattleUnitBase>();
            FDataTableRowHandle Handle; Handle.DataTable = Table; Handle.RowName = Name;
            FString Error;
            if (!Unit->InitializeUnit(Handle, Team, 1, Error)) return nullptr;
            const TArray<UUnitSkillBase*> Instances = Unit->GetEquippedSkills();
            for (int32 Index = 0; Index < Data.TestSkills.Num(); ++Index) Instances[Index]->Settings = Data.TestSkills[Index];
            ABoardTile* Tile = World->SpawnActor<ABoardTile>();
            Tile->HexQ = Q; Tile->HexR = 0;
            Tile->TeamType = Team == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
            Tile->SetActorLocation(FVector(Q * 500.f, 0, 0));
            return Unit->PlaceOnTile(Tile) ? Unit : nullptr;
        }
        static FTestUnitData UnitData()
        {
            FTestUnitData Data;
            Data.BaseStats.MaxHealth = 1000.f; Data.BaseStats.AttackDamage = 100.f;
            Data.BaseStats.AttackRange = 3; Data.BaseStats.CriticalChance = 0.f;
            FUnitSkillDefinition Skill; Skill.SkillId = TEXT("Shield"); Skill.Target = EUnitSkillTarget::Self;
            FUnitSkillEffect Shield; Shield.Kind = EUnitEffectKind::Shield; Shield.BaseValue = 200.f;
            Skill.Effects.Add(Shield); Data.TestSkills.Add(Skill);
            return Data;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBerserkerRageTest, "ProjectA.Unit.BerserkerRage.HealthAndAttackSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBerserkerRageTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    for (const int32 Stars : {1, 2})
    {
        FFixture F;
        FTestUnitData Data;
        Data.BaseStats.MaxHealth = 750.f;
        Data.BaseStats.AttackSpeed = 0.75f;
        Data.SkillClasses.Add(UBerserkerRageSkill::StaticClass());
        ABattleUnitBase* Unit = F.Spawn(TEXT("Berserker"), Data, EBattleTeam::Player, 0);
        if (!TestNotNull(TEXT("Berserker spawned"), Unit)) return false;
        TestTrue(TEXT("Set star level"), Unit->SetStarLevel(Stars));
        Unit->StartCombat();
        const float MaxHP = Unit->FinalStats.MaxHealth;
        const auto Damage = [Unit](float Amount)
        {
            FUnitDamageRequest Request;
            Request.Type = EUnitDamageType::TrueDamage;
            Request.RawDamage = Amount;
            return Unit->ReceiveDamage(Request);
        };
        const auto CheckSpeed = [this, Unit](const TCHAR* Description, float Expected)
        {
            TestTrue(Description, FMath::IsNearlyEqual(Unit->FinalStats.AttackSpeed, Expected, 0.0001f));
        };
        TestFalse(TEXT("Passive-only unit does not use mana"), Unit->UsesMana());
        CheckSpeed(TEXT("Full health uses base attack speed"), 0.75f);
        Damage(MaxHP * 0.5f);
        CheckSpeed(TEXT("Half health grants 40 percent attack speed"), 1.05f);
        Unit->AddShield(TEXT("Support"), 50.f, 4.f);
        TestEqual(TEXT("Shield absorbs the next hit"), Damage(50.f).HealthDamage, 0.f);
        CheckSpeed(TEXT("Shield does not affect health-based bonus"), 1.05f);
        Damage(MaxHP * 0.4f);
        CheckSpeed(TEXT("Low health bonus caps at 64 percent"), 1.23f);
        Unit->ReceiveHealing(MaxHP * 0.65f, Unit);
        CheckSpeed(TEXT("Healing to 75 percent health lowers the bonus"), 0.9f);
        Unit->SetOvertimeBonuses(0.2f, 1.f);
        CheckSpeed(TEXT("Passive and other attack speed bonuses add before multiplying base"), 1.05f);
        Unit->ReceiveHealing(MaxHP, Unit);
        CheckSpeed(TEXT("Full heal removes the passive bonus"), 0.9f);
        Damage(MaxHP * 0.5f);
        Unit->StopCombat();
        TestTrue(TEXT("Reset into next round"), Unit->ResetForRound());
        CheckSpeed(TEXT("Round reset restores base attack speed"), 0.75f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBerserkerLifestealTest, "ProjectA.Unit.BerserkerRage.BasicAttackLifesteal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBerserkerLifestealTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    for (const int32 Stars : {1, 2})
    {
        FFixture F;
        FTestUnitData Data;
        Data.BaseStats.MaxHealth = 1000.f;
        Data.BaseStats.AttackDamage = 100.f;
        Data.BaseStats.AttackSpeed = 1.f;
        Data.BaseStats.CriticalChance = 0.f;
        Data.BasicAttack.DamageType = EUnitDamageType::TrueDamage;
        ABattleUnitBase* Target = F.Spawn(TEXT("Target"), Data, EBattleTeam::Enemy, 1);
        Data.SkillClasses.Add(UBerserkerRageSkill::StaticClass());
        ABattleUnitBase* Unit = F.Spawn(TEXT("Berserker"), Data, EBattleTeam::Player, 0);
        if (!TestNotNull(TEXT("Berserker spawned"), Unit) || !TestNotNull(TEXT("Target spawned"), Target)) return false;

        TestTrue(TEXT("Set star level"), Unit->SetStarLevel(Stars));
        Unit->StartCombat();
        Target->StartCombat();
        Unit->CurrentTarget = Target;

        FUnitDamageRequest Damage;
        Damage.Type = EUnitDamageType::TrueDamage;
        Damage.RawDamage = 300.f;
        Unit->ReceiveDamage(Damage);
        const float MissingHealthBaseline = Unit->CurrentHP;
        Target->AddShield(TEXT("TestShield"), Unit->FinalStats.AttackDamage, 10.f);
        TestTrue(TEXT("Shielded attack starts"), Unit->TryBasicAttack());
        int32 AttackId = Unit->ActiveActionId;
        TestTrue(TEXT("Shielded attack lands"), Unit->CommitAttackHit(AttackId));
        TestEqual(TEXT("Fully blocked hit gives no lifesteal"), Unit->CurrentHP, MissingHealthBaseline);
        Unit->FinishAction(AttackId);

        Unit->Tick(1.f);
        const float TargetHP = Target->CurrentHP;
        AttackId = Unit->ActiveActionId;
        TestTrue(TEXT("Next attack lands"), Unit->CommitAttackHit(AttackId));
        TestFalse(TEXT("Duplicate notify is rejected"), Unit->CommitAttackHit(AttackId));
        const float HealedHP = Unit->CurrentHP;
        TestTrue(TEXT("Heals 10 percent of the target's lost health"),
            Target->CurrentHP < TargetHP &&
            FMath::IsNearlyEqual(HealedHP, MissingHealthBaseline + (TargetHP - Target->CurrentHP) * 0.1f, 0.001f));

        Damage.Attacker = Unit;
        Damage.Source = EUnitDamageSource::Skill;
        Damage.RawDamage = 40.f;
        TestTrue(TEXT("Skill damage applied"), Target->ReceiveDamage(Damage).HealthDamage > 0.f);
        TestEqual(TEXT("Skill damage gives no lifesteal"), Unit->CurrentHP, HealedHP);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPriestHealingLightTest, "ProjectA.Unit.PriestHealingLight.TargetAndScaling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPriestHealingLightTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    for (const int32 Stars : {1, 2})
    {
        for (const float AP : {100.f, 200.f})
        {
            FFixture F;
            FTestUnitData PriestData;
            PriestData.BaseStats.MaxHealth = 500.f;
            PriestData.BaseStats.AbilityPower = AP;
            PriestData.BaseStats.AttackRange = 3;
            PriestData.BaseStats.StartMana = 30.f;
            PriestData.BaseStats.MaxMana = 70.f;
            PriestData.SkillClasses.Add(UPriestHealingLightSkill::StaticClass());
            FTestUnitData AllyData;
            AllyData.BaseStats.MaxHealth = 2000.f;
            ABattleUnitBase* Priest = F.Spawn(TEXT("Priest"), PriestData, EBattleTeam::Player, 0);
            ABattleUnitBase* FarAlly = F.Spawn(TEXT("FarAlly"), AllyData, EBattleTeam::Player, 10);
            ABattleUnitBase* Enemy = F.Spawn(TEXT("Enemy"), AllyData, EBattleTeam::Enemy, 11);
            if (!Priest || !FarAlly || !Enemy) { AddError(TEXT("Could not deploy healing test units")); return false; }
            TestTrue(TEXT("Set priest star level"), Priest->SetStarLevel(Stars));
            Priest->SetCombatRoster({FarAlly, Enemy});
            Priest->StartCombat();
            FarAlly->StartCombat();
            Enemy->StartCombat();
            Priest->GainMana(40.f);
            TestFalse(TEXT("Full health allies do not trigger a cast"), Priest->TryCastSkill());
            TestEqual(TEXT("No cast preserves full mana"), Priest->CurrentMana, 70.f);

            const auto Damage = [](ABattleUnitBase* Victim, float Amount)
            {
                FUnitDamageRequest Request;
                Request.Type = EUnitDamageType::TrueDamage;
                Request.RawDamage = Amount;
                Victim->ReceiveDamage(Request);
            };
            Damage(Priest, 100.f);
            Damage(FarAlly, 1500.f);
            Damage(Enemy, 1900.f);
            const float PriestHP = Priest->CurrentHP;
            if (!TestTrue(TEXT("Can cast at a distant injured ally"), Priest->TryCastSkill())) return false;
            TestEqual(TEXT("Mana consumed once"), Priest->CurrentMana, 0.f);
            const int32 CastId = Priest->ActiveActionId;
            TestTrue(TEXT("Heal commits on the selected ally"), Priest->CommitSkillEffect(CastId));
            TestFalse(TEXT("Duplicate commit cannot heal again"), Priest->CommitSkillEffect(CastId));
            Priest->FinishAction(CastId);
            const float ExpectedHeal = (Stars == 2 ? 540.f : 300.f) * (AP / 100.f);
            TestEqual(TEXT("Lowest health ratio wins across the board and scales with AP"), FarAlly->CurrentHP, 500.f + ExpectedHeal);
            TestEqual(TEXT("Lower absolute HP alone does not determine the target"), Priest->CurrentHP, PriestHP);
            TestEqual(TEXT("Enemy with a lower health ratio is excluded"), Enemy->CurrentHP, 100.f);

            FarAlly->ReceiveHealing(2000.f, Priest);
            Priest->GainMana(70.f);
            if (!TestTrue(TEXT("Can target self if the rest of the team is full"), Priest->TryCastSkill())) return false;
            TestTrue(TEXT("Self heal commits"), Priest->CommitSkillEffect(Priest->ActiveActionId));
            TestEqual(TEXT("Healing caps at maximum health"), Priest->CurrentHP, Priest->FinalStats.MaxHealth);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShamanCurseTest, "ProjectA.Unit.ShamanCurse.SharedTargetAndCadence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShamanCurseTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData ShamanData;
    ShamanData.BaseStats.AbilityPower = 100.f;
    ShamanData.BaseStats.MaxMana = 40.f;
    ShamanData.SkillClasses = {UShamanWeakeningCurseSkill::StaticClass()};
    FTestUnitData EnemyData;
    EnemyData.BaseStats.MaxHealth = 1000.f;
    EnemyData.BaseStats.MagicResistance = 100.f;
    ABattleUnitBase* First = F.Spawn(TEXT("FirstShaman"), ShamanData, EBattleTeam::Player, 0);
    ShamanData.BaseStats.AbilityPower = 200.f;
    ABattleUnitBase* Second = F.Spawn(TEXT("SecondShaman"), ShamanData, EBattleTeam::Player, -1);
    ABattleUnitBase* Near = F.Spawn(TEXT("Near"), EnemyData, EBattleTeam::Enemy, 1);
    ABattleUnitBase* Other = F.Spawn(TEXT("Other"), EnemyData, EBattleTeam::Enemy, 2);
    ABattleUnitBase* Far = F.Spawn(TEXT("Far"), EnemyData, EBattleTeam::Enemy, 4);
    if (!First || !Second || !Near || !Other || !Far) { AddError(TEXT("Could not deploy curse test units")); return false; }
    Second->SetStarLevel(2);
    First->SetCombatRoster({Near, Other, Far});
    Second->SetCombatRoster({Near, Other, Far});
    for (ABattleUnitBase* Unit : {First, Second, Near, Other, Far}) Unit->StartCombat();
    First->CurrentTarget = Near;
    Second->CurrentTarget = Near;
    const FName CurseId(TEXT("ShamanWeakeningCurse"));

    const auto Cast = [](ABattleUnitBase* Unit)
    {
        Unit->GainMana(40.f);
        if (!Unit->TryCastSkill()) return false;
        const int32 Id = Unit->ActiveActionId;
        const bool bCommitted = Unit->CommitSkillEffect(Id);
        Unit->FinishAction(Id);
        return bCommitted;
    };
    if (!TestTrue(TEXT("First shaman curses current attack target"), Cast(First))) return false;
    TestEqual(TEXT("Curse applies resistance reduction immediately"), Near->FinalStats.MagicResistance, 70.f);
    TestEqual(TEXT("No immediate damage"), Near->CurrentHP, 1000.f);
    if (!TestTrue(TEXT("Second shaman can curse the other enemy"), Cast(Second))) return false;
    TestEqual(TEXT("Second shaman sees first shaman's curse"), Other->GetPeriodicDamageRemaining(CurseId), 6.f);
    TestTrue(TEXT("Casting leaves the basic attack target unchanged"), Second->CurrentTarget == Near);
    Near->Tick(1.f);
    TestTrue(TEXT("The first tick is 20 damage before reduced MR"),
        FMath::IsNearlyEqual(Near->CurrentHP, 1000.f - 20.f / 1.7f, 0.001f));
    Near->Tick(0.75f);
    const float HPBeforeRefresh = Near->CurrentHP;

    if (!TestTrue(TEXT("All in range cursed means refresh shortest duration"), Cast(Second))) return false;
    TestEqual(TEXT("Shorter curse was refreshed"), Near->GetPeriodicDamageRemaining(CurseId), 6.f);
    TestTrue(TEXT("Out of range enemy stays uncursed"), Far->GetPeriodicDamageRemaining(CurseId) < 0.f);
    TestEqual(TEXT("Refresh does not deal instant damage"), Near->CurrentHP, HPBeforeRefresh);
    Near->Tick(0.25f);
    const float NewTickDamage = 72.f / 1.7f;
    TestTrue(TEXT("Refresh preserves next tick and uses latest caster's star and AP"),
        FMath::IsNearlyEqual(Near->CurrentHP, HPBeforeRefresh - NewTickDamage, 0.001f));

    FUnitDamageRequest Fatal;
    Fatal.Type = EUnitDamageType::TrueDamage;
    Fatal.RawDamage = 10000.f;
    TestTrue(TEXT("Refreshing caster dies"), Second->ReceiveDamage(Fatal).bKilled);
    Near->Tick(6.f);
    TestTrue(TEXT("Six refreshed ticks complete without stacking despite the caster's death"),
        FMath::IsNearlyEqual(Near->CurrentHP, HPBeforeRefresh - 6.f * NewTickDamage, 0.001f));
    TestTrue(TEXT("Expired curse is no longer visible to targeting"), Near->GetPeriodicDamageRemaining(CurseId) < 0.f);
    TestEqual(TEXT("Resistance restored after final tick"), Near->FinalStats.MagicResistance, 100.f);

    FUnitStatModifier Stronger;
    Stronger.MagicResistanceReduction = 0.5f;
    Other->AddModifier(TEXT("Stronger"), Stronger, 10.f);
    TestEqual(TEXT("Only strongest MR reduction applies"), Other->FinalStats.MagicResistance, 50.f);
    Other->StopCombat();
    TestTrue(TEXT("Round stop clears pending ticks"), Other->GetPeriodicDamageRemaining(CurseId) < 0.f);
    TestEqual(TEXT("Round stop clears resistance reduction"), Other->FinalStats.MagicResistance, 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmorShredOnHitTest, "ProjectA.Unit.ArmorShred.AppliesAfterBasicAttack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArmorShredOnHitTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData AttackerData;
    AttackerData.BaseStats.AttackDamage = 100.f;
    AttackerData.BaseStats.AttackSpeed = 0.55f;
    AttackerData.BaseStats.CriticalChance = 0.f;
    AttackerData.SkillClasses = {UArmorShredSkill::StaticClass()};
    FTestUnitData TargetData;
    TargetData.BaseStats.MaxHealth = 1000.f;
    TargetData.BaseStats.Armor = 100.f;
    ABattleUnitBase* Attacker = F.Spawn(TEXT("Sunderer"), AttackerData, EBattleTeam::Player, 0);
    ABattleUnitBase* Target = F.Spawn(TEXT("Target"), TargetData, EBattleTeam::Enemy, 1);
    if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Target"), Target)) return false;
    Attacker->StartCombat();
    Target->StartCombat();
    TestFalse(TEXT("Only a passive is equipped"), Attacker->UsesMana());

    FUnitDamageRequest Spell;
    Spell.Attacker = Attacker;
    Spell.Source = EUnitDamageSource::Skill;
    Spell.RawDamage = 10.f;
    Spell.Type = EUnitDamageType::TrueDamage;
    Target->ReceiveDamage(Spell);
    TestEqual(TEXT("Skill damage does not shred armor"), Target->FinalStats.Armor, 100.f);

    const auto Hit = [Attacker, Target]
    {
        Attacker->CurrentTarget = Target;
        if (!Attacker->TryBasicAttack()) return false;
        const int32 ActionId = Attacker->ActiveActionId;
        const bool bHit = Attacker->CommitAttackHit(ActionId);
        Attacker->FinishAction(ActionId);
        Attacker->CurrentTarget = nullptr;
        return bHit;
    };
    const float BeforeFirstHit = Target->CurrentHP;
    if (!TestTrue(TEXT("First basic attack lands"), Hit())) return false;
    TestEqual(TEXT("First hit uses original armor"), Target->CurrentHP, BeforeFirstHit - 50.f);
    TestEqual(TEXT("Armor drops by 30 percent after the first hit"), Target->FinalStats.Armor, 70.f);

    Attacker->Tick(3.f);
    Target->Tick(3.f);
    const float BeforeSecondHit = Target->CurrentHP;
    if (!TestTrue(TEXT("Second basic attack lands"), Hit())) return false;
    TestTrue(TEXT("Second hit uses the already reduced armor"),
        FMath::IsNearlyEqual(BeforeSecondHit - Target->CurrentHP, 100.f / 1.7f, 0.001f));
    if (TestEqual(TEXT("Same effect does not stack"), Target->ActiveModifiers.Num(), 1))
        TestEqual(TEXT("Another hit refreshes the full four seconds"), Target->ActiveModifiers[0].RemainingSeconds, 4.f);

    FUnitStatModifier Stronger;
    Stronger.ArmorReduction = 0.5f;
    Target->AddModifier(TEXT("Stronger"), Stronger, 1.f);
    TestEqual(TEXT("Strongest separate armor reduction wins"), Target->FinalStats.Armor, 50.f);
    Target->Tick(1.5f);
    TestEqual(TEXT("Refreshed shred outlasts the earlier expiration"), Target->FinalStats.Armor, 70.f);
    Target->Tick(2.5f);
    TestEqual(TEXT("Armor returns to its prior value after four seconds"), Target->FinalStats.Armor, 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImperialBlessingTest, "ProjectA.Unit.ImperialBlessing.PeriodicHealingAndRefresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FImperialBlessingTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData Data;
    Data.BaseStats.MaxHealth = 1000.f;
    Data.BaseStats.AttackDamage = 100.f;
    Data.BaseStats.AbilityPower = 100.f;
    ABattleUnitBase* Ally = F.Spawn(TEXT("Ally"), Data, EBattleTeam::Player, 0);
    ABattleUnitBase* Enemy = F.Spawn(TEXT("Enemy"), Data, EBattleTeam::Enemy, 1);
    Data.SkillClasses = {UImperialBlessingSkill::StaticClass()};
    ABattleUnitBase* Strong = F.Spawn(TEXT("Strong"), Data, EBattleTeam::Player, -1);
    ABattleUnitBase* Weak = F.Spawn(TEXT("Weak"), Data, EBattleTeam::Player, -2);
    if (!Ally || !Enemy || !Strong || !Weak) { AddError(TEXT("Could not spawn blessing test units")); return false; }
    Strong->SetStarLevel(2);
    for (ABattleUnitBase* Caster : {Strong, Weak}) Caster->SetCombatRoster({Ally, Enemy, Strong, Weak});
    for (ABattleUnitBase* Unit : {Ally, Enemy, Strong, Weak}) Unit->StartCombat();

    const auto Cast = [](ABattleUnitBase* Caster)
    {
        Caster->GainMana(Caster->FinalStats.MaxMana);
        if (!Caster->TryCastSkill()) return false;
        const int32 Id = Caster->ActiveActionId;
        const bool bCommitted = Caster->CommitSkillEffect(Id);
        Caster->FinishAction(Id);
        return bCommitted;
    };
    if (!TestTrue(TEXT("Buff can cast when all allies are full"), Cast(Weak))) return false;
    TestEqual(TEXT("Caster gains the AP buff"), Strong->FinalStats.AbilityPower, 110.f);
    TestEqual(TEXT("Team AD buff is applied"), Ally->FinalStats.AttackDamage, 110.f);
    TestEqual(TEXT("Enemy is not buffed"), Enemy->FinalStats.AttackDamage, 100.f);
    Ally->CurrentHP = 600.f;
    Ally->Tick(0.75f);
    TestEqual(TEXT("No instant or early healing"), Ally->CurrentHP, 600.f);
    if (!TestTrue(TEXT("Second caster refreshes existing blessing"), Cast(Strong))) return false;
    Ally->Tick(0.25f);
    TestEqual(TEXT("Existing tick cadence kept and blessing AP excluded"), Ally->CurrentHP, 645.f);
    if (!TestTrue(TEXT("Weaker recast refreshes the blessing"), Cast(Weak))) return false;
    TestEqual(TEXT("Same blessing never stacks stats"), Ally->FinalStats.AttackDamage, 110.f);

    FUnitDamageRequest Fatal;
    Fatal.RawDamage = 10000.f;
    Fatal.Type = EUnitDamageType::TrueDamage;
    TestTrue(TEXT("Strong caster dies"), Strong->ReceiveDamage(Fatal).bKilled);
    Ally->Tick(4.f);
    TestEqual(TEXT("Four refreshed ticks use stronger amount after caster dies"), Ally->CurrentHP, 825.f);
    TestEqual(TEXT("Final healing tick is followed by AD removal"), Ally->FinalStats.AttackDamage, 100.f);
    TestEqual(TEXT("AP restored on expiration"), Ally->FinalStats.AbilityPower, 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIronwallGuardTest, "ProjectA.Unit.IronwallGuard.ReductionAndRefresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIronwallGuardTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData Data;
    Data.BaseStats.MaxHealth = 1000.f;
    Data.BaseStats.Armor = 100.f;
    Data.SkillClasses = {UIronwallGuardSkill::StaticClass()};
    ABattleUnitBase* Guard = F.Spawn(TEXT("Guard"), Data, EBattleTeam::Player, 0);
    if (!TestNotNull(TEXT("Guard spawned"), Guard)) return false;
    Guard->StartCombat();
    const auto Cast = [Guard]
    {
        Guard->GainMana(Guard->FinalStats.MaxMana);
        if (!Guard->TryCastSkill()) return false;
        const int32 Id = Guard->ActiveActionId;
        const bool bCommitted = Guard->CommitSkillEffect(Id);
        Guard->FinishAction(Id);
        return bCommitted;
    };
    const auto TakeHit = [Guard]
    {
        FUnitDamageRequest Request;
        Request.RawDamage = 200.f;
        Request.Type = EUnitDamageType::Physical;
        return Guard->ReceiveDamage(Request).HealthDamage;
    };
    TestEqual(TEXT("Armor alone reduces hit to 100"), TakeHit(), 100.f);
    if (!TestTrue(TEXT("Guard buff commits"), Cast())) return false;
    TestTrue(TEXT("40 percent reduction multiplies damage after armor"), FMath::IsNearlyEqual(TakeHit(), 60.f, 0.001f));
    Guard->Tick(3.f);
    if (!TestTrue(TEXT("Guard refreshes"), Cast())) return false;
    Guard->Tick(2.f);
    TestTrue(TEXT("Second cast refreshes without stacking"), FMath::IsNearlyEqual(TakeHit(), 60.f, 0.001f));
    Guard->Tick(2.f);
    TestEqual(TEXT("Defense expires four seconds after recast"), TakeHit(), 100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FThunderMageLightningTest, "ProjectA.Unit.ThunderMageLightning.DamageAndStun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FThunderMageLightningTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    for (const int32 Stars : {1, 2})
    {
        FFixture F;
        FTestUnitData CasterData;
        CasterData.BaseStats.AbilityPower = 200.f;
        CasterData.BaseStats.CriticalChance = 1.f;
        CasterData.SkillClasses = {UThunderMageLightningSkill::StaticClass()};
        FTestUnitData EnemyData;
        EnemyData.BaseStats.MaxHealth = 2000.f;
        EnemyData.BaseStats.MagicResistance = 100.f;
        ABattleUnitBase* Caster = F.Spawn(TEXT("Caster"), CasterData, EBattleTeam::Player, 0);
        ABattleUnitBase* Enemy = F.Spawn(TEXT("Enemy"), EnemyData, EBattleTeam::Enemy, 4);
        ABattleUnitBase* Other = F.Spawn(TEXT("Other"), EnemyData, EBattleTeam::Enemy, 5);
        if (!Caster || !Enemy || !Other) { AddError(TEXT("Could not deploy lightning test units")); return false; }
        Caster->SetStarLevel(Stars);
        const float StunTime = Stars == 1 ? 1.f : 0.5f;
        Enemy->CombatPolicy.StunDurationMultiplier = StunTime;
        for (ABattleUnitBase* Unit : {Caster, Enemy, Other}) Unit->StartCombat();
        Caster->GainMana(Caster->FinalStats.MaxMana);
        Caster->CurrentTarget = Other;
        TestFalse(TEXT("Target beyond four tiles cannot be cast on"), Caster->TryCastSkill());
        Caster->CurrentTarget = Enemy;
        if (!TestTrue(TEXT("Current enemy in range starts cast"), Caster->TryCastSkill())) return false;
        const int32 ActionId = Caster->ActiveActionId;
        TestTrue(TEXT("Lightning commits once"), Caster->CommitSkillEffect(ActionId));
        TestFalse(TEXT("Duplicate impact does not repeat damage"), Caster->CommitSkillEffect(ActionId));
        TestEqual(TEXT("AP 200 doubles raw damage and magic resistance halves it"),
            Enemy->CurrentHP, Stars == 1 ? 1700.f : 1460.f);
        TestEqual(TEXT("Other target takes no damage"), Other->CurrentHP, 2000.f);
        if (TestEqual(TEXT("Target received one stun"), Enemy->ActiveModifiers.Num(), 1))
        {
            TestTrue(TEXT("The modifier is a stun"), Enemy->IsStunned());
            TestEqual(TEXT("Common stun multiplier applies"), Enemy->ActiveModifiers[0].RemainingSeconds, StunTime);
        }
        Enemy->Tick(StunTime);
        TestFalse(TEXT("Stun expires at its given duration"), Enemy->IsStunned());

        Caster->FinishAction(ActionId);
        Caster->GainMana(Caster->FinalStats.MaxMana);
        if (!TestTrue(TEXT("Second cast begins before the target dies"), Caster->TryCastSkill())) return false;
        FUnitDamageRequest Fatal;
        Fatal.RawDamage = 10000.f;
        Fatal.Type = EUnitDamageType::TrueDamage;
        Enemy->ReceiveDamage(Fatal);
        Caster->CurrentTarget = Other;
        TestFalse(TEXT("A dead target makes single target lightning fizzle"), Caster->CommitSkillEffect(Caster->ActiveActionId));
        TestEqual(TEXT("Lightning does not transfer to the new target"), Other->CurrentHP, 2000.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpellbladeCombatTest, "ProjectA.Unit.Spellblade.ComboAndCrossSlash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSpellbladeCombatTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData Data;
    Data.BaseStats.AttackDamage = 100.f;
    Data.BaseStats.AbilityPower = 100.f;
    Data.BaseStats.CriticalChance = 1.f;
    Data.BasicAttack.bUseAnimNotify = true;
    Data.SkillClasses = {USpellbladeDualWieldSkill::StaticClass(), USpellbladeCrossSlashSkill::StaticClass()};
    ABattleUnitBase* Caster = F.Spawn(TEXT("Spellblade"), Data, EBattleTeam::Player, 0);
    FTestUnitData EnemyData;
    EnemyData.BaseStats.MaxHealth = 1000.f;
    EnemyData.BaseStats.Armor = 100.f;
    ABattleUnitBase* Enemy = F.Spawn(TEXT("Enemy"), EnemyData, EBattleTeam::Enemy, 1);
    if (!Caster || !Enemy) { AddError(TEXT("Could not deploy Spellblade test units")); return false; }
    Caster->StartCombat();
    Enemy->StartCombat();
    Caster->CurrentTarget = Enemy;
    if (!TestTrue(TEXT("Combo starts"), Caster->TryBasicAttack())) return false;
    const int32 ComboId = Caster->ActiveActionId;
    TestFalse(TEXT("Followup waits for primary attack"), Caster->CommitFollowupAttackHit(ComboId));
    TestTrue(TEXT("Primary lands"), Caster->CommitAttackHit(ComboId));
    TestEqual(TEXT("Primary uses physical mitigation and the combo crit"), Enemy->CurrentHP, 962.5f);
    TestTrue(TEXT("Followup lands"), Caster->CommitFollowupAttackHit(ComboId));
    TestFalse(TEXT("Followup Notify cannot repeat"), Caster->CommitFollowupAttackHit(ComboId));
    TestEqual(TEXT("Followup uses magic mitigation and the same crit"), Enemy->CurrentHP, 910.f);
    TestEqual(TEXT("One combo counts as one basic hit"), Caster->BasicHitCount, 1);
    TestEqual(TEXT("Combo awards mana once"), Caster->CurrentMana, 10.f);
    Caster->FinishAction(ComboId);
    Caster->GainMana(Caster->FinalStats.MaxMana);
    if (!TestTrue(TEXT("Cross slash starts"), Caster->TryCastSkill())) return false;
    TestTrue(TEXT("Cross slash commits"), Caster->CommitSkillEffect(Caster->ActiveActionId));
    TestEqual(TEXT("Cross slash deals both types without crit"), Enemy->CurrentHP, 700.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSniperPrecisionShotTest, "ProjectA.Unit.SniperPrecisionShot.ReleaseCountAndCrit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSniperPrecisionShotTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData Data;
    Data.BaseStats.AttackDamage = 100.f;
    Data.BaseStats.AttackRange = 5;
    Data.BaseStats.CriticalChance = 0.f;
    Data.BaseStats.CriticalMultiplier = 2.f;
    Data.BasicAttack.Delivery = EUnitAttackDelivery::Projectile;
    Data.BasicAttack.ProjectileSpeed = 5000.f;
    Data.SkillClasses = {USniperPrecisionShotSkill::StaticClass()};
    ABattleUnitBase* Caster = F.Spawn(TEXT("Sniper"), Data, EBattleTeam::Player, 0);
    FTestUnitData TargetData;
    TargetData.BaseStats.MaxHealth = 1000.f;
    ABattleUnitBase* First = F.Spawn(TEXT("First"), TargetData, EBattleTeam::Enemy, 1);
    ABattleUnitBase* Second = F.Spawn(TEXT("Second"), TargetData, EBattleTeam::Enemy, 2);
    if (!Caster || !First || !Second) { AddError(TEXT("Could not deploy sniper test units")); return false; }
    for (ABattleUnitBase* Unit : {Caster, First, Second}) Unit->StartCombat();
    const auto Fire = [Caster](ABattleUnitBase* Target, bool bRelease)
    {
        Caster->CurrentTarget = Target;
        if (!Caster->TryBasicAttack()) return false;
        const int32 ActionId = Caster->ActiveActionId;
        const bool bCommitted = !bRelease || Caster->CommitAttackHit(ActionId);
        Caster->FinishAction(ActionId);
        Caster->CurrentTarget = nullptr;
        Caster->Tick(1.f);
        return bCommitted;
    };
    TestFalse(TEXT("Passive-only sniper does not use mana"), Caster->UsesMana());
    if (!TestTrue(TEXT("First projectile releases"), Fire(First, true))) return false;
    TestEqual(TEXT("First shot is normal"), First->CurrentHP, 900.f);
    if (!TestTrue(TEXT("A windup is cancelled before release"), Fire(First, false))) return false;
    if (!TestTrue(TEXT("Second projectile releases at another target"), Fire(Second, true))) return false;
    TestEqual(TEXT("Cancelled windup does not increment or switch progress"), Second->CurrentHP, 900.f);
    if (!TestTrue(TEXT("Third projectile releases"), Fire(Second, true))) return false;
    TestEqual(TEXT("Third shot is guaranteed critical at the configured multiplier"), Second->CurrentHP, 500.f);
    if (!TestTrue(TEXT("Cycle restarts after the precision shot"), Fire(First, true))) return false;
    TestEqual(TEXT("Fourth shot is normal"), First->CurrentHP, 800.f);
    Caster->StopCombat();
    TestEqual(TEXT("Round cleanup resets cycle"), Caster->GetEquippedSkills()[0]->Counter, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElementalistIceLanceTest, "ProjectA.Unit.Elementalist.IceLance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElementalistIceLanceTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData Data;
    Data.BaseStats.AbilityPower = 100.f;
    Data.SkillClasses = {UElementalistSkill::StaticClass()};
    ABattleUnitBase* Caster = F.Spawn(TEXT("Elementalist"), Data, EBattleTeam::Player, 0);
    Data.SkillClasses.Reset();
    Data.BaseStats.MaxHealth = 1000.f;
    ABattleUnitBase* Enemy = F.Spawn(TEXT("Enemy"), Data, EBattleTeam::Enemy, 1);
    if (!Caster || !Enemy) { AddError(TEXT("Could not deploy ice lance test units")); return false; }
    Caster->SetStarLevel(2);
    auto* Skill = Cast<UElementalistSkill>(Caster->GetEquippedSkills()[0]);
    if (!TestNotNull(TEXT("Elementalist skill"), Skill)) return false;
    Skill->FrostTwoStarAPRatio = 4.f;
    Caster->StartCombat();
    Enemy->StartCombat();
    Caster->CurrentTarget = Enemy;
    Caster->GainMana(45.f);
    if (!TestTrue(TEXT("Frost casts at 45 mana"), Caster->TryCastSkill())) return false;
    TestTrue(TEXT("Ice Lance releases"), Caster->CommitSkillEffect(Caster->ActiveActionId));
    TestEqual(TEXT("Damage waits for projectile arrival"), Enemy->CurrentHP, 1000.f);
    Caster->Tick(0.5f);
    TestEqual(TEXT("Two-star Frost ratio can be tuned separately"), Enemy->CurrentHP, 600.f);
    TestEqual(TEXT("Ice Lance slows attack speed"), Enemy->FinalStats.AttackSpeed, 0.75f);
    Enemy->Tick(3.f);
    TestEqual(TEXT("Slow expires after three seconds"), Enemy->FinalStats.AttackSpeed, 1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitCombatRulesTest, "ProjectA.Unit.CombatRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitCombatRulesTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F;
    FTestUnitData Data = FFixture::UnitData();
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
    FTestUnitData Data = FFixture::UnitData(); Data.BasicAttack.Delivery = EUnitAttackDelivery::Projectile;
    FUnitSkillDefinition Growth; Growth.SkillId = TEXT("EveryThirdHit"); Growth.Trigger = EUnitTrigger::BasicAttackHit;
    Growth.TriggerEvery = 3; Growth.Target = EUnitSkillTarget::Self;
    FUnitSkillEffect Effect; Effect.Kind = EUnitEffectKind::PermanentGrowth; Effect.BaseValue = 1.f; Effect.GrowthLimitPerCombat = 3.f;
    Growth.Effects.Add(Effect); Data.TestSkills.Add(Growth);
    ABattleUnitBase* A = F.Spawn(TEXT("Archer"), Data, EBattleTeam::Player, 0);
    FTestUnitData Enemy = FFixture::UnitData(); Enemy.TestSkills.Empty();
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
    FTestUnitData Data = FFixture::UnitData(); Data.BaseStats.Armor = 100.f;
    FUnitSkillDefinition Bonus; Bonus.SkillId = TEXT("BonusDamage"); Bonus.Trigger = EUnitTrigger::DamageDealt;
    FUnitSkillEffect Damage; Damage.Kind = EUnitEffectKind::Damage; Damage.BaseValue = 20.f; Damage.DamageType = EUnitDamageType::TrueDamage;
    Bonus.Effects.Add(Damage); Data.TestSkills.Add(Bonus);
    ABattleUnitBase* A = F.Spawn(TEXT("A"), Data, EBattleTeam::Player, 0);
    Data.TestSkills.Empty();
    ABattleUnitBase* B = F.Spawn(TEXT("Boss"), Data, EBattleTeam::Enemy, 1);
    if (!A || !B) return false;
    B->CombatPolicy.StunDurationMultiplier = 0.5f;
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
    FTestUnitData RapidFire = FFixture::UnitData();
    FUnitSkillEffect& RapidEffect = RapidFire.TestSkills[0].Effects[0];
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
    FTestUnitData TargetData = FFixture::UnitData(); TargetData.TestSkills.Empty();
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitActionFacingTest, "ProjectA.Unit.ActionFacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitActionFacingTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    for (EUnitAttackDelivery Delivery : {EUnitAttackDelivery::Melee, EUnitAttackDelivery::Projectile})
    {
        FFixture F; FTestUnitData Data = FFixture::UnitData(); Data.TestSkills.Empty();
        Data.BasicAttack.Delivery = Delivery;
        Data.Visuals.VisualTransform.SetRotation(FRotator(0.f, -90.f, 0.f).Quaternion());
        ABattleUnitBase* A = F.Spawn(TEXT("Attacker"), Data, EBattleTeam::Player, 0);
        ABattleUnitBase* B = F.Spawn(TEXT("First"), Data, EBattleTeam::Enemy, 1);
        ABattleUnitBase* C = F.Spawn(TEXT("Second"), Data, EBattleTeam::Enemy, 2);
        if (!A || !B || !C) return false;
        B->SetActorLocation(FVector(0, 500, 200)); C->SetActorLocation(FVector(-500, -500, 100));
        A->StartCombat(); B->StartCombat(); C->StartCombat();
        A->SetActorRotation(FRotator(0, 180, 0)); A->CurrentTarget = B;
        TestTrue(TEXT("Attack starts without prior movement"), A->TryBasicAttack());
        TestTrue(TEXT("Attack faces target horizontally"), A->GetActorForwardVector().Equals((B->GetActorLocation() - A->GetActorLocation()).GetSafeNormal2D(), 0.001f));
        TestTrue(TEXT("DT model forward correction stays applied"), A->VisualRoot->GetRelativeRotation().Equals(FRotator(0, -90, 0)));
        TestTrue(TEXT("Model +Y forward matches target after correction"), A->VisualRoot->GetRightVector().Equals(A->GetActorForwardVector(), 0.001f));
        A->FinishAction(A->ActiveActionId); A->CurrentTarget = nullptr; A->Tick(2.f);
        A->CurrentTarget = C;
        TestTrue(TEXT("Next attack can start after retarget"), A->TryBasicAttack());
        TestTrue(TEXT("Next attack turns toward new target"), A->GetActorForwardVector().Equals((C->GetActorLocation() - A->GetActorLocation()).GetSafeNormal2D(), 0.001f));
        const FRotator Facing = A->GetActorRotation(); A->CurrentTarget = B;
        TestFalse(TEXT("Active attack rejects another start"), A->TryBasicAttack());
        TestTrue(TEXT("Rejected start does not turn active attack"), A->GetActorRotation().Equals(Facing));
    }
    for (EUnitSkillTarget Target : {EUnitSkillTarget::CurrentEnemy, EUnitSkillTarget::ClosestEnemy, EUnitSkillTarget::Self, EUnitSkillTarget::LowestHealthAlly})
    {
        FFixture F; FTestUnitData Data = FFixture::UnitData(); Data.TestSkills[0].Target = Target;
        ABattleUnitBase* A = F.Spawn(TEXT("Caster"), Data, EBattleTeam::Player, 0);
        ABattleUnitBase* B = F.Spawn(TEXT("Enemy"), Data, EBattleTeam::Enemy, 1);
        ABattleUnitBase* Ally = F.Spawn(TEXT("Ally"), Data, EBattleTeam::Player, 2);
        if (!A || !B || !Ally) return false;
        B->SetActorLocation(FVector(200, 500, 300));
        A->SetCombatRoster({A, B, Ally}); A->StartCombat(); B->StartCombat(); Ally->StartCombat();
        FUnitDamageRequest Damage; Damage.Attacker = B; Damage.RawDamage = 100.f;
        Ally->ReceiveDamage(Damage);
        A->CurrentTarget = B; A->GainMana(100.f); A->SetActorRotation(FRotator(0, -35, 0));
        TestTrue(TEXT("Skill starts"), A->TryCastSkill());
        if (Target == EUnitSkillTarget::CurrentEnemy || Target == EUnitSkillTarget::ClosestEnemy)
            TestTrue(TEXT("Enemy skill faces chosen target without pitching"), A->GetActorForwardVector().Equals((B->GetActorLocation() - A->GetActorLocation()).GetSafeNormal2D(), 0.001f));
        else TestTrue(TEXT("Self and ally skills preserve facing"), A->GetActorRotation().Equals(FRotator(0, -35, 0)));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitProjectileViewTest, "ProjectA.Unit.ProjectileViewIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitProjectileViewTest::RunTest(const FString& Parameters)
{
    using namespace UnitCombatTests;
    FFixture F; FTestUnitData Data = FFixture::UnitData();
    Data.BasicAttack.Delivery = EUnitAttackDelivery::Projectile;
    UStaticMesh* BasicMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UStaticMesh* SkillMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("Basic visual mesh"), BasicMesh) || !TestNotNull(TEXT("Skill visual mesh"), SkillMesh)) return false;
    UMaterialInterface* BasicMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    UMaterialInstanceDynamic* SkillMaterial = UMaterialInstanceDynamic::Create(BasicMaterial, F.World);
    Data.BasicAttack.ProjectileVisual.Mesh = BasicMesh;
    Data.BasicAttack.ProjectileVisual.Material = BasicMaterial;
    Data.BasicAttack.ProjectileVisual.Scale = FVector(0.15f);
    Data.BasicAttack.ProjectileVisual.Rotation = FRotator(0.f, -90.f, 0.f);
    Data.BasicAttack.ProjectileOrigin.Component = EUnitProjectileOriginComponent::OffHand;
    Data.BasicAttack.ProjectileOrigin.SocketName = TEXT("Muzzle");
    Data.BasicAttack.ProjectileOrigin.LocalOffset = FVector(10.f, 0.f, 0.f);
    FUnitSkillDefinition& Skill = Data.TestSkills[0];
    Skill.SkillId = TEXT("Fireball"); Skill.Target = EUnitSkillTarget::CurrentEnemy;
    Skill.bProjectile = true; Skill.bUseAnimNotify = true;
    Skill.ProjectileVisual.Mesh = SkillMesh;
    Skill.ProjectileVisual.Material = SkillMaterial;
    Skill.ProjectileVisual.Scale = FVector(0.3f, 0.4f, 0.5f);
    Skill.ProjectileVisual.Rotation = FRotator(10.f, 20.f, 30.f);
    Skill.Effects[0].Kind = EUnitEffectKind::Damage;
    ABattleUnitBase* A = F.Spawn(TEXT("Mage"), Data, EBattleTeam::Player, 0);
    ABattleUnitBase* B = F.Spawn(TEXT("Enemy"), FFixture::UnitData(), EBattleTeam::Enemy, 1);
    if (!A || !B) return false;
    UStaticMesh* WeaponMesh = DuplicateObject(BasicMesh, F.World);
    UStaticMeshSocket* Muzzle = NewObject<UStaticMeshSocket>(WeaponMesh);
    Muzzle->SocketName = TEXT("Muzzle");
    Muzzle->RelativeLocation = FVector(24.f, 0.f, 35.f);
    Muzzle->RelativeRotation = FRotator(0.f, 90.f, 0.f);
    Muzzle->RelativeScale = FVector::OneVector;
    WeaponMesh->AddSocket(Muzzle);
    A->OffHand->SetStaticMesh(WeaponMesh);
    A->OffHand->SetRelativeLocation(FVector(20.f, -15.f, 30.f));
    A->OffHand->SetVisibility(true);
    A->MainHand->SetStaticMesh(WeaponMesh);
    A->MainHand->SetRelativeLocation(FVector(45.f, 15.f, 20.f));
    A->MainHand->SetVisibility(true);
    A->StartCombat(); B->StartCombat(); A->CurrentTarget = B;
    TestTrue(TEXT("Basic attack starts"), A->TryBasicAttack());
    TestTrue(TEXT("Basic projectile launches"), A->CommitAttackHit(A->ActiveActionId));
    A->FinishAction(A->ActiveActionId);
    A->GainMana(100.f);
    TestTrue(TEXT("Fireball cast starts while basic projectile is airborne"), A->TryCastSkill());
    TestTrue(TEXT("Skill projectile launches"), A->CommitSkillEffect(A->ActiveActionId));
    A->FinishAction(A->ActiveActionId);
    if (!TestEqual(TEXT("Basic and skill shots coexist"), A->Projectiles.Num(), 2)) return false;
    const FUnitProjectileView Basic = A->MakeProjectileView(A->Projectiles[0]);
    const FUnitProjectileView Fireball = A->MakeProjectileView(A->Projectiles[1]);
    const FVector BodyOrigin = A->GetActorLocation() + FVector(0, 0, A->GetHealthBarHeight() * 0.5f);
    const FVector MeshLocalOrigin = FVector(24.f, 10.f, 35.f); // 소켓의 +X 보정은 회전 후 메시 +Y다.
    TestTrue(TEXT("Basic shot starts at the off-hand socket plus its rotated local offset"),
        Basic.Position.Equals(A->OffHand->GetComponentTransform().TransformPosition(MeshLocalOrigin)));
    TestTrue(TEXT("Skill projectile keeps the original body launch point"), Fireball.Position.Equals(BodyOrigin));
    FUnitProjectileOrigin& Origin = A->Definition.BasicAttack.ProjectileOrigin;
    Origin.Component = EUnitProjectileOriginComponent::MainHand;
    TestTrue(TEXT("Main hand origin selects the same named socket on a different component"),
        A->GetBasicProjectileLaunchLocation(BodyOrigin).Equals(A->MainHand->GetComponentTransform().TransformPosition(MeshLocalOrigin)));
    Origin.SocketName = TEXT("MissingMuzzle");
    TestTrue(TEXT("A missing socket falls back to the body"), A->GetBasicProjectileLaunchLocation(BodyOrigin).Equals(BodyOrigin));
    Origin = FUnitProjectileOrigin{};
    TestTrue(TEXT("Unset origin preserves the legacy position"), A->GetBasicProjectileLaunchLocation(BodyOrigin).Equals(BodyOrigin));
    TestTrue(TEXT("Basic shot flag and empty skill ID"), Basic.bBasicAttack && Basic.SkillId.IsNone());
    TestTrue(TEXT("Skill shot flag and actual DT skill ID"), !Fireball.bBasicAttack && Fireball.SkillId == TEXT("Fireball"));
    TestTrue(TEXT("Basic shot uses basic attack visual data"), Basic.Visual.Mesh == BasicMesh && Basic.Visual.Material == BasicMaterial && Basic.Visual.Scale.Equals(FVector(0.15f)));
    TestTrue(TEXT("Skill shot uses its own visual data"), Fireball.Visual.Mesh == SkillMesh && Fireball.Visual.Material == SkillMaterial && Fireball.Visual.Scale.Equals(FVector(0.3f, 0.4f, 0.5f)));
    TestTrue(TEXT("Launch rotation faces target center"), Basic.Rotation.Vector().Equals((B->GetActorLocation() + FVector(0,0,B->GetHealthBarHeight()*0.5f) - Basic.Position).GetSafeNormal(), 0.001f));
    TestTrue(TEXT("Mesh correction is separate from flight rotation"), Basic.Visual.Rotation.Equals(FRotator(0,-90,0)) && Fireball.Visual.Rotation.Equals(FRotator(10,20,30)));
    TestTrue(TEXT("Shot IDs stay distinct"), Basic.ProjectileId != Fireball.ProjectileId);
    TestTrue(TEXT("Launch target and position preserved"), Basic.Target == B && Fireball.Target == B
        && Basic.Position.Equals(A->Projectiles[0].Position) && Fireball.Position.Equals(A->Projectiles[1].Position));
    // 다음 행동/데이터가 바뀌어도 이미 발사된 탄의 외형 구분은 바뀌면 안 된다.
    A->CurrentTarget = nullptr; A->GetEquippedSkills()[0]->Settings.SkillId = TEXT("NextSkill");
    A->Definition.BasicAttack.ProjectileVisual = FUnitProjectileVisualDefinition();
    A->GetEquippedSkills()[0]->Settings.ProjectileVisual = FUnitProjectileVisualDefinition();
    B->SetActorLocation(FVector(500.f, 500.f, 200.f));
    A->Tick(0.1f);
    if (!TestEqual(TEXT("Both shots remain in flight"), A->Projectiles.Num(), 2)) return false;
    const FUnitProjectileView MovedBasic = A->MakeProjectileView(A->Projectiles[0]);
    const FUnitProjectileView MovedSkill = A->MakeProjectileView(A->Projectiles[1]);
    TestTrue(TEXT("Basic identity stable in flight"), MovedBasic.ProjectileId == Basic.ProjectileId && MovedBasic.bBasicAttack && MovedBasic.SkillId.IsNone());
    TestTrue(TEXT("Skill identity stable in flight"), MovedSkill.ProjectileId == Fireball.ProjectileId && !MovedSkill.bBasicAttack && MovedSkill.SkillId == Fireball.SkillId);
    TestTrue(TEXT("Original target retained"), MovedBasic.Target == B && MovedSkill.Target == B);
    TestTrue(TEXT("Launched basic visual is an independent snapshot"), MovedBasic.Visual.Mesh == BasicMesh && MovedBasic.Visual.Material == BasicMaterial && MovedBasic.Visual.Scale.Equals(Basic.Visual.Scale));
    TestTrue(TEXT("Launched skill visual is an independent snapshot"), MovedSkill.Visual.Mesh == SkillMesh && MovedSkill.Visual.Material == SkillMaterial && MovedSkill.Visual.Scale.Equals(Fireball.Visual.Scale));
    TestTrue(TEXT("Basic mesh correction remains fixed in flight"), MovedBasic.Visual.Rotation.Equals(Basic.Visual.Rotation));
    TestTrue(TEXT("Skill mesh correction remains fixed in flight"), MovedSkill.Visual.Rotation.Equals(Fireball.Visual.Rotation));
    TestTrue(TEXT("Basic rotation follows actual travel toward moving target"), MovedBasic.Rotation.Vector().Equals((MovedBasic.Position - Basic.Position).GetSafeNormal(), 0.001f));
    TestTrue(TEXT("Skill rotation follows actual travel toward moving target"), MovedSkill.Rotation.Vector().Equals((MovedSkill.Position - Fireball.Position).GetSafeNormal(), 0.001f));
    TestFalse(TEXT("Flight rotation changes with trajectory"), MovedBasic.Rotation.Equals(Basic.Rotation));
    TestFalse(TEXT("Basic position updated"), MovedBasic.Position.Equals(Basic.Position));
    TestFalse(TEXT("Skill position updated"), MovedSkill.Position.Equals(Fireball.Position));
    TestEqual(TEXT("No premature damage during flight"), B->CurrentHP, 1000.f);
    A->StopCombat();
    TestTrue(TEXT("Combat stop clears both shots"), A->Projectiles.IsEmpty());
    B->StopCombat(); A->ResetForRound(); B->ResetForRound();
    A->StartCombat(); B->StartCombat(); B->SetActorLocation(A->GetActorLocation()); A->CurrentTarget = B;
    A->TryBasicAttack(); A->CommitAttackHit(A->ActiveActionId);
    if (!TestEqual(TEXT("Zero-distance shot can launch"), A->Projectiles.Num(), 1)) return false;
    TestTrue(TEXT("Zero-distance launch rotation remains finite"), !A->MakeProjectileView(A->Projectiles[0]).Rotation.ContainsNaN());
    return true;
}
#endif
