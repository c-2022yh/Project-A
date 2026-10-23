#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoardManager.generated.h"

class ABoardTile;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBoardGeneratedEvent);

UCLASS()
class PROJECT_A_API ABoardManager : public AActor
{
    GENERATED_BODY()

public:
    ABoardManager();

protected:
    virtual void BeginPlay() override;

public:

    //씬 루트 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    USceneComponent* SceneRoot;

    //생성할 타일 클래스
    //BP_BoardTile Blueprint 클래스를 인스펙터에서 지정
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    TSubclassOf<ABoardTile> TileClass;

    //행 개수
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Rows = 6;

    //열 개수
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Columns = 5;

    // Rows start at negative world Y (the current camera side).
    // Values outside the board size are clamped during generation.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ClampMin = "0", ToolTip = "Number of player rows from the negative world Y side. Remaining rows belong to the enemy."))
    int32 PlayerRows = 3;

    // Leaving a material unset preserves the tile Blueprint's material.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Player tile material for mesh slot 0. Leave unset to keep the tile Blueprint material."))
    TObjectPtr<UMaterialInterface> PlayerTileMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Enemy tile material for mesh slot 0. Leave unset to keep the tile Blueprint material."))
    TObjectPtr<UMaterialInterface> EnemyTileMaterial = nullptr;

    //육각 타일의 가로 간격
    // Automatic spacing assumes the current point-up hex layout (90 degree tile yaw).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing")
    bool bAutoCalculateSpacing = true;

    // 0 = touching tiles, 0.02 = 2 percent spacing.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.0", UIMax = "0.2", EditCondition = "bAutoCalculateSpacing"))
    float GapRatio = 0.02f;

    // Manual values are preserved while automatic spacing is enabled.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.01", EditCondition = "!bAutoCalculateSpacing"))
    float HorizontalSpacing = 173.2f;

    //육각 타일의 세로 간격
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.01", EditCondition = "!bAutoCalculateSpacing"))
    float VerticalSpacing = 150.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveHorizontalSpacing = 0.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveVerticalSpacing = 0.0f;

    //생성된 모든 타일 저장
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    TArray<ABoardTile*> Tiles;

    //보드 생성
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

	
};