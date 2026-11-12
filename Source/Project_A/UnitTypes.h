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
// 사거리는 월드 길이가 아니라 육각 타일 수이며 기력은 사용하지 않는다.
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
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CriticalChance = 0.f;
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
    // 투사체와 Notify 판정은 후속 단계에서 연결한다. 아직 공격하지 않는다.
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
    // 성급 배율은 미정이므로 데이터로 명시한다. 1·2·3성 순서다.
    // 체력과 공격력에만 적용하며 기본 배율은 모두 1이다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> StarHealthMultipliers = {1.f, 1.f, 1.f};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<float> StarAttackMultipliers = {1.f, 1.f, 1.f};
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitBasicAttackDefinition BasicAttack;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitVisualDefinition Visuals;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FUnitAnimationDefinition Animations;
};
