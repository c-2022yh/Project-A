#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoardTile.generated.h"

//타일 위에 올라갈 수 있는 유닛을 정의
// [전투/배치 계약] 초기 배치 진영 구분이며 유닛의 전투 팀/출전 한도와는 별개이다.
UENUM(BlueprintType)
enum class EBoardTileTeam : uint8
{
    Player  UMETA(DisplayName = "Player"),
    Enemy   UMETA(DisplayName = "Enemy"),
    Neutral UMETA(DisplayName = "Neutral")
};

//ABoardTile 클래스 정의
// [협업: 맵/전투] 보드 한 칸의 표시·좌표·점유 조회를 담당한다.
// 타일을 비우거나 점유시키는 저수준 함수는 배치 정책 API가 아니다. 외부 파트는 BoardManager를 통해 이동을 요청한다.
UCLASS()
class PROJECT_A_API ABoardTile : public AActor
{
    GENERATED_BODY()

public:
    ABoardTile();

	//타일의 메쉬 컴포넌트
    // [맵/아트 연결] Visibility 히트와 표면 높이 계산에 사용한다. 시각 메시 변경 시 간격·충돌·유닛 배치 높이를 함께 확인한다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tile")
    UStaticMeshComponent* TileMesh;

    //보기 쉬운 보드 배열 좌표
    // [맵/전투 조회] 0 기반 행/열. 생성 이후 직접 바꾸면 GetTile 결과와 육각 좌표가 불일치하므로 읽기 전용으로 취급한다.
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    int32 Row = 0;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    int32 Column = 0;

	//육각형 좌표계에서의 좌표
    // [전투 좌표 계약] odd-row offset을 axial로 변환한 값: Q = Column - (Row - (Row & 1)) / 2, R = Row.
    // 월드 좌표나 배열 인덱스와 구분한다. 이 클래스는 육각 이웃/거리/길찾기를 구현하지 않는다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    int32 HexQ = 0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    int32 HexR = 0;

	//타일의 팀 타입을 나타냄
    // [배치 조회] BoardManager가 생성 시 지정하는 영역 소속. Player 영역 칸 수가 실제 출전 한도는 아니다.
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Tile")
    EBoardTileTeam TeamType = EBoardTileTeam::Neutral;

	//타일에 유닛이 올라가있는지 여부
    // [점유 조회] OccupiedUnit과 함께 관리하는 상태. C++에서도 직접 수정하지 말고 BoardManager 배치 API를 사용한다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    bool bOccupied = false;

	//타일에 올라가 있는 유닛 포인터값
    // [전투/성장 조회] 점유 액터. 유닛 정보는 IsValid 확인 후 ABattleUnitBase로 변환한다. CurrentTile과의 양방향 일치를 유지해야 한다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Tile")
    AActor* OccupiedUnit = nullptr;


	//타일에 유닛 배치 가능 여부를 반환
    // [조회 주의] 로컬 점유 플래그/포인터가 일치하고 비었는지만 검사한다. 단계·진영·출전 한도·교환 허용 검사가 아니다.
    UFUNCTION(BlueprintCallable, Category = "Tile")
    bool CanPlaceUnit() const;

    // [맵/표현 조회] 월드 위치의 메시 바운드 상단 Z를 반환한다. 캡슐 중심 높이는 포함하지 않으며 정밀 충돌 표면 추적도 아니다.
    UFUNCTION(BlueprintPure, Category = "Tile")
    FVector GetSurfaceLocation() const;

private:
    friend class ABoardManager;
    friend class ABattleUnitBase;
    friend class AAutoBattleGameMode;

    // [내부 계약] 아래 함수는 보드/기존 유닛/초기화 코드만 사용한다. Set은 CurrentTile을 갱신하지만 bIsDeployed 설정은 호출자 책임이다.
    bool HasConsistentOccupancy() const;
    void SetOccupiedUnit(AActor* Unit);
    void ClearTile();
};