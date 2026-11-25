#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "../Skills/UnitSkillProgress.h"
#include "ShopTypes.generated.h"

UENUM(BlueprintType)
enum class EShopPhase : uint8
{
    Inactive, Preparation, Combat, Settlement, RewardSelection, Finished
};

USTRUCT(BlueprintType)
struct FShopMatchState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    EShopPhase Phase = EShopPhase::Inactive;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    int32 Round = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    int32 DeploymentLimit = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    bool bRoundCleared = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    int32 ClearGoldAwarded = 0;

    bool CanTrade() const { return Phase == EShopPhase::Preparation || Phase == EShopPhase::Combat; }
    bool CanManageBoard() const { return Phase == EShopPhase::Preparation || Phase == EShopPhase::RewardSelection; }
};

UENUM(BlueprintType)
enum class ETransactionResult : uint8
{
    Success, WrongPhase, InvalidSelection, InsufficientGold, NoSpace, Unavailable, ChangeRejected
};

UENUM(BlueprintType)
enum class EUnitLocation : uint8
{
    None, Bench, Board
};

USTRUCT(BlueprintType)
struct FUnitPosition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    EUnitLocation Location = EUnitLocation::None;

    // Bench uses Column as a zero-based slot index.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    int32 Row = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    int32 Column = INDEX_NONE;
};

// Shop offer data. UnitId is the identifier supplied by the combat team.
USTRUCT(BlueprintType)
struct FShopOffer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    FName UnitId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop", meta=(ClampMin="1"))
    int32 Cost = 1;
};

USTRUCT(BlueprintType)
struct FShopRoundOdds
{
    GENERATED_BODY()

    // Index 0 corresponds to cost 1. Only tiers in the configured catalog are eligible.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    TArray<float> CostWeights;
};

USTRUCT(BlueprintType)
struct FItemBonuses
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float HP = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float AttackDamage = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float AbilityPower = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float Armor = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float AttackSpeed = 0;
    // 증가율은 0.2 = 20%. 같은 능력치의 증가율은 합산한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float HealthPercent = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float AttackDamagePercent = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float AbilityPowerPercent = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float ArmorPercent = 0;
    // 0.1 = 평타/피격 마나 획득량 +10%. 시작 마나와 스킬의 직접 지급에는 적용하지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float ManaGainPercent = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float MagicResistance = 0;
    // 0.1 = 치명타 확률 +10%p. 피해 배율 0.2 = 기존 1.5배에서 1.7배.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float CriticalChance = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float CriticalMultiplier = 0;
    // 전투 시작 시 지급할 마나에 고정 수치를 더한다. 마나 획득 증가율은 적용하지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop") float StartMana = 0;

    void Add(const FItemBonuses& Other);
};

USTRUCT(BlueprintType)
struct FItemDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    TObjectPtr<class UTexture2D> Icon = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    FItemBonuses Bonuses;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    bool bCanDropOnKill = true;
};

USTRUCT(BlueprintType)
struct FRosterUnit
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    FGuid Id;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    FName UnitId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    int32 Stars = 1;

    // Price paid when purchased. Reward units use their configured base cost.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    int32 BaseCost = 0;
    // 기물 고유 스킬 기록. 합성 결과/새 전투 Actor에 이관하며 DT 설정과 구분한다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    TArray<FUnitSkillProgress> SkillProgress;
};

UENUM(BlueprintType)
enum class ERewardKind : uint8
{
    Unit, Item, Gold, RandomUnit, RandomItem
};

USTRUCT(BlueprintType)
struct FRewardOffer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    ERewardKind Kind = ERewardKind::Item;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    FName DefinitionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop", meta=(ClampMin="1", ClampMax="2"))
    int32 Stars = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop", meta=(ClampMin="0"))
    float Weight = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop", meta=(ClampMin="0"))
    int32 GoldAmount = 0;
    // RandomUnit 전용. DefinitionId는 비워두고, 이 코스트에서 Stars 성급 유닛을 추첨한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop", meta=(ClampMin="1", ClampMax="5"))
    int32 UnitCost = 1;
};

UENUM(BlueprintType)
enum class ERosterUnitEdit : uint8
{
    Add, Remove, Update, Move
};

USTRUCT(BlueprintType)
struct FRosterUnitChange
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    ERosterUnitEdit Edit = ERosterUnitEdit::Update;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    FRosterUnit Unit;

    // Used for Add and Move. Update preserves the unit's current position.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    FUnitPosition Destination;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    bool bReplaceEquipment = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    TArray<FName> Equipment;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    float StarStatMultiplier = 1;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Shop")
    float StarVisualScale = 1;
};

USTRUCT(BlueprintType)
struct FRosterUnitProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float MaxHP = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float AttackDamage = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float AbilityPower = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float Armor = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float AttackSpeed = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float AttackRange = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shop")
    float MaxMP = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FManagerChanged);
