#include "BoardManager.h"
#include "BoardTile.h"
#include "BattleUnitBase.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

ABoardManager::ABoardManager()
{
    PrimaryActorTick.bCanEverTick = false;
    
    //씬 루트 컴포넌트 생성
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
}

void ABoardManager::BeginPlay()
{
    Super::BeginPlay();

    GenerateBoard();
}

void ABoardManager::GenerateBoard()
{
    UWorld* World = GetWorld();
    if (!World || !TileClass || Rows <= 0 || Columns <= 0)
    {
        return;
    }

    // 점유 중인 보드는 지우지 않는다. 전투를 정지하고 유닛을 대기석으로 옮긴 뒤 다시 생성한다.
    PruneInvalidReservations();
    for (ABoardTile* Tile : Tiles)
    {
        if (IsValid(Tile) && (IsValid(Tile->OccupiedUnit) || Tile->IsReserved()))
        {
            LastMovementMessage = TEXT("Move units off the board before regenerating tiles.");
            return;
        }
    }
    Reservations.Empty();
    // 보드를 다시 생성할 때 배열뿐 아니라 기존 타일 액터도 제거한다.
    for (ABoardTile* Tile : Tiles)
    {
        if (IsValid(Tile))
        {
            Tile->OwningBoard = nullptr;
            Tile->Destroy();
        }
    }
    Tiles.Empty();
    EffectiveHorizontalSpacing = 0.0f;
    EffectiveVerticalSpacing = 0.0f;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FRotator TileRotation(0.0f, 90.0f, 0.0f);

    // BP 생성 시 설정과 메시 스케일까지 반영하려고 실제 타일을 생성하여 크기를 측정한다.
    // 측정에 사용한 액터는 첫 번째 보드 타일로 재사용한다.
    ABoardTile* FirstTile = World->SpawnActor<ABoardTile>(
        TileClass, GetActorLocation(), TileRotation, SpawnParams);
    if (!FirstTile)
    {
        return;
    }

    float SpacingX = HorizontalSpacing;
    float SpacingY = VerticalSpacing;
    if (bAutoCalculateSpacing)
    {
        UStaticMeshComponent* Mesh = FirstTile->TileMesh;
        if (!IsValid(Mesh) || !Mesh->GetStaticMesh())
        {
            UE_LOG(LogTemp, Warning, TEXT("BoardManager: Assign a TileMesh static mesh before using automatic spacing."));
            FirstTile->Destroy();
            return;
        }

        const FVector TileSize = Mesh->CalcBounds(Mesh->GetComponentTransform()).BoxExtent * 2.0;
        const float GapMultiplier = 1.0f + FMath::Max(0.0f, GapRatio);
        SpacingX = TileSize.X * GapMultiplier;
        SpacingY = TileSize.Y * 0.75f * GapMultiplier;
    }

    if (!FMath::IsFinite(SpacingX) || !FMath::IsFinite(SpacingY)
        || SpacingX <= KINDA_SMALL_NUMBER || SpacingY <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: Tile spacing must be finite and greater than zero."));
        FirstTile->Destroy();
        return;
    }

    EffectiveHorizontalSpacing = SpacingX;
    EffectiveVerticalSpacing = SpacingY;

    // 보드를 중앙에 맞출 때 엇갈린 행이 가로로 반 칸 더 차지하는 폭도 포함한다.
    const float StaggerWidth = Rows > 1 ? SpacingX * 0.5f : 0.0f;
    const float BoardWidth = (Columns - 1) * SpacingX + StaggerWidth;
    const float BoardHeight = (Rows - 1) * SpacingY;
    const int32 ClampedPlayerRows = FMath::Clamp(PlayerRows, 0, Rows);

    for (int32 Row = 0; Row < Rows; ++Row)
    {
        for (int32 Col = 0; Col < Columns; ++Col)
        {
            const float RowOffset = Row % 2 == 1 ? SpacingX * 0.5f : 0.0f;
            const FVector SpawnLocation = GetActorLocation() + FVector(
                Col * SpacingX + RowOffset - BoardWidth * 0.5f,
                Row * SpacingY - BoardHeight * 0.5f,
                0.0f);

            ABoardTile* NewTile = Row == 0 && Col == 0
                ? FirstTile
                : World->SpawnActor<ABoardTile>(TileClass, SpawnLocation, TileRotation, SpawnParams);
            if (!NewTile)
            {
                continue;
            }

            NewTile->OwningBoard = this;
            NewTile->SetActorLocation(SpawnLocation);
            NewTile->Row = Row;
            NewTile->Column = Col;
            NewTile->HexQ = Col - (Row - (Row & 1)) / 2;
            NewTile->HexR = Row;

            const bool bPlayerTile = Row < ClampedPlayerRows;
            NewTile->TeamType = bPlayerTile ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;

            UMaterialInterface* TeamMaterial = bPlayerTile
                ? PlayerTileMaterial.Get() : EnemyTileMaterial.Get();
            if (IsValid(NewTile->TileMesh) && TeamMaterial)
            {
                NewTile->TileMesh->SetMaterial(0, TeamMaterial);
            }

            NewTile->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
            Tiles.Add(NewTile);
        }
    }
    OnBoardGenerated.Broadcast();
}

ABoardTile* ABoardManager::GetTile(int32 Row, int32 Column) const
{
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && Tile->Row == Row && Tile->Column == Column) return Tile;
    return nullptr;
}

TArray<ABoardTile*> ABoardManager::GetPlayerTiles() const
{
    TArray<ABoardTile*> Result;
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && Tile->TeamType == EBoardTileTeam::Player) Result.Add(Tile);
    return Result;
}

TArray<ABoardTile*> ABoardManager::GetEnemyTiles() const
{
    TArray<ABoardTile*> Result;
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && Tile->TeamType == EBoardTileTeam::Enemy) Result.Add(Tile);
    return Result;
}


bool ABoardManager::OwnsTile(const ABoardTile* Tile) const
{
    return IsValid(Tile) && Tile->OwningBoard == this && Tiles.Contains(Tile);
}
ABoardTile* ABoardManager::GetTileByHex(int32 Q, int32 R) const
{
    for (ABoardTile* Tile : Tiles)
        if (OwnsTile(Tile) && Tile->HexQ == Q && Tile->HexR == R) return Tile;
    return nullptr;
}
TArray<ABoardTile*> ABoardManager::GetNeighbors(ABoardTile* Tile) const
{
    TArray<ABoardTile*> Result;
    if (!OwnsTile(Tile)) return Result;
    // 방향 순서를 고정해 같은 조건에서 같은 최단 경로를 선택한다.
    static const FIntPoint Directions[] = {{1,0}, {1,-1}, {0,-1}, {-1,0}, {-1,1}, {0,1}};
    for (const FIntPoint Direction : Directions)
        if (ABoardTile* Neighbor = GetTileByHex(Tile->HexQ + Direction.X, Tile->HexR + Direction.Y)) Result.Add(Neighbor);
    return Result;
}
TArray<ABattleUnitBase*> ABoardManager::GetUnitsOnBoard() const
{
    TArray<ABattleUnitBase*> Result;
    for (ABoardTile* Tile : Tiles)
    {
        if (!OwnsTile(Tile)) continue;
        ABattleUnitBase* Unit = Cast<ABattleUnitBase>(Tile->OccupiedUnit);
        if (IsValid(Unit) && Unit->CurrentTile == Tile && Unit->IsAlive()) Result.AddUnique(Unit);
    }
    return Result;
}
bool ABoardManager::BuildPath(ABattleUnitBase* Unit, ABattleUnitBase* Target, bool bIgnoreUnits, TArray<ABoardTile*>& Path) const
{
    Path.Reset();
    if (!IsValid(Unit) || !IsValid(Target) || Unit == Target || !Unit->IsAlive() || !Target->IsAlive()
        || Unit->TeamType == Target->TeamType || !OwnsTile(Unit->CurrentTile) || !OwnsTile(Target->CurrentTile)
        || Unit->CurrentTile->OccupiedUnit != Unit || Target->CurrentTile->OccupiedUnit != Target) return false;
    ABoardTile* Start = Unit->CurrentTile;
    ABoardTile* Goal = nullptr;
    TArray<ABoardTile*> Queue; Queue.Add(Start);
    TMap<ABoardTile*, ABoardTile*> Parent; Parent.Add(Start, nullptr);
    // 모든 간선 비용이 같은 30칸 보드이므로 BFS로 충분하다. 출발 칸은 점유한 채 탐색한다.
    for (int32 Head = 0; Head < Queue.Num(); ++Head)
    {
        ABoardTile* Current = Queue[Head];
        if (Current != Target->CurrentTile && ABattleUnitBase::HexDistance(Current, Target->CurrentTile) <= FMath::Max(0, Unit->FinalStats.AttackRange))
        { Goal = Current; break; }
        for (ABoardTile* Neighbor : GetNeighbors(Current))
        {
            if (Parent.Contains(Neighbor) || Neighbor->bBlocksMovement || Neighbor == Target->CurrentTile) continue;
            if (!bIgnoreUnits && ((IsValid(Neighbor->OccupiedUnit) && Neighbor->OccupiedUnit != Unit)
                || (Neighbor->IsReserved() && Neighbor->ReservedUnit != Unit))) continue;
            Parent.Add(Neighbor, Current); Queue.Add(Neighbor);
        }
    }
    if (!Goal) return false;
    for (ABoardTile* Step = Goal; Step != Start; Step = Parent.FindChecked(Step)) Path.Insert(Step, 0);
    return true;
}
bool ABoardManager::FindPathToAttackRange(ABattleUnitBase* Unit, ABattleUnitBase* Target, TArray<ABoardTile*>& Path) const
{
    return BuildPath(Unit, Target, false, Path);
}
EUnitPathResult ABoardManager::TryReserveNextStep(ABattleUnitBase* Unit, ABattleUnitBase* Target, int32 MoveId, ABoardTile*& NextTile)
{
    NextTile = nullptr;
    if (bReleasingUnits || !IsValid(Unit) || !IsValid(Target) || MoveId <= 0 || !Unit->bInCombat || !Target->bInCombat
        || Unit->GetWorld() != GetWorld() || Target->GetWorld() != GetWorld()) return EUnitPathResult::Unavailable;
    PruneInvalidReservations();
    if (const FMoveReservation* Existing = Reservations.Find(Unit))
    {
        if (Existing->MoveId == MoveId && IsMoveReservationValid(Unit, MoveId, Existing->Destination.Get()))
        { NextTile = Existing->Destination.Get(); return EUnitPathResult::ReservedStep; }
        LastMovementMessage = TEXT("This unit already has another move reservation.");
        return EUnitPathResult::TemporarilyBlocked;
    }
    if (!Unit->IsAlive() || Unit->IsStunned() || !Target->IsAlive() || Unit->ActionState != EUnitActionState::Idle
        || !OwnsTile(Unit->CurrentTile) || !OwnsTile(Target->CurrentTile) || Unit->CurrentTile->OccupiedUnit != Unit)
        return EUnitPathResult::Unavailable;
    TArray<ABoardTile*> Path;
    if (!BuildPath(Unit, Target, false, Path))
    {
        // 유닛을 무시해도 경로가 없으면 지형상 도달 불가, 경로가 있으면 일시적인 혼잡이다.
        const bool bTerrainReachable = BuildPath(Unit, Target, true, Path);
        LastMovementMessage = bTerrainReachable ? TEXT("Path temporarily blocked by units or reservations.") : TEXT("No terrain path reaches attack range.");
        return bTerrainReachable ? EUnitPathResult::TemporarilyBlocked : EUnitPathResult::Unreachable;
    }
    if (Path.IsEmpty()) return EUnitPathResult::AlreadyInRange;
    ABoardTile* Destination = Path[0];
    if (!Destination->CanPlaceUnit()) return EUnitPathResult::TemporarilyBlocked;
    // 게임 스레드 한 함수 안에서 검사와 예약을 함께 끝낸다. 중간에 BP/비동기 콜백을 호출하지 않는다.
    FMoveReservation Reservation; Reservation.Unit = Unit; Reservation.Source = Unit->CurrentTile;
    Reservation.Destination = Destination; Reservation.MoveId = MoveId; Reservation.CreatedAt = GetWorld()->GetTimeSeconds();
    Destination->ReservedUnit = Unit; Destination->ReservedMoveId = MoveId;
    Reservations.Add(Unit, Reservation);
    NextTile = Destination; LastMovementMessage.Reset();
    return EUnitPathResult::ReservedStep;
}
bool ABoardManager::IsMoveReservationValid(ABattleUnitBase* Unit, int32 MoveId, ABoardTile* Destination) const
{
    if (!IsValid(Unit) || !Unit->IsAlive() || !Unit->bInCombat || !OwnsTile(Destination) || Destination->bBlocksMovement
        || IsValid(Destination->OccupiedUnit) || !Destination->IsReservedFor(Unit, MoveId)) return false;
    const FMoveReservation* Reservation = Reservations.Find(Unit);
    return Reservation && Reservation->MoveId == MoveId && Reservation->Destination == Destination
        && OwnsTile(Reservation->Source.Get()) && Unit->CurrentTile == Reservation->Source
        && Reservation->Source->OccupiedUnit == Unit && ABattleUnitBase::HexDistance(Reservation->Source.Get(), Destination) == 1;
}
bool ABoardManager::CommitReservedMove(ABattleUnitBase* Unit, int32 MoveId)
{
    const FMoveReservation* Found = Reservations.Find(Unit);
    if (!Found || !IsMoveReservationValid(Unit, MoveId, Found->Destination.Get())
        || Unit->ActiveActionId != MoveId || Unit->ActionState != EUnitActionState::Moving) return false;
    ABoardTile* Destination = Found->Destination.Get();
    if (!Unit->GetActorLocation().Equals(Destination->GetSurfaceLocation() + FVector(0,0,1.f), 1.f)) return false;
    ABoardTile* Source = Found->Source.Get();
    Reservations.Remove(Unit);
    Destination->ReservedUnit = nullptr; Destination->ReservedMoveId = 0;
    // 점유 교환은 보드가 한 번에 확정한다. Unit은 타일 내부 값을 직접 수정하지 않는다.
    Source->ClearOccupant(); Destination->AssignOccupant(Unit);
    return Unit->CurrentTile == Destination;
}
bool ABoardManager::CancelReservedMove(ABattleUnitBase* Unit, int32 MoveId)
{
    const FMoveReservation* Found = Reservations.Find(Unit);
    if (!Found || Found->MoveId != MoveId) return false;
    ABoardTile* Destination = Found->Destination.Get();
    Reservations.Remove(Unit); // 콜백보다 먼저 제거해 재진입/늦은 취소가 새 예약을 지우지 않게 한다.
    if (IsValid(Destination) && Destination->IsReservedFor(Unit, MoveId))
    { Destination->ReservedUnit = nullptr; Destination->ReservedMoveId = 0; }
    if (IsValid(Unit)) Unit->CancelBoardMove(MoveId);
    return true;
}
void ABoardManager::PruneInvalidReservations()
{
    const auto Snapshot = Reservations;
    for (const auto& Pair : Snapshot)
    {
        const FMoveReservation& Reservation = Pair.Value;
        ABattleUnitBase* Unit = Reservation.Unit.Get();
        const bool bStarted = IsValid(Unit) && Unit->ActionState == EUnitActionState::Moving && Unit->ActiveActionId == Reservation.MoveId;
        const bool bTimedOut = !bStarted && GetWorld()->GetTimeSeconds() - Reservation.CreatedAt >= FMath::Max(0.1f, UnstartedReservationTimeout);
        const bool bOtherAction = IsValid(Unit) && Unit->ActionState != EUnitActionState::Idle && !bStarted;
        if (!IsMoveReservationValid(Unit, Reservation.MoveId, Reservation.Destination.Get()) || bTimedOut || bOtherAction)
        {
            if (IsValid(Unit)) CancelReservedMove(Unit, Reservation.MoveId);
            else
            {
                ABoardTile* Tile = Reservation.Destination.Get();
                if (IsValid(Tile) && !IsValid(Tile->ReservedUnit) && Tile->ReservedMoveId == Reservation.MoveId)
                { Tile->ReservedUnit = nullptr; Tile->ReservedMoveId = 0; }
                Reservations.Remove(Pair.Key);
            }
        }
    }
}
void ABoardManager::ReleaseUnitTiles(ABattleUnitBase* Unit)
{
    if (!Unit || bReleasingUnits) return;
    TGuardValue<bool> Guard(bReleasingUnits, true);
    if (const FMoveReservation* Found = Reservations.Find(Unit)) CancelReservedMove(Unit, Found->MoveId);
    for (ABoardTile* Tile : Tiles)
    {
        if (!IsValid(Tile)) continue;
        if (Tile->ReservedUnit == Unit) { Tile->ReservedUnit = nullptr; Tile->ReservedMoveId = 0; }
        if (Tile->OccupiedUnit == Unit) Tile->ClearOccupant();
    }
}
bool ABoardManager::TryPlaceUnit(ABattleUnitBase* Unit, ABoardTile* Tile)
{
    if (bReleasingUnits || !IsValid(Unit) || !Unit->IsAlive() || Unit->bInCombat || !OwnsTile(Tile) || Unit->GetWorld() != GetWorld()) return false;
    PruneInvalidReservations();
    const EBoardTileTeam Team = Unit->TeamType == EBattleTeam::Player ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
    if (Tile->TeamType != Team || Tile->bBlocksMovement || Tile->IsReserved() || (IsValid(Tile->OccupiedUnit) && Tile->OccupiedUnit != Unit)) return false;
    if (IsValid(Unit->CurrentTile) && Unit->CurrentTile->OwningBoard != this) return false;
    Tile->AssignOccupant(Unit);
    return Unit->CurrentTile == Tile;
}
void ABoardManager::ClearTileOccupancy(ABoardTile* Tile)
{
    if (!OwnsTile(Tile)) return;
    ABattleUnitBase* Occupant = Cast<ABattleUnitBase>(Tile->OccupiedUnit);
    if (IsValid(Occupant)) ReleaseUnitTiles(Occupant);
    else Tile->ClearOccupant();
    if (IsValid(Tile->ReservedUnit)) CancelReservedMove(Tile->ReservedUnit, Tile->ReservedMoveId);
    Tile->ReservedUnit = nullptr; Tile->ReservedMoveId = 0;
}
void ABoardManager::NotifyTileRemoved(ABoardTile* Tile)
{
    if (!Tile) return;
    const auto Snapshot = Reservations;
    for (const auto& Pair : Snapshot)
        if (Pair.Value.Source == Tile || Pair.Value.Destination == Tile)
            if (ABattleUnitBase* Unit = Pair.Value.Unit.Get()) CancelReservedMove(Unit, Pair.Value.MoveId);
    ABattleUnitBase* Occupant = Cast<ABattleUnitBase>(Tile->OccupiedUnit);
    if (IsValid(Occupant)) { Occupant->StopCombat(); ReleaseUnitTiles(Occupant); }
    Tile->ClearOccupant(); Tile->ReservedUnit = nullptr; Tile->ReservedMoveId = 0;
    Tiles.Remove(Tile);
}
void ABoardManager::EndPlay(const EEndPlayReason::Type Reason)
{
    TArray<ABattleUnitBase*> Units = GetUnitsOnBoard();
    for (const auto& Pair : Reservations) if (ABattleUnitBase* Unit = Pair.Value.Unit.Get()) Units.AddUnique(Unit);
    for (ABattleUnitBase* Unit : Units) { Unit->StopCombat(); ReleaseUnitTiles(Unit); }
    Reservations.Empty();
    const TArray<ABoardTile*> OldTiles = Tiles; Tiles.Empty();
    for (ABoardTile* Tile : OldTiles)
    {
        if (!IsValid(Tile)) continue;
        Tile->OwningBoard = nullptr;
        if (Reason == EEndPlayReason::Destroyed) Tile->Destroy();
    }
    Super::EndPlay(Reason);
}
