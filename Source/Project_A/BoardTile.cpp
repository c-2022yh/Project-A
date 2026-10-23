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
void ABoardTile::BeginPlay() { Super::BeginPlay(); }
bool ABoardTile::CanPlaceUnit() const { return !IsValid(OccupiedUnit); }
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
void ABoardTile::SetOccupiedUnit(AActor* Unit)
{
    if (!IsValid(Unit) || (IsValid(OccupiedUnit) && OccupiedUnit != Unit)) return;
    if (ABattleUnitBase* BattleUnit = Cast<ABattleUnitBase>(Unit))
    {
        if (IsValid(BattleUnit->CurrentTile) && BattleUnit->CurrentTile != this) BattleUnit->CurrentTile->ClearTile();
        BattleUnit->CurrentTile = this;
    }
    OccupiedUnit = Unit;
    bOccupied = true;
}
void ABoardTile::ClearTile()
{
    if (ABattleUnitBase* Unit = Cast<ABattleUnitBase>(OccupiedUnit))
        if (Unit->CurrentTile == this) Unit->CurrentTile = nullptr;
    OccupiedUnit = nullptr;
    bOccupied = false;
}
