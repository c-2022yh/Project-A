#include "BoardTile.h"
#include "BattleUnitBase.h"
#include "BoardManager.h"
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
bool ABoardTile::CanPlaceUnit() const { return !bBlocksMovement && !IsValid(OccupiedUnit) && !IsReserved(); }
bool ABoardTile::IsReserved() const { return IsValid(ReservedUnit); }
bool ABoardTile::IsReservedFor(ABattleUnitBase* Unit, int32 MoveId) const
{
    return IsValid(Unit) && ReservedUnit == Unit && MoveId > 0 && ReservedMoveId == MoveId;
}
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
    if (!IsValid(Unit) || bBlocksMovement || IsReserved() || (IsValid(OccupiedUnit) && OccupiedUnit != Unit)) return;
    if (IsValid(OwningBoard))
    {
        if (ABattleUnitBase* BattleUnit = Cast<ABattleUnitBase>(Unit)) { OwningBoard->TryPlaceUnit(BattleUnit, this); return; }
    }
    AssignOccupant(Unit);
}
void ABoardTile::AssignOccupant(AActor* Unit)
{
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
    if (IsValid(OwningBoard)) { OwningBoard->ClearTileOccupancy(this); return; }
    ClearOccupant();
}
void ABoardTile::ClearOccupant()
{
    if (ABattleUnitBase* Unit = Cast<ABattleUnitBase>(OccupiedUnit))
        if (Unit->CurrentTile == this) Unit->CurrentTile = nullptr;
    OccupiedUnit = nullptr;
    bOccupied = false;
}

void ABoardTile::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(OwningBoard)) OwningBoard->NotifyTileRemoved(this);
    else ClearOccupant();
    Super::EndPlay(Reason);
}
