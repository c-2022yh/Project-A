#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchTile.generated.h"

class UStaticMeshComponent;

// [협업: 맵/성장/UI] 대기석 한 칸의 표시와 점유 조회만 제공한다.
// 기물 생성/등록/아이템 보유/판매 처리는 담당하지 않는다. 배정·회수·교환은 BoardManager API로 요청한다.
UCLASS()
class PROJECT_A_API ABenchTile : public AActor
{
	GENERATED_BODY()

public:
	ABenchTile();

	// [맵/아트 연결] 대기석 표시와 Visibility 히트, 바운드 상단 높이 계산에 사용한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bench")
	TObjectPtr<UStaticMeshComponent> TileMesh;

	// [성장/UI 조회] BoardManager가 부여하는 0 기반 슬롯 식별값(기본 0~6). 배열 순서로 추정하거나 외부에서 변경하지 않는다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Bench")
	int32 SlotIndex = INDEX_NONE;

	// [점유 조회] OccupiedUnit과 함께 관리한다. 외부 직접 수정은 중복 점유·성장 처리 불일치를 만들 수 있다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Bench")
	bool bOccupied = false;

	// The bench owns this link; BattleUnitBase::CurrentTile stays null while a unit is waiting.
	// [성장/배치 계약] 대기 중 유닛의 CurrentTile은 nullptr이고 bIsDeployed는 false이다.
	// 그러나 두 값만으로 슬롯 확보를 보장할 수 없으므로, 등록 후 TrySynchronizeBenchOccupancy/ValidateUnitPlacementState 결과를 확인한다.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Bench")
	TObjectPtr<AActor> OccupiedUnit;

	// [조회 주의] 이 슬롯의 로컬 빈칸 여부만 반환한다. 미배정 유닛의 공간 수요나 구매 가능 여부는 BoardManager에서 조회한다.
	UFUNCTION(BlueprintPure, Category = "Bench")
	bool CanPlaceUnit() const;

	// [맵/표현 조회] 메시 바운드 상단의 월드 위치이며 유닛 캡슐 중심 보정은 포함하지 않는다.
	UFUNCTION(BlueprintPure, Category = "Bench")
	FVector GetSurfaceLocation() const;

private:
	friend class ABoardManager;

	// [내부 계약] BoardManager 전용 점유 갱신. 유닛 등록/좌표/출전 상태/아이템을 자동 변경하는 함수가 아니다.
	bool HasConsistentOccupancy() const;
	void SetOccupiedUnit(AActor* Unit);
	void ClearTile();
};
