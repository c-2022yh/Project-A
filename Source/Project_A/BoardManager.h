#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AutoBattleGameMode.h"
#include "BoardManager.generated.h"

class ABoardTile;
class ABenchTile;
class ABattleUnitBase;
class UMaterialInterface;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBoardGeneratedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUnitMovedToBenchEvent, ABattleUnitBase*, Unit);

UCLASS()
class PROJECT_A_API ABoardManager : public AActor
{
    GENERATED_BODY()

public:
    ABoardManager();

protected:
    virtual void BeginPlay() override;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    USceneComponent* SceneRoot;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    TSubclassOf<ABoardTile> TileClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Bench")
    TSubclassOf<ABenchTile> BenchTileClass;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board|Bench")
    int32 BenchSlotCount = 7;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Bench",
        meta = (DisplayName = "Bench Start Offset", ToolTip = "Offset from the default first bench slot position."))
    FVector BenchAreaOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Rows = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Columns = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ClampMin = "0", ToolTip = "Number of player rows from the negative world Y side."))
    int32 PlayerRows = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Player tile material for mesh slot 0."))
    TObjectPtr<UMaterialInterface> PlayerTileMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Enemy tile material for mesh slot 0."))
    TObjectPtr<UMaterialInterface> EnemyTileMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing")
    bool bAutoCalculateSpacing = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.0", UIMax = "0.2", EditCondition = "bAutoCalculateSpacing"))
    float GapRatio = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.01", EditCondition = "!bAutoCalculateSpacing"))
    float HorizontalSpacing = 173.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.01", EditCondition = "!bAutoCalculateSpacing"))
    float VerticalSpacing = 150.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveHorizontalSpacing = 0.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveVerticalSpacing = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    TArray<ABoardTile*> Tiles;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board|Bench")
    TArray<ABenchTile*> BenchSlots;

    UFUNCTION(BlueprintCallable, Category = "Board")
    void GenerateBoard();

    UPROPERTY(BlueprintAssignable, Category = "Board")
    FBoardGeneratedEvent OnBoardGenerated;

    UFUNCTION(BlueprintPure, Category = "Board")
    ABoardTile* GetTile(int32 Row, int32 Column) const;

    UFUNCTION(BlueprintPure, Category = "Board")
    TArray<ABoardTile*> GetPlayerTiles() const;

    UFUNCTION(BlueprintPure, Category = "Board")
    TArray<ABoardTile*> GetEnemyTiles() const;

    UFUNCTION(BlueprintPure, Category = "Board|Bench")
    ABenchTile* GetBenchSlot(int32 SlotIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Board|Bench")
    void SynchronizeBenchOccupancy();

    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    void SavePlayerFormation();

    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool RestorePlayerFormation();

    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool TryPlaceUnitOnTile(ABattleUnitBase* Unit, ABoardTile* TargetTile);

    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool TryMoveUnitToBench(ABattleUnitBase* Unit, ABenchTile* TargetSlot);

    UPROPERTY(BlueprintAssignable, Category = "Board|Bench")
    FUnitMovedToBenchEvent OnUnitMovedToBench;

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<ABattleUnitBase>> SavedDeployedUnits;

    UPROPERTY(Transient)
    TArray<TObjectPtr<ABoardTile>> SavedDeployedTiles;

    ABenchTile* FindBenchSlotForWaitingUnit(const ABattleUnitBase* Unit) const;
    bool IsUnitMovementPhaseActive(const AAutoBattleGameMode* Mode) const;

    UFUNCTION()
    void HandleGamePhaseChanged(EGamePhase Phase);
};
