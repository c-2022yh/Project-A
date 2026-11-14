#include "BenchTile.h"

#include "Components/StaticMeshComponent.h"

ABenchTile::ABenchTile()
{
	PrimaryActorTick.bCanEverTick = false;
	TileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TileMesh"));
	RootComponent = TileMesh;
	TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TileMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

bool ABenchTile::CanPlaceUnit() const
{
	return HasConsistentOccupancy() && !bOccupied;
}

// [검사 범위] 로컬 플래그/포인터만 비교한다. 같은 유닛의 다른 슬롯 점유 여부는 BoardManager가 검사한다.
bool ABenchTile::HasConsistentOccupancy() const
{
	return bOccupied == IsValid(OccupiedUnit);
}

FVector ABenchTile::GetSurfaceLocation() const
{
	FVector Location = GetActorLocation();
	if (IsValid(TileMesh) && TileMesh->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = TileMesh->CalcBounds(TileMesh->GetComponentTransform());
		Location.Z = Bounds.Origin.Z + Bounds.BoxExtent.Z;
	}
	return Location;
}

// [저수준 연계] 대기석 쪽 참조만 기록한다. CurrentTile/bIsDeployed, 실제 위치, 아이템 반환은 이 함수에서 처리하지 않는다.
void ABenchTile::SetOccupiedUnit(AActor* Unit)
{
	if (!IsValid(Unit) || !HasConsistentOccupancy()
		|| (bOccupied && OccupiedUnit != Unit)) return;
	OccupiedUnit = Unit;
	bOccupied = true;
}

// [저수준 연계] 슬롯 참조 해제만 수행한다. 외부 판매/합성 시스템은 이 함수를 직접 호출하지 말고 점유 정리 계약을 보드 담당과 맞춘다.
void ABenchTile::ClearTile()
{
	OccupiedUnit = nullptr;
	bOccupied = false;
}
