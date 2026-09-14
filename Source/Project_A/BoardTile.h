#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoardTile.generated.h"

//타일 위에 올라갈 수 있는 유닛을 정의
UENUM(BlueprintType)
enum class EBoardTileTeam : uint8
{
    Player  UMETA(DisplayName = "Player"),
    Enemy   UMETA(DisplayName = "Enemy"),
    Neutral UMETA(DisplayName = "Neutral")
};

//ABoardTile 클래스 정의
UCLASS()
class PROJECT_A_API ABoardTile : public AActor
{
    GENERATED_BODY()

public:
    ABoardTile();

protected:
    virtual void BeginPlay() override;

public:
	//타일의 메쉬 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tile")
    UStaticMeshComponent* TileMesh;

	//타일의 위치를 나타내는 그리드 좌표
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    int32 GridX = 0;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    int32 GridY = 0;

	//타일의 팀 타입을 나타냄
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    EBoardTileTeam TeamType = EBoardTileTeam::Neutral;

	//타일에 유닛이 올라가있는지 여부
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    bool bOccupied = false;

	//타일에 올라가 있는 유닛 포인터값
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    AActor* OccupiedUnit = nullptr;

	//타일에 유닛 배치 가능 여부를 반환
    UFUNCTION(BlueprintCallable, Category = "Tile")
    bool CanPlaceUnit() const;

	//타일에 유닛 배치
    UFUNCTION(BlueprintCallable, Category = "Tile")
    void SetOccupiedUnit(AActor* Unit);

	//타일에 유닛 제거
    UFUNCTION(BlueprintCallable, Category = "Tile")
    void ClearTile();
};