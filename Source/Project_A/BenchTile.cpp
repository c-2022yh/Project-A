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
	return !bOccupied && !IsValid(OccupiedUnit);
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

void ABenchTile::SetOccupiedUnit(AActor* Unit)
{
	if (!IsValid(Unit) || bOccupied != IsValid(OccupiedUnit)
		|| (bOccupied && OccupiedUnit != Unit)) return;
	OccupiedUnit = Unit;
	bOccupied = true;
}

void ABenchTile::ClearTile()
{
	OccupiedUnit = nullptr;
	bOccupied = false;
}
