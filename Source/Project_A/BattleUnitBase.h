#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BattleUnitBase.generated.h"

class ABoardTile;
class UStaticMeshComponent;
class ABattleUnitBase;

UENUM(BlueprintType)
enum class EBattleTeam : uint8
{
    Player = 0 UMETA(DisplayName = "Player"),
    Enemy = 1 UMETA(DisplayName = "Enemy")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBattleUnitEvent, ABattleUnitBase*, Unit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBattleUnitAttackEvent, ABattleUnitBase*, Attacker, ABattleUnitBase*, Target);

UCLASS()
class PROJECT_A_API ABattleUnitBase : public ACharacter
{
    GENERATED_BODY()
public:
    ABattleUnitBase();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
protected:
    virtual void BeginPlay() override;
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit")
    EBattleTeam TeamType = EBattleTeam::Player;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats", meta=(ClampMin="1"))
    float MaxHP = 100.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats")
    float CurrentHP = 100.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats", meta=(ClampMin="0"))
    float MaxMP = 100.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats")
    float CurrentMP = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats", meta=(ClampMin="0"))
    float AttackDamage = 10.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats", meta=(ClampMin="0"))
    float Armor = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats", meta=(ClampMin="0.01"))
    float AttackSpeed = 1.0f;
    // Distance between capsule surfaces, not between their centers.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats", meta=(ClampMin="0"))
    float AttackRange = 150.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats", meta=(ClampMin="0"))
    float MovementSpeed = 600.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
    ABattleUnitBase* CurrentTarget = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
    bool bIsDead = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
    bool bInCombat = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Board")
    bool bIsDeployed = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Board")
    TObjectPtr<ABoardTile> CurrentTile = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Unit|Visual")
    TObjectPtr<UStaticMeshComponent> PlaceholderMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit|Visual")
    bool bUsePlaceholderMesh = true;
    UPROPERTY(BlueprintAssignable, Category="Combat")
    FBattleUnitEvent OnHealthChanged;
    UPROPERTY(BlueprintAssignable, Category="Combat")
    FBattleUnitEvent OnUnitDied;
    UPROPERTY(BlueprintAssignable, Category="Combat")
    FBattleUnitAttackEvent OnAttack;

    UFUNCTION(BlueprintCallable, Category="Combat") void ReceiveDamage(float DamageAmount);
    UFUNCTION(BlueprintCallable, Category="Combat") void Heal(float HealAmount);
    UFUNCTION(BlueprintCallable, Category="Combat") void AddMana(float ManaAmount);
    UFUNCTION(BlueprintCallable, Category="Combat") void Die();
    UFUNCTION(BlueprintPure, Category="Combat") bool IsAlive() const;
    UFUNCTION(BlueprintCallable, Category="Combat") void StartCombat();
    UFUNCTION(BlueprintCallable, Category="Combat") void StopCombat();
    UFUNCTION(BlueprintCallable, Category="Board") bool PlaceOnTile(ABoardTile* Tile);
private:
    float AttackCooldown = 0.0f;
    float TargetRefreshTime = 0.0f;
    void FindTarget();
};
