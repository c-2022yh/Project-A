#include "BattleUnitBase.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"

ABattleUnitBase::ABattleUnitBase()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ABattleUnitBase::BeginPlay()
{
    Super::BeginPlay();

    CurrentHP = MaxHP;
    CurrentMP = 0.0f;
    bIsDead = false;
}

void ABattleUnitBase::ReceiveDamage(float DamageAmount)
{
    if (bIsDead)
    {
        return;
    }

    CurrentHP -= DamageAmount;

    if (CurrentHP <= 0.0f)
    {
        CurrentHP = 0.0f;
        Die();
    }
}

void ABattleUnitBase::Heal(float HealAmount)
{
    if (bIsDead)
    {
        return;
    }

    CurrentHP = FMath::Clamp(CurrentHP + HealAmount, 0.0f, MaxHP);
}

void ABattleUnitBase::AddMana(float ManaAmount)
{
    if (bIsDead)
    {
        return;
    }

    CurrentMP = FMath::Clamp(CurrentMP + ManaAmount, 0.0f, MaxMP);
}

void ABattleUnitBase::Die()
{
    if (bIsDead)
    {
        return;
    }

    bIsDead = true;

    GetCharacterMovement()->DisableMovement();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool ABattleUnitBase::IsAlive() const
{
    return !bIsDead && CurrentHP > 0.0f;
}