#include "BoardTile.h"
#include "Components/StaticMeshComponent.h"

//생성자에서 타일의 메쉬 컴포넌트를 초기화하고 충돌 설정을 적용    
ABoardTile::ABoardTile()
{
    PrimaryActorTick.bCanEverTick = false;

    TileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TileMesh"));
    RootComponent = TileMesh;

    TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ABoardTile::BeginPlay()
{
    Super::BeginPlay();
}

bool ABoardTile::CanPlaceUnit() const
{
    return !bOccupied;
}

void ABoardTile::SetOccupiedUnit(AActor* Unit)
{
    if (Unit == nullptr)
    {
        return;
    }

    OccupiedUnit = Unit;
    bOccupied = true;
}

void ABoardTile::ClearTile()
{
    OccupiedUnit = nullptr;
    bOccupied = false;
}