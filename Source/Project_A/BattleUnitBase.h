#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BattleUnitBase.generated.h"

UENUM(BlueprintType)
enum class EBattleTeam : uint8
{
    Player UMETA(DisplayName = "Player"),
    Enemy  UMETA(DisplayName = "Enemy")
};

UCLASS()
class PROJECT_A_API ABattleUnitBase : public ACharacter
{
    GENERATED_BODY()

public:
    ABattleUnitBase();

protected:
    virtual void BeginPlay() override;

public:

    // 팀 구분
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unit")
    EBattleTeam TeamType = EBattleTeam::Player;

    // 최대 체력
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float MaxHP = 100.0f;

    // 현재 체력
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    float CurrentHP = 100.0f;

    // 최대 마나
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float MaxMP = 100.0f;

    // 현재 마나
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    float CurrentMP = 0.0f;

    // 공격력
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float AttackDamage = 10.0f;

    // 방어력
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float Armor = 0.0f;

    // 공격 속도 (초당 공격 횟수)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float AttackSpeed = 1.0f;

    // 공격 사거리
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float AttackRange = 150.0f;

    // 현재 공격 대상
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    ABattleUnitBase* CurrentTarget = nullptr;

    // 사망 여부
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
    bool bIsDead = false;

    // 피해
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void ReceiveDamage(float DamageAmount);

    // 회복
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void Heal(float HealAmount);

    // 마나 획득
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void AddMana(float ManaAmount);

    // 사망
    UFUNCTION(BlueprintCallable, Category = "Combat")
    void Die();

    // 생존 여부
    UFUNCTION(BlueprintPure, Category = "Combat")
    bool IsAlive() const;
};