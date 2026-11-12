#include "BattleUnitBase.h"
#include "BoardTile.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace
{
    // 유효하지 않은 값이 전파되지 않게 능력치 계산 정책을 한곳에 둔다.
    float Bounded(float Value, float Min, float Max = MAX_flt)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Min;
    }
    float StarMultiplier(const TArray<float>& Values, int32 Star)
    {
        return Values.IsValidIndex(Star - 1) ? Bounded(Values[Star - 1], 0.f) : 1.f;
    }
}

ABattleUnitBase::ABattleUnitBase()
{
    PrimaryActorTick.bCanEverTick = false;
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
    // 레벨에 놓은 유닛은 데이터 행을 바꾸면 즉시 미리 볼 수 있다.
    if (!GetWorld()->IsGameWorld())
    {
        if (UnitData.DataTable && !UnitData.RowName.IsNone()) RefreshUnitFromData();
        else
        {
            ClearVisuals();
            bInitialized = false;
            UnitID = NAME_None;
            Definition = FUnitDefinition();
            FinalStats = FUnitStats();
            CurrentHP = 0.f;
        }
    }
}

void ABattleUnitBase::BeginPlay()
{
    Super::BeginPlay();
    // Deferred Spawn에서 이미 초기화했다면 덮어쓰지 않는다.
    if (!bInitialized && UnitData.DataTable) RefreshUnitFromData();
}

bool ABattleUnitBase::InitializeUnit(FDataTableRowHandle Data, EBattleTeam Team, int32 Stars, FString& Error)
{
    Error.Reset();
    if (bInCombat || IsValid(CurrentTile)) Error = TEXT("Remove the unit from its tile before changing its definition.");
    else if (Placement == EUnitPlacement::Waiting && Team != EBattleTeam::Player) Error = TEXT("Waiting placement is only for player units.");
    else if (Stars < 1 || Stars > 3) Error = TEXT("Star level must be 1, 2 or 3.");
    else if (Team != EBattleTeam::Player && Team != EBattleTeam::Enemy) Error = TEXT("Invalid team.");
    else if (!Data.DataTable || Data.RowName.IsNone()
        || Data.DataTable->GetRowStruct() != FUnitDefinition::StaticStruct())
        Error = TEXT("Select a UnitDefinition data table and a valid row name.");
    const FUnitDefinition* Row = Error.IsEmpty()
        ? Data.DataTable->FindRow<FUnitDefinition>(Data.RowName, TEXT("InitializeUnit"), false) : nullptr;
    if (Error.IsEmpty() && !Row) Error = TEXT("The selected unit row does not exist.");
    if (Row && (Row->Cost < 1 || Row->Cost > 5)) Error = TEXT("Unit cost must be between 1 and 5.");
    if (!Error.IsEmpty())
    {
        LastValidationMessage = Error;
        return false;
    }
    // 원본 행은 읽기만 하고 인스턴스 사본을 만든다. 전투 상태를 DT에 쓰지 않는다.
    UnitData = Data;
    UnitID = Data.RowName;
    Definition = *Row;
    TeamType = Team;
    StarLevel = Stars;
    bInitialized = true;
    bIsDead = false;
    ActionState = EUnitActionState::Idle;
    FinalStats = Definition.BaseStats;
    CurrentHP = FMath::Max(1.f, FinalStats.MaxHealth);
    RecalculateStats();
    SetActorHiddenInGame(false);
    ApplyVisuals();
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
    // 항상 원본에서 다시 계산한다. 반복 호출해도 배율이 누적되지 않는다.
    // 아이템·일시 효과는 이후 단계에서 이 계산 경로에 활성 보정값으로 합산한다.
    const float OldMax = FinalStats.MaxHealth;
    FinalStats = Definition.BaseStats;
    FinalStats.MaxHealth = Bounded(FinalStats.MaxHealth * StarMultiplier(Definition.StarHealthMultipliers, StarLevel), 1.f);
    FinalStats.AttackDamage = Bounded(FinalStats.AttackDamage * StarMultiplier(Definition.StarAttackMultipliers, StarLevel), 0.f);
    FinalStats.AbilityPower = Bounded(FinalStats.AbilityPower, 0.f);
    FinalStats.Armor = Bounded(FinalStats.Armor, -MAX_flt);
    FinalStats.MagicResistance = Bounded(FinalStats.MagicResistance, -MAX_flt);
    FinalStats.AttackSpeed = Bounded(FinalStats.AttackSpeed, 0.01f);
    FinalStats.AttackRange = FMath::Max(0, FinalStats.AttackRange);
    FinalStats.MovementSpeed = Bounded(FinalStats.MovementSpeed, 0.f);
    FinalStats.CriticalChance = Bounded(FinalStats.CriticalChance, 0.f, 1.f);
    FinalStats.CriticalMultiplier = Bounded(FinalStats.CriticalMultiplier, 1.f);
    // 초기화 외의 재계산은 체력 비율을 유지한다. 사망한 유닛을 살리지는 않는다.
    CurrentHP = bIsDead ? 0.f : FinalStats.MaxHealth * FMath::Clamp(CurrentHP / FMath::Max(1.f, OldMax), 0.f, 1.f);
    OnStatsChanged.Broadcast(this);
    OnHealthChanged.Broadcast(this);
}

bool ABattleUnitBase::SetStarLevel(int32 Stars)
{
    if (!bInitialized || bInCombat || Stars < 1 || Stars > 3) return false;
    StarLevel = Stars;
    RecalculateStats();
    return true;
}

void ABattleUnitBase::SetTeamType(EBattleTeam Team)
{
    if (bInCombat || IsValid(CurrentTile)) return;
    if (Team != EBattleTeam::Player && Team != EBattleTeam::Enemy) return;
    // 대기석은 아군 전용이다. 대기 상태를 유지한 채 적으로 변경하지 않는다.
    if (Placement == EUnitPlacement::Waiting && Team != EBattleTeam::Player) return;
    TeamType = Team;
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

bool ABattleUnitBase::PlaceOnTile(ABoardTile* Tile)
{
    if (!IsAlive() || bInCombat || !IsValid(Tile)) return false;
    const EBoardTileTeam Required = TeamType == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
    if (Tile->TeamType != Required || (Tile->OccupiedUnit != this && !Tile->CanPlaceUnit())) return false;
    Tile->SetOccupiedUnit(this);
    if (Tile->OccupiedUnit != this) return false;
    SetActorLocation(Tile->GetSurfaceLocation() + FVector(0, 0, 1.f));
    Placement = EUnitPlacement::Board;
    bIsDeployed = true;
    PreparationTransform = GetActorTransform();
    return true;
}

bool ABattleUnitBase::SetWaitingPlacement(FVector Location)
{
    if (!IsAlive() || bInCombat || TeamType != EBattleTeam::Player || Location.ContainsNaN()) return false;
    if (IsValid(CurrentTile)) CurrentTile->ClearTile();
    SetActorLocation(Location);
    Placement = EUnitPlacement::Waiting;
    bIsDeployed = false;
    PreparationTransform = GetActorTransform();
    return true;
}

void ABattleUnitBase::StartCombat()
{
    // 아직 공격이나 이동을 구현한 상태가 아니다. 보드 점유도 유지한다.
    if (IsAlive() && Placement == EUnitPlacement::Board) bInCombat = true;
}
void ABattleUnitBase::StopCombat() { bInCombat = false; }
float ABattleUnitBase::GetSelectionRadius() const { return SelectionCollision->GetScaledCapsuleRadius(); }
float ABattleUnitBase::GetHealthBarHeight() const
{
    return SelectionCollision->GetRelativeLocation().Z + SelectionCollision->GetScaledCapsuleHalfHeight() + 45.f;
}
void ABattleUnitBase::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    Super::EndPlay(Reason);
}
