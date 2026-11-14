#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShopTypes.generated.h"

UENUM(BlueprintType)
enum class EShopPhase : uint8
{
    Inactive, Preparation, Combat, Settlement, RewardSelection, Finished
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
};

UENUM(BlueprintType)
enum class ERewardKind : uint8
{
    Unit, Item
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
    FItemBonuses ItemBonuses;

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
