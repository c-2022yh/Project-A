#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoardManager.generated.h"

class ABoardTile;

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

    //육각 타일의 가로 간격
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    float HorizontalSpacing = 173.2f;

    //육각 타일의 세로 간격
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    float VerticalSpacing = 150.0f;

    //생성된 모든 타일 저장
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    TArray<ABoardTile*> Tiles;

    //보드 생성
    UFUNCTION(BlueprintCallable, Category = "Board")
    void GenerateBoard();

	
};