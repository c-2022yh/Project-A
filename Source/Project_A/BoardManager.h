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

    // 행은 월드 Y의 음수 방향, 즉 현재 카메라 쪽부터 시작한다.
    // 설정값이 보드 크기를 벗어나면 생성 시 유효한 범위로 제한한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ClampMin = "0", ToolTip = "Number of player rows from the negative world Y side. Remaining rows belong to the enemy."))
    int32 PlayerRows = 3;

    // 머티리얼을 지정하지 않으면 타일 BP에 설정된 머티리얼을 유지한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Player tile material for mesh slot 0. Leave unset to keep the tile Blueprint material."))
    TObjectPtr<UMaterialInterface> PlayerTileMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Enemy tile material for mesh slot 0. Leave unset to keep the tile Blueprint material."))
    TObjectPtr<UMaterialInterface> EnemyTileMaterial = nullptr;

    //육각 타일의 가로 간격
    // 자동 간격 계산은 현재의 꼭짓점이 위를 향하는 육각형 배치(타일 Yaw 90도)를 기준으로 한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing")
    bool bAutoCalculateSpacing = true;

    // 0이면 타일이 맞닿고, 0.02이면 타일 크기의 2%만큼 간격을 둔다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.0", UIMax = "0.2", EditCondition = "bAutoCalculateSpacing"))
    float GapRatio = 0.02f;

    // 자동 간격 계산을 사용해도 수동 간격 설정값은 보존한다.
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