#include "BattleUnitBase.h"
#include "AutoBattleGameMode.h"
#include "BoardTile.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ABattleUnitBase::ABattleUnitBase()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(110.0f, 200.0f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    bUseControllerRotationYaw = false;
    AutoPossessAI = EAutoPossessAI::Disabled;
    PlaceholderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderMesh"));
    PlaceholderMesh->SetupAttachment(GetCapsuleComponent());
    PlaceholderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PlaceholderMesh->SetRelativeScale3D(FVector(2.2f, 2.2f, 4.0f));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) PlaceholderMesh->SetStaticMesh(Cube.Object);
}

void ABattleUnitBase::BeginPlay()
{
    Super::BeginPlay();
    MaxHP = FMath::Max(1.0f, MaxHP);
    CurrentHP = MaxHP;
    CurrentMP = 0.0f;
    bIsDead = false;
    PlaceholderMesh->SetVisibility(bUsePlaceholderMesh);
    StopCombat();
}

bool ABattleUnitBase::PlaceOnTile(ABoardTile* Tile)
{
    if (!IsAlive() || bInCombat || !IsValid(Tile)) return false;
    const EBoardTileTeam Required = TeamType == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
    if (Tile->TeamType != Required || (Tile->OccupiedUnit != this && !Tile->CanPlaceUnit())) return false;
    if (CurrentTile == Tile) return true;
    Tile->SetOccupiedUnit(this);
    if (Tile->OccupiedUnit != this) return false;
    SetActorLocation(Tile->GetSurfaceLocation() + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f));
    bIsDeployed = true;
    return true;
}

void ABattleUnitBase::StartCombat()
{
    if (!IsAlive() || !bIsDeployed) return;
    if (IsValid(CurrentTile)) CurrentTile->ClearTile();
    bInCombat = true;
    AttackCooldown = 0.0f;
    TargetRefreshTime = 0.0f;
    GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.0f, MovementSpeed);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

void ABattleUnitBase::StopCombat()
{
    bInCombat = false;
    CurrentTarget = nullptr;
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
}

void ABattleUnitBase::FindTarget()
{
    CurrentTarget = nullptr;
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode) return;
    double BestDistance = TNumericLimits<double>::Max();
    for (ABattleUnitBase* Unit : Mode->BattleUnits)
    {
        if (!IsValid(Unit) || Unit == this || Unit->TeamType == TeamType || !Unit->IsAlive() || !Unit->bInCombat) continue;
        const double Distance = FVector::DistSquared2D(GetActorLocation(), Unit->GetActorLocation());
        if (Distance < BestDistance) { BestDistance = Distance; CurrentTarget = Unit; }
    }
}

void ABattleUnitBase::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bInCombat || !IsAlive()) return;
    AttackCooldown = FMath::Max(0.0f, AttackCooldown - DeltaSeconds);
    TargetRefreshTime -= DeltaSeconds;
    if (!IsValid(CurrentTarget) || !CurrentTarget->IsAlive() || !CurrentTarget->bInCombat || TargetRefreshTime <= 0)
    {
        FindTarget();
        TargetRefreshTime = 0.5f;
    }
    if (!IsValid(CurrentTarget)) return;
    FVector Direction = CurrentTarget->GetActorLocation() - GetActorLocation();
    Direction.Z = 0;
    const float ContactDistance = GetCapsuleComponent()->GetScaledCapsuleRadius()
        + CurrentTarget->GetCapsuleComponent()->GetScaledCapsuleRadius();
    if (Direction.SizeSquared() <= FMath::Square(ContactDistance + FMath::Max(0.0f, AttackRange)))
    {
        GetCharacterMovement()->StopMovementImmediately();
        if (!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
        if (AttackCooldown <= 0 && AttackSpeed > 0 && AttackDamage > 0)
        {
            AttackCooldown = 1.0f / FMath::Max(0.01f, AttackSpeed);
            ABattleUnitBase* Target = CurrentTarget;
            OnAttack.Broadcast(this, Target);
            if (bInCombat && IsAlive() && IsValid(Target) && Target->IsAlive()) Target->ReceiveDamage(AttackDamage);
        }
    }
    else
    {
        // 평평한 전장에서 목표를 향해 이동한다. 장애물 우회는 추후 별도의 경로 탐색 구현이 필요하다.
        FVector Steering = Direction.GetSafeNormal();
        const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
        if (Mode)
        {
            for (ABattleUnitBase* Other : Mode->BattleUnits)
            {
                if (!IsValid(Other) || Other == this || !Other->IsAlive() || !Other->bInCombat) continue;
                FVector Away = GetActorLocation() - Other->GetActorLocation();
                Away.Z = 0;
                const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius()
                    + Other->GetCapsuleComponent()->GetScaledCapsuleRadius() + 80.0f;
                const float Distance = Away.Size();
                if (Distance > 1 && Distance < Radius) Steering += Away / Distance * (1.0f - Distance / Radius);
            }
        }
        AddMovementInput(Steering.GetSafeNormal(), 1.0f, true);
    }
}

void ABattleUnitBase::ReceiveDamage(float DamageAmount)
{
    if (!IsAlive() || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0) return;
    const float Applied = DamageAmount * 100.0f / (100.0f + FMath::Max(0.0f, Armor));
    CurrentHP = FMath::Clamp(CurrentHP - Applied, 0.0f, MaxHP);
    OnHealthChanged.Broadcast(this);
    if (CurrentHP <= 0) Die();
}
void ABattleUnitBase::Heal(float HealAmount)
{
    if (!IsAlive() || !FMath::IsFinite(HealAmount) || HealAmount <= 0) return;
    CurrentHP = FMath::Clamp(CurrentHP + HealAmount, 0.0f, MaxHP);
    OnHealthChanged.Broadcast(this);
}
void ABattleUnitBase::AddMana(float ManaAmount)
{
    if (IsAlive() && FMath::IsFinite(ManaAmount) && ManaAmount > 0)
        CurrentMP = FMath::Clamp(CurrentMP + ManaAmount, 0.0f, FMath::Max(0.0f, MaxMP));
}
void ABattleUnitBase::Die()
{
    if (bIsDead) return;
    bIsDead = true;
    CurrentHP = 0;
    StopCombat();
    if (IsValid(CurrentTile)) CurrentTile->ClearTile();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetActorHiddenInGame(true);
    OnUnitDied.Broadcast(this);
}
bool ABattleUnitBase::IsAlive() const { return !bIsDead && CurrentHP > 0; }
void ABattleUnitBase::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(CurrentTile) && CurrentTile->OccupiedUnit == this) CurrentTile->ClearTile();
    Super::EndPlay(Reason);
}
