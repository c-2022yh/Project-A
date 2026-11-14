#include "BoardManager.h"

#include "AutoBattleGameMode.h"
#include "BattleUnitBase.h"
#include "BenchTile.h"
#include "BoardTile.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"

ABoardManager::ABoardManager()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
    BenchTileClass = ABenchTile::StaticClass();
}

void ABoardManager::BeginPlay()
{
    Super::BeginPlay();
    GenerateBoard();
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
        Mode->OnPhaseChanged.AddDynamic(this, &ABoardManager::HandleGamePhaseChanged);
}

void ABoardManager::HandleGamePhaseChanged(EGamePhase Phase)
{
    if (Phase == EGamePhase::RoundClear || Phase == EGamePhase::GameOver || Phase == EGamePhase::Victory)
        RestorePlayerFormation();
}

void ABoardManager::GenerateBoard()
{
    UWorld* World = GetWorld();
    if (!World || !TileClass || Rows <= 0 || Columns <= 0) return;

    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile)) Tile->Destroy();
    for (ABenchTile* Slot : BenchSlots)
        if (IsValid(Slot)) Slot->Destroy();
    Tiles.Empty();
    BenchSlots.Empty();
    EffectiveHorizontalSpacing = 0.0f;
    EffectiveVerticalSpacing = 0.0f;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FRotator TileRotation(0.0f, 90.0f, 0.0f);
    ABoardTile* FirstTile = World->SpawnActor<ABoardTile>(TileClass, GetActorLocation(), TileRotation, SpawnParams);
    if (!FirstTile) return;

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
        const FVector TileSize = Mesh->CalcBounds(Mesh->GetComponentTransform()).BoxExtent * 2.0f;
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
            if (!NewTile) continue;

            NewTile->SetActorLocation(SpawnLocation);
            NewTile->Row = Row;
            NewTile->Column = Col;
            NewTile->HexQ = Col - (Row - (Row & 1)) / 2;
            NewTile->HexR = Row;
            const bool bPlayerTile = Row < ClampedPlayerRows;
            NewTile->TeamType = bPlayerTile ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;
            UMaterialInterface* TeamMaterial = bPlayerTile ? PlayerTileMaterial.Get() : EnemyTileMaterial.Get();
            if (IsValid(NewTile->TileMesh) && TeamMaterial) NewTile->TileMesh->SetMaterial(0, TeamMaterial);
            NewTile->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
            Tiles.Add(NewTile);
        }
    }

    if (BenchTileClass && BenchSlotCount > 0)
    {
        FVector BenchCenter = GetActorLocation();
        const TArray<ABoardTile*> PlayerTiles = GetPlayerTiles();
        if (!PlayerTiles.IsEmpty() && IsValid(PlayerTiles[0]->TileMesh))
        {
            const FBoxSphereBounds NearBounds = PlayerTiles[0]->TileMesh->CalcBounds(
                PlayerTiles[0]->TileMesh->GetComponentTransform());
            BenchCenter = FVector(GetActorLocation().X,
                NearBounds.Origin.Y - NearBounds.BoxExtent.Y + 120.0f - 600.0f,
                PlayerTiles[0]->GetActorLocation().Z);
        }
        const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
        const FVector Offset = IsValid(Mode) ? Mode->WaitingAreaOffset : FVector::ZeroVector;
        const FVector BenchStartLocation = BenchCenter + Offset + BenchAreaOffset
            - FVector((BenchSlotCount - 1) * EffectiveHorizontalSpacing * 0.5f, 0.0f, 0.0f);
        for (int32 Index = 0; Index < BenchSlotCount; ++Index)
        {
            const FVector Location = BenchStartLocation + FVector(Index * EffectiveHorizontalSpacing, 0.0f, 0.0f);
            ABenchTile* Slot = World->SpawnActor<ABenchTile>(BenchTileClass, Location, TileRotation, SpawnParams);
            if (!Slot) continue;
            Slot->SlotIndex = Index;
            Slot->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
            BenchSlots.Add(Slot);
        }
    }
    else if (BenchSlotCount > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: BenchTileClass is unset; waiting slots were not generated."));
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

ABenchTile* ABoardManager::GetBenchSlot(int32 SlotIndex) const
{
    for (ABenchTile* Slot : BenchSlots)
        if (IsValid(Slot) && Slot->SlotIndex == SlotIndex) return Slot;
    return nullptr;
}

ABenchTile* ABoardManager::FindBenchSlotForWaitingUnit(const ABattleUnitBase* Unit) const
{
    if (!IsValid(Unit)) return nullptr;
    for (ABenchTile* Slot : BenchSlots)
        if (IsValid(Slot) && Slot->OccupiedUnit == Unit) return Slot;

    ABenchTile* BestSlot = nullptr;
    float BestDistance = TNumericLimits<float>::Max();
    for (ABenchTile* Slot : BenchSlots)
    {
        if (!IsValid(Slot) || !Slot->CanPlaceUnit()) continue;
        const float Distance = FVector::Dist2D(Unit->GetActorLocation(), Slot->GetActorLocation());
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            BestSlot = Slot;
        }
    }
    return BestSlot;
}

void ABoardManager::SynchronizeBenchOccupancy()
{
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(Mode) || BenchSlots.IsEmpty()) return;
    for (ABenchTile* Slot : BenchSlots)
    {
        if (!IsValid(Slot)) continue;
        ABattleUnitBase* Occupant = Cast<ABattleUnitBase>(Slot->OccupiedUnit);
        if (!IsValid(Occupant) || Occupant->TeamType != EBattleTeam::Player || !Occupant->IsAlive()
            || Occupant->bInCombat || Occupant->bIsDeployed || IsValid(Occupant->CurrentTile))
            Slot->ClearTile();
        else if (!Slot->bOccupied)
            Slot->bOccupied = true;
    }
    for (ABattleUnitBase* Unit : Mode->BattleUnits)
    {
        if (!IsValid(Unit) || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive()
            || Unit->bIsDeployed || IsValid(Unit->CurrentTile)) continue;
        if (ABenchTile* Slot = FindBenchSlotForWaitingUnit(Unit))
        {
            Slot->SetOccupiedUnit(Unit);
            Unit->SetActorLocation(Slot->GetSurfaceLocation()
                + FVector(0.0f, 0.0f, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f),
                false, nullptr, ETeleportType::TeleportPhysics);
        }
    }
}

void ABoardManager::SavePlayerFormation()
{
    SavedDeployedUnits.Empty();
    SavedDeployedTiles.Empty();
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(Mode) || !Mode->bEncounterReady) return;
    SynchronizeBenchOccupancy();

    for (ABattleUnitBase* Unit : Mode->BattleUnits)
    {
        if (!IsValid(Unit) || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive() || !Unit->bIsDeployed)
            continue;
        ABoardTile* Tile = Unit->CurrentTile;
        if (!IsValid(Tile) || !Tiles.Contains(Tile) || Tile->TeamType != EBoardTileTeam::Player
            || Tile->OccupiedUnit != Unit || !Tile->bOccupied)
        {
            SavedDeployedUnits.Empty();
            SavedDeployedTiles.Empty();
            Mode->StatusMessage = TEXT("Formation save failed because a deployed unit has inconsistent tile state.");
            return;
        }
        SavedDeployedUnits.Add(Unit);
        SavedDeployedTiles.Add(Tile);
    }
}

bool ABoardManager::RestorePlayerFormation()
{
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    const bool bResultReady = IsValid(Mode) && (Mode->bBattleResultPending
        || Mode->CurrentPhase == EGamePhase::RoundClear || Mode->CurrentPhase == EGamePhase::GameOver
        || Mode->CurrentPhase == EGamePhase::Victory);
    if (!bResultReady || SavedDeployedUnits.Num() != SavedDeployedTiles.Num()) return false;

    TSet<ABoardTile*> RestoreTiles;
    TSet<ABattleUnitBase*> RestoreUnits;
    for (int32 Index = 0; Index < SavedDeployedUnits.Num(); ++Index)
    {
        ABattleUnitBase* Unit = SavedDeployedUnits[Index];
        ABoardTile* Tile = SavedDeployedTiles[Index];
        if (!IsValid(Unit) || !IsValid(Tile) || !Mode->BattleUnits.Contains(Unit) || !Tiles.Contains(Tile)
            || Unit->TeamType != EBattleTeam::Player || Tile->TeamType != EBoardTileTeam::Player
            || RestoreUnits.Contains(Unit) || RestoreTiles.Contains(Tile)) return false;
        RestoreUnits.Add(Unit);
        RestoreTiles.Add(Tile);
        if (IsValid(Tile->OccupiedUnit) && !RestoreUnits.Contains(Cast<ABattleUnitBase>(Tile->OccupiedUnit)))
        {
            // The occupant must itself be part of the saved formation.
            if (!SavedDeployedUnits.Contains(Cast<ABattleUnitBase>(Tile->OccupiedUnit))) return false;
        }
    }

    for (ABattleUnitBase* Unit : SavedDeployedUnits)
    {
        if (!IsValid(Unit)) continue;
        Unit->StopCombat();
        if (IsValid(Unit->CurrentTile)) Unit->CurrentTile->ClearTile();
    }
    for (ABoardTile* Tile : RestoreTiles)
        if (IsValid(Tile)) Tile->ClearTile();

    for (int32 Index = 0; Index < SavedDeployedUnits.Num(); ++Index)
    {
        ABattleUnitBase* Unit = SavedDeployedUnits[Index];
        ABoardTile* Tile = SavedDeployedTiles[Index];
        if (!IsValid(Unit) || !Unit->IsAlive()) continue;
        if (!Unit->PlaceOnTile(Tile))
        {
            Mode->StatusMessage = TEXT("Formation restore failed. Review the saved player tile assignments.");
            return false;
        }
    }
    SavedDeployedUnits.Empty();
    SavedDeployedTiles.Empty();
    return true;
}

bool ABoardManager::IsUnitMovementPhaseActive(const AAutoBattleGameMode* Mode) const
{
    return IsValid(Mode) && Mode->bEncounterReady
        && (Mode->CurrentPhase == EGamePhase::Preparation
            ? Mode->bPreparationActive
            : Mode->CurrentPhase == EGamePhase::RoundClear);
}

bool ABoardManager::TryPlaceUnitOnTile(ABattleUnitBase* Unit, ABoardTile* TargetTile)
{
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsUnitMovementPhaseActive(Mode) || !IsValid(Unit) || !Mode->BattleUnits.Contains(Unit)
        || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive() || Unit->bInCombat
        || !IsValid(TargetTile) || !Tiles.Contains(TargetTile)
        || TargetTile->TeamType != EBoardTileTeam::Player)
    {
        if (Mode) Mode->StatusMessage = TEXT("Choose a valid player tile during a placement phase.");
        return false;
    }

    SynchronizeBenchOccupancy();
    ABoardTile* PreviousTile = Unit->CurrentTile;
    ABenchTile* PreviousBenchSlot = nullptr;
    const bool bFromBoard = Unit->bIsDeployed;
    if (bFromBoard)
    {
        if (!IsValid(PreviousTile) || !Tiles.Contains(PreviousTile) || PreviousTile->TeamType != EBoardTileTeam::Player
            || PreviousTile->OccupiedUnit != Unit || !PreviousTile->bOccupied)
        {
            Mode->StatusMessage = TEXT("Unit placement state is inconsistent. Positions were not changed.");
            return false;
        }
    }
    else
    {
        if (IsValid(PreviousTile) || !(PreviousBenchSlot = FindBenchSlotForWaitingUnit(Unit))
            || PreviousBenchSlot->OccupiedUnit != Unit || !PreviousBenchSlot->bOccupied)
        {
            Mode->StatusMessage = TEXT("Waiting unit has no valid bench slot. Positions were not changed.");
            return false;
        }
    }

    if (TargetTile == PreviousTile) return true;
    const bool bTargetHasOccupant = IsValid(TargetTile->OccupiedUnit);
    if (TargetTile->bOccupied != bTargetHasOccupant)
    {
        Mode->StatusMessage = TEXT("Target tile occupancy is inconsistent. Positions were not changed.");
        return false;
    }

    ABattleUnitBase* OtherUnit = nullptr;
    if (bTargetHasOccupant)
    {
        OtherUnit = Cast<ABattleUnitBase>(TargetTile->OccupiedUnit);
        if (!IsValid(OtherUnit) || !Mode->BattleUnits.Contains(OtherUnit) || OtherUnit == Unit
            || OtherUnit->TeamType != EBattleTeam::Player || !OtherUnit->IsAlive() || OtherUnit->bInCombat
            || !OtherUnit->bIsDeployed || OtherUnit->CurrentTile != TargetTile)
        {
            Mode->StatusMessage = TEXT("Choose an empty player tile or another deployed ally.");
            return false;
        }
    }
    else if (!bFromBoard && Mode->GetDeployedPlayerCount() >= Mode->MaxBoardUnits)
    {
        Mode->StatusMessage = TEXT("Deployment limit reached. Move an existing unit instead.");
        return false;
    }

    if (bFromBoard)
    {
        if (OtherUnit)
        {
            const FTransform UnitStart = Unit->GetActorTransform();
            const FTransform OtherStart = OtherUnit->GetActorTransform();
            PreviousTile->ClearTile();
            TargetTile->ClearTile();
            const bool bOtherMoved = OtherUnit->PlaceOnTile(PreviousTile);
            const bool bUnitMoved = bOtherMoved && Unit->PlaceOnTile(TargetTile);
            if (!bUnitMoved)
            {
                PreviousTile->ClearTile();
                TargetTile->ClearTile();
                OtherUnit->PlaceOnTile(TargetTile);
                Unit->PlaceOnTile(PreviousTile);
                OtherUnit->SetActorTransform(OtherStart, false, nullptr, ETeleportType::TeleportPhysics);
                Unit->SetActorTransform(UnitStart, false, nullptr, ETeleportType::TeleportPhysics);
                Mode->StatusMessage = TEXT("Unit swap failed. Positions were restored.");
                return false;
            }
        }
        else if (!Unit->PlaceOnTile(TargetTile))
        {
            Mode->StatusMessage = TEXT("Choose an empty player tile.");
            return false;
        }
    }
    else
    {
        const FTransform UnitStart = Unit->GetActorTransform();
        const FTransform OtherStart = OtherUnit ? OtherUnit->GetActorTransform() : FTransform::Identity;
        PreviousBenchSlot->ClearTile();
        if (OtherUnit) TargetTile->ClearTile();
        if (!Unit->PlaceOnTile(TargetTile))
        {
            PreviousBenchSlot->SetOccupiedUnit(Unit);
            Unit->SetActorTransform(UnitStart, false, nullptr, ETeleportType::TeleportPhysics);
            if (OtherUnit)
            {
                TargetTile->SetOccupiedUnit(OtherUnit);
                OtherUnit->SetActorTransform(OtherStart, false, nullptr, ETeleportType::TeleportPhysics);
            }
            Mode->StatusMessage = TEXT("Unit placement failed. Positions were restored.");
            return false;
        }
        if (OtherUnit)
        {
            OtherUnit->bIsDeployed = false;
            PreviousBenchSlot->SetOccupiedUnit(OtherUnit);
            OtherUnit->SetActorLocation(PreviousBenchSlot->GetSurfaceLocation()
                + FVector(0.0f, 0.0f, OtherUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f),
                false, nullptr, ETeleportType::TeleportPhysics);
            OnUnitMovedToBench.Broadcast(OtherUnit);
        }
    }

    Mode->StatusMessage = TEXT("Unit placed. Select another unit or press Enter to start.");
    return true;
}

bool ABoardManager::TryMoveUnitToBench(ABattleUnitBase* Unit, ABenchTile* TargetSlot)
{
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    const bool bCombatPhase = IsValid(Mode) && Mode->CurrentPhase == EGamePhase::Combat;
    const bool bPlacementPhase = IsUnitMovementPhaseActive(Mode);
    if (!Mode || !Mode->bEncounterReady || (!bPlacementPhase && !bCombatPhase)
        || (bCombatPhase && Mode->bBattleResultPending)
        || !IsValid(Unit) || !Mode->BattleUnits.Contains(Unit) || Unit->TeamType != EBattleTeam::Player
        || !Unit->IsAlive() || Unit->bInCombat || !IsValid(TargetSlot) || !BenchSlots.Contains(TargetSlot))
    {
        if (Mode) Mode->StatusMessage = TEXT("Choose a valid bench slot during an allowed phase.");
        return false;
    }

    SynchronizeBenchOccupancy();
    if (!Unit->bIsDeployed && !IsValid(Unit->CurrentTile))
    {
        ABenchTile* SourceSlot = FindBenchSlotForWaitingUnit(Unit);
        if (!IsValid(SourceSlot) || SourceSlot->OccupiedUnit != Unit || !SourceSlot->bOccupied)
        {
            Mode->StatusMessage = TEXT("Waiting unit has no valid bench slot. Positions were not changed.");
            return false;
        }
        const bool bTargetHasOccupant = IsValid(TargetSlot->OccupiedUnit);
        if (TargetSlot->bOccupied != bTargetHasOccupant)
        {
            Mode->StatusMessage = TEXT("Bench slot occupancy is inconsistent. Positions were not changed.");
            return false;
        }
        if (SourceSlot == TargetSlot) return true;
        ABattleUnitBase* OtherUnit = bTargetHasOccupant ? Cast<ABattleUnitBase>(TargetSlot->OccupiedUnit) : nullptr;
        if (bTargetHasOccupant && (!IsValid(OtherUnit) || !Mode->BattleUnits.Contains(OtherUnit)
            || OtherUnit->TeamType != EBattleTeam::Player || !OtherUnit->IsAlive()
            || OtherUnit->bInCombat || OtherUnit->bIsDeployed || IsValid(OtherUnit->CurrentTile)))
        {
            Mode->StatusMessage = TEXT("Choose an empty bench slot or another waiting ally.");
            return false;
        }
        SourceSlot->ClearTile();
        if (OtherUnit) TargetSlot->ClearTile();
        TargetSlot->SetOccupiedUnit(Unit);
        Unit->SetActorLocation(TargetSlot->GetSurfaceLocation()
            + FVector(0.0f, 0.0f, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f),
            false, nullptr, ETeleportType::TeleportPhysics);
        if (OtherUnit)
        {
            SourceSlot->SetOccupiedUnit(OtherUnit);
            OtherUnit->SetActorLocation(SourceSlot->GetSurfaceLocation()
                + FVector(0.0f, 0.0f, OtherUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f),
                false, nullptr, ETeleportType::TeleportPhysics);
        }
        return true;
    }

    if (bCombatPhase || !bPlacementPhase || !Unit->bIsDeployed || !IsValid(Unit->CurrentTile)
        || !Tiles.Contains(Unit->CurrentTile) || Unit->CurrentTile->OccupiedUnit != Unit
        || !Unit->CurrentTile->bOccupied || Unit->CurrentTile->TeamType != EBoardTileTeam::Player)
    {
        Mode->StatusMessage = TEXT("Only deployed allies can return from the board during a placement phase.");
        return false;
    }

    ABoardTile* SourceTile = Unit->CurrentTile;
    const bool bTargetHasOccupant = IsValid(TargetSlot->OccupiedUnit);
    if (TargetSlot->bOccupied != bTargetHasOccupant)
    {
        Mode->StatusMessage = TEXT("Bench slot occupancy is inconsistent. Positions were not changed.");
        return false;
    }
    ABattleUnitBase* OtherUnit = bTargetHasOccupant ? Cast<ABattleUnitBase>(TargetSlot->OccupiedUnit) : nullptr;
    if (OtherUnit && (!IsValid(OtherUnit) || !Mode->BattleUnits.Contains(OtherUnit) || OtherUnit == Unit
        || OtherUnit->TeamType != EBattleTeam::Player || !OtherUnit->IsAlive() || OtherUnit->bInCombat || OtherUnit->bIsDeployed
        || IsValid(OtherUnit->CurrentTile)))
    {
        Mode->StatusMessage = TEXT("Choose an empty bench slot or another waiting ally.");
        return false;
    }

    const FTransform UnitStart = Unit->GetActorTransform();
    const FTransform OtherStart = OtherUnit ? OtherUnit->GetActorTransform() : FTransform::Identity;
    SourceTile->ClearTile();
    if (OtherUnit) TargetSlot->ClearTile();
    Unit->bIsDeployed = false;
    TargetSlot->SetOccupiedUnit(Unit);
    Unit->SetActorLocation(TargetSlot->GetSurfaceLocation()
        + FVector(0.0f, 0.0f, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f),
        false, nullptr, ETeleportType::TeleportPhysics);

    if (OtherUnit && !OtherUnit->PlaceOnTile(SourceTile))
    {
        TargetSlot->ClearTile();
        SourceTile->SetOccupiedUnit(Unit);
        Unit->bIsDeployed = true;
        Unit->SetActorTransform(UnitStart, false, nullptr, ETeleportType::TeleportPhysics);
        TargetSlot->SetOccupiedUnit(OtherUnit);
        OtherUnit->SetActorTransform(OtherStart, false, nullptr, ETeleportType::TeleportPhysics);
        Mode->StatusMessage = TEXT("Unit swap failed. Positions were restored.");
        return false;
    }

    OnUnitMovedToBench.Broadcast(Unit);
    Mode->StatusMessage = TEXT("Unit returned to the bench.");
    return true;
}
