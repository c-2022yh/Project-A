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

class ABoardManager;
class ABattleUnitBase;

//ABoardTile 클래스 정의
UCLASS()
class PROJECT_A_API ABoardTile : public AActor
{
    GENERATED_BODY()

public:
    ABoardTile();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

public:
	//타일의 메쉬 컴포넌트
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tile")
    UStaticMeshComponent* TileMesh;

    //보기 쉬운 보드 배열 좌표
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    int32 Row = 0;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    int32 Column = 0;

	//육각형 좌표계에서의 좌표
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    int32 HexQ = 0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    int32 HexR = 0;

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

    UFUNCTION(BlueprintPure, Category = "Tile")
    FVector GetSurfaceLocation() const;

	//타일에 유닛 배치
    UFUNCTION(BlueprintCallable, Category = "Tile")
    void SetOccupiedUnit(AActor* Unit);

	//타일에 유닛 제거
    UFUNCTION(BlueprintCallable, Category = "Tile")
    void ClearTile();
    // 지형 통행 불가와 유닛에 의한 일시적인 길막을 구분한다.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tile|Movement") bool bBlocksMovement = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Tile|Board") TObjectPtr<ABoardManager> OwningBoard = nullptr;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Tile|Movement") TObjectPtr<ABattleUnitBase> ReservedUnit = nullptr;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Tile|Movement") int32 ReservedMoveId = 0;
    UFUNCTION(BlueprintPure, Category="Tile|Movement") bool IsReserved() const;
    UFUNCTION(BlueprintPure, Category="Tile|Movement") bool IsReservedFor(ABattleUnitBase* Unit, int32 MoveId) const;
private:
    friend class ABoardManager;
    void AssignOccupant(AActor* Unit);
    void ClearOccupant();
};