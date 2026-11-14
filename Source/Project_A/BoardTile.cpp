#include "BoardTile.h"
#include "BattleUnitBase.h"
#include "Components/StaticMeshComponent.h"

ABoardTile::ABoardTile()
{
    PrimaryActorTick.bCanEverTick = false;
    TileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TileMesh"));
    RootComponent = TileMesh;
    TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    TileMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}
bool ABoardTile::CanPlaceUnit() const { return HasConsistentOccupancy() && !bOccupied; }
// [검사 범위] 이 검사는 로컬 플래그/포인터만 비교한다. 유닛 역참조·중복 점유 검사는 BoardManager가 안정된 경계에서 수행한다.
bool ABoardTile::HasConsistentOccupancy() const { return bOccupied == IsValid(OccupiedUnit); }
FVector ABoardTile::GetSurfaceLocation() const
{
    FVector Location = GetActorLocation();
    if (IsValid(TileMesh) && TileMesh->GetStaticMesh())
    {
        const FBoxSphereBounds Bounds = TileMesh->CalcBounds(TileMesh->GetComponentTransform());
        Location.Z = Bounds.Origin.Z + Bounds.BoxExtent.Z;
    }
    return Location;
}
// [저수준 연계] 기존 CurrentTile을 비우고 양방향 참조를 연결한다. 등록·단계·한도·대기석 정리는 검사하지 않으므로 외부 배치 진입점으로 쓰지 않는다.
void ABoardTile::SetOccupiedUnit(AActor* Unit)
{
    if (!IsValid(Unit) || !HasConsistentOccupancy()
        || (bOccupied && OccupiedUnit != Unit)) return;
    if (ABattleUnitBase* BattleUnit = Cast<ABattleUnitBase>(Unit))
    {
        if (IsValid(BattleUnit->CurrentTile) && BattleUnit->CurrentTile != this) BattleUnit->CurrentTile->ClearTile();
        BattleUnit->CurrentTile = this;
    }
    OccupiedUnit = Unit;
    bOccupied = true;
}
// [저수준 연계] 이 타일을 가리키는 유닛 역참조만 해제한다. 판매/합성 보상이나 출전 플래그까지 갱신하는 함수가 아니다.
void ABoardTile::ClearTile()
{
    if (ABattleUnitBase* Unit = Cast<ABattleUnitBase>(OccupiedUnit))
        if (Unit->CurrentTile == this) Unit->CurrentTile = nullptr;
    OccupiedUnit = nullptr;
    bOccupied = false;
}
