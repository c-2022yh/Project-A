#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UnitTypes.generated.h"

class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;
class UTexture2D;
class UAnimInstance;
class UAnimSequence;
class UAnimMontage;

UENUM(BlueprintType)
enum class EBattleTeam : uint8 { Player, Enemy };

UENUM(BlueprintType)
enum class EUnitPlacement : uint8 { Unplaced, Waiting, Board };

UENUM(BlueprintType)
enum class EUnitActionState : uint8 { Idle, Moving, BasicAttack, Casting, Dead };

UENUM(BlueprintType)
enum class EUnitDamageType : uint8 { Physical, Magical, TrueDamage };

UENUM(BlueprintType)
enum class EUnitAttackDelivery : uint8 { Melee, Projectile };

// 원본과 최종 능력치는 같은 형식을 사용하지만 서로 다른 값으로 보관한다.
// 사거리는 육각 타일 수다. 마나는 액티브를 가진 기물만 사용한다.
USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitStats
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxHealth = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackDamage = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AbilityPower = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Armor = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MagicResistance = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackSpeed = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AttackRange = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MovementSpeed = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float StartMana = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxMana = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CriticalChance = 0.25f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CriticalMultiplier = 1.5f;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitWeaponVisual
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMesh> Mesh = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SocketName = NAME_None;
    // 소켓 트랜스폼에 추가되는 유닛별 보정. 기본 스케일은 1이다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform RelativeTransform = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitVisualDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;
    // Actor는 바닥에 두고 모델 크기·높이·정면은 VisualRoot에서 보정한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform VisualTransform = FTransform::Identity;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitWeaponVisual MainHand;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitWeaponVisual OffHand;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitAnimationDefinition
{
    GENERATED_BODY()
    // AnimBP가 있으면 사용하고, 없으면 IdleAnimation만 반복 재생한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UAnimInstance> AnimClass;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimSequence> IdleAnimation = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimSequence> MoveAnimation = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> AttackMontage = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> HitMontage = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> DeathMontage = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> SpawnMontage = nullptr;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitBasicAttackDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitAttackDelivery Delivery = EUnitAttackDelivery::Melee;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitDamageType DamageType = EUnitDamageType::Physical;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackDamageRatio = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FlatDamage = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bCanCrit = true;
    // Notify 방식이면 BP AnimNotify에서 CommitAttackHit(ActionId)를 호출한다.
    // 누락된 Notify는 피해 없이 행동을 종료한다. 타이머 방식은 임시 모션 검증용이다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUseAnimNotify = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AnimationSecondsAtSpeedOne = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) float HitTimeRatio = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjectileSpeed = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjectileMaxLifetime = 10.f;
};

UENUM(BlueprintType)
enum class EUnitTrigger : uint8 { ManaFull, CombatStart, AttackStarted, BasicAttackHit, DamageDealt, DamageTaken, Kill };
UENUM(BlueprintType)
enum class EUnitSkillTarget : uint8 { CurrentEnemy, ClosestEnemy, Self, LowestHealthAlly };
UENUM(BlueprintType)
enum class EUnitEffectKind : uint8 { Damage, Healing, Shield, Modifier, EmpowerNextAttack, PermanentGrowth, Mana };
UENUM(BlueprintType)
enum class EUnitGrowthStat : uint8 { MaxHealth, AttackDamage, AbilityPower, Armor, MagicResistance };
UENUM(BlueprintType)
enum class EUnitDamageSource : uint8 { BasicAttack, Skill, Triggered, DamageOverTime };
UENUM(BlueprintType)
enum class EUnitPathResult : uint8 { Unavailable, TemporarilyBlocked, Unreachable, ReservedStep, AlreadyInRange };
UENUM(BlueprintType)
enum class EUnitNegativeResistancePolicy : uint8 { ClampToZero, AmplifyDamage };
UENUM(BlueprintType)
enum class EUnitShieldOrder : uint8 { EarliestExpiry, OldestFirst };

// 활성 효과 목록에서 매번 재계산한다. 제거할 때 스탯을 역으로 빼지 않는다.
USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitStatModifier
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHealth = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AttackDamage = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AbilityPower = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Armor = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MagicResistance = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AttackSpeedBonus = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AttackSpeedSlow = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DamageIncrease = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DamageReduction = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ArmorReduction = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MagicResistanceReduction = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AttackDamageReduction = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float AbilityPowerReduction = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CriticalChance = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CriticalMultiplier = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bStun = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bBlockManaGain = false;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitSkillEffect
{
    GENERATED_BODY()
    // 같은 EffectId는 시전자와 무관하게 같은 효과다. 비우면 SkillId+배열 순서를 사용한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName EffectId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitEffectKind Kind = EUnitEffectKind::Damage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bApplyToSelf = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float BaseValue = 0.f;
    // 음수면 피해/회복/보호막의 기본 수치만 1.8배. AD/AP 계수는 중복 승급하지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float TwoStarBaseValue = -1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackDamageRatio = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float AbilityPowerRatio = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitDamageType DamageType = EUnitDamageType::Magical;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Duration = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitStatModifier Modifier;
    // 궁수 속사처럼 2성에서 버프율만 바뀌는 경우 명시한다. 기본값은 동일 수치다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bOverrideModifierAtTwoStars = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitStatModifier TwoStarModifier;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float EmpowerAttackMultiplier = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitGrowthStat GrowthStat = EUnitGrowthStat::AttackDamage;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float GrowthLimitPerCombat = 3.f;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitSkillDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName SkillId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitTrigger Trigger = EUnitTrigger::ManaFull;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Priority = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 TriggerEvery = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bResetCountOnTargetChange = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float InternalCooldown = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EUnitSkillTarget Target = EUnitSkillTarget::CurrentEnemy;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 CastRange = 4;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AreaRadius = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CastSeconds = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> CastMontage = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUseAnimNotify = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bProjectile = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjectileSpeed = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ProjectileMaxLifetime = 10.f;
    // 일반 스킬은 기절로 취소되지 않는다. 채널링 예외만 명시적으로 켠다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bInterruptibleByStun = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRefundManaWhenInterrupted = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FUnitSkillEffect> Effects;
};

// 미정 정책의 임시값. 단위는 초/월드 길이이며 피해는 반올림하지 않는 float다.
// 피격 마나는 타격당 min(방어/감소 적용 후 피해*계수, 상한), 기본 임시값 0.1/20.
USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitCombatPolicy
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DamageTakenManaRatio = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DamageTakenManaCapPerHit = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float RetargetInterval = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float BlockedTargetGraceSeconds = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float NotifyTimeoutGraceSeconds = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxAttackSpeed = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxTriggerDepth = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EUnitNegativeResistancePolicy NegativeResistance = EUnitNegativeResistancePolicy::ClampToZero;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EUnitShieldOrder ShieldOrder = EUnitShieldOrder::EarliestExpiry;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitDamageRequest
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) TObjectPtr<class ABattleUnitBase> Attacker = nullptr;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<class ABattleUnitBase> Victim = nullptr;
    UPROPERTY(BlueprintReadWrite) float RawDamage = 0.f;
    UPROPERTY(BlueprintReadWrite) EUnitDamageType Type = EUnitDamageType::Physical;
    UPROPERTY(BlueprintReadWrite) EUnitDamageSource Source = EUnitDamageSource::Skill;
    UPROPERTY(BlueprintReadWrite) FName SkillId;
    UPROPERTY(BlueprintReadWrite) int32 ActionId = 0;
    // 동일 요청이 재전달돼도 피해를 중복 적용하지 않는 ID.
    UPROPERTY(BlueprintReadWrite) FGuid DamageId = FGuid::NewGuid();
    UPROPERTY(BlueprintReadWrite) int32 TriggerDepth = 0;
    UPROPERTY(BlueprintReadWrite) TArray<FName> TriggerChain;
    // 발사 시점의 공격 측 계산값. 방어·감소·보호막 계산은 대상의 공통 경로에서 수행한다.
    UPROPERTY(BlueprintReadOnly) bool bCritical = false;
    UPROPERTY(BlueprintReadOnly) float OffenseMultiplier = 1.f;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitDamageResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bApplied = false;
    UPROPERTY(BlueprintReadOnly) bool bCritical = false;
    UPROPERTY(BlueprintReadOnly) float MitigatedDamage = 0.f;
    UPROPERTY(BlueprintReadOnly) float ShieldDamage = 0.f;
    UPROPERTY(BlueprintReadOnly) float HealthDamage = 0.f;
    UPROPERTY(BlueprintReadOnly) bool bKilled = false;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitTimedModifier
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName EffectId;
    UPROPERTY(BlueprintReadOnly) float RemainingSeconds = 0.f;
    UPROPERTY(BlueprintReadOnly) FUnitStatModifier Modifier;
};
USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitShield
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FName EffectId;
    UPROPERTY(BlueprintReadOnly) float Amount = 0.f;
    UPROPERTY(BlueprintReadOnly) float RemainingSeconds = 0.f;
    UPROPERTY(BlueprintReadOnly) int32 Order = 0;
};

// 외형은 BP가 표시하고 비행/명중/피해는 C++가 관리한다. Actor 투사체는 필수가 아니다.
USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitProjectileView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 ProjectileId = 0;
    UPROPERTY(BlueprintReadOnly) FVector Position = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<class ABattleUnitBase> Target = nullptr;
};

USTRUCT(BlueprintType)
struct PROJECT_A_API FUnitDefinition : public FTableRowBase
{
    GENERATED_BODY()
    // 데이터 테이블의 RowName이 유닛 ID다. 별도 ID를 중복 저장하지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(MultiLine=true)) FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UTexture2D> Icon = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1", ClampMax="5")) int32 Cost = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> RoleTags;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitStats BaseStats;
    // 1/2성 표준. 기존 테스트 행을 읽기 위한 배율 필드이며 새 행은 1.8을 사용한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> StarHealthMultipliers = {1.f, 1.8f};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> StarAttackMultipliers = {1.f, 1.8f};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bIsBoss = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FUnitSkillDefinition> Skills;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitBasicAttackDefinition BasicAttack;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitVisualDefinition Visuals;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitAnimationDefinition Animations;
};
