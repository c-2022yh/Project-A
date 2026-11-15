#include "BoardManager.h"

#include "AutoBattleGameMode.h"
#include "AutoBattlePlayerController.h"
#include "BattleUnitBase.h"
#include "BenchTile.h"
#include "BoardTile.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"

namespace
{
FVector GetUnitLocationOnSurface(const ABattleUnitBase* Unit, const FVector& Surface)
{
    return Surface + FVector(0.0f, 0.0f, 1.0f);
}
}

// Snapshots live only for one synchronous movement; no serialized/BP layout changes are needed.
struct ABoardManager::FUnitPlacementSnapshot
{
    ABattleUnitBase* Unit = nullptr;
    ABoardTile* Tile = nullptr;
    ABenchTile* BenchSlot = nullptr;
    FTransform Transform = FTransform::Identity;
    FTransform PreparationTransform = FTransform::Identity;
    EUnitPlacement Placement = EUnitPlacement::Unplaced;
    bool bWasDeployed = false;
};

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

// [ÏßÑÌñâ ?∞Í≤∞ Í≤ΩÍ≥Ñ] Ï§ÄÎπ?ÏßÑÏûÖ ???ÄÍ∏∞ÏÑù ?ôÍ∏∞?? Í≤∞Í≥º ÏßÑÏûÖ ??Î∞∞Ïπò Î≥µÍ?Î•??òÌñâ?úÎã§. ?êÎß§/?©ÏÑ±/??Î≥¥ÏÉÅ ?±Î°ù ?§Ïóê???±Ïû• ?åÌä∏Í∞Ä Î≥ÑÎèÑ ?ôÍ∏∞?îÎ? ?îÏ≤≠?úÎã§.
void ABoardManager::HandleGamePhaseChanged(EGamePhase Phase)
{
    if (Phase == EGamePhase::Combat) bFormationRestoreCompleted = false;
    // Initial GameMode setup positions units before StartPreparationPhase broadcasts this event.
    // Future roster/reward owners should call SynchronizeBenchOccupancy after registering new waiting units.
    if (Phase == EGamePhase::Preparation)
        SynchronizeBenchOccupancy();
    if (Phase == EGamePhase::RoundClear || Phase == EGamePhase::GameOver || Phase == EGamePhase::Victory)
        RestorePlayerFormation();
}

void ABoardManager::GenerateBoard()
{
    if (bPlacementInProgress || bBoardGenerationInProgress || bBoardGenerationNotificationInProgress)
    {
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: Cannot regenerate during an active board update."));
        return;
    }
    if (HasUnitsPreventingBoardGeneration())
    {
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: Remove registered units and occupied slots before regenerating."));
        return;
    }

    PruneInvalidReservations();
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && Tile->IsReserved()) return;

    bool bGenerated = false;
    {
        TGuardValue<bool> GenerationGuard(bBoardGenerationInProgress, true);
        CancelPlayerInteractions();
        bGenerated = GenerateBoardInternal();
    }
    // Setup listeners must see a stable board and be allowed to query/register waiting units.
    if (bGenerated)
    {
        TGuardValue<bool> NotificationGuard(bBoardGenerationNotificationInProgress, true);
        OnBoardGenerated.Broadcast();
    }
}

bool ABoardManager::HasUnitsPreventingBoardGeneration() const
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (IsValid(Mode))
        for (ABattleUnitBase* Unit : Mode->BattleUnits)
            if (IsValid(Unit)) return true;
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && (IsValid(Tile->OccupiedUnit) || Tile->IsReserved())) return true;
    for (ABenchTile* Slot : BenchSlots)
        if (IsValid(Slot) && IsValid(Slot->OccupiedUnit)) return true;
    return false;
}

// [Îß?BP Í≥ÑÏïΩ] ?ÑÎ≥¥ ?Ä?ºÏùÑ ÎßåÎìú???ôÏïà ?∏Î? ?±Î°ù/Î∞∞Ïπò/Í≤ΩÏ†ú Ï≤òÎ¶¨Î•??òÌñâ?òÏ? ?äÎäî?? Í∏∞Ï°¥ ?Ä??Ï∞∏Ï°∞ Ï∫êÏãú???ùÏÑ± ?±Í≥µ ?åÎ¶º ???§Ïãú Ï°∞Ìöå?úÎã§.
bool ABoardManager::GenerateBoardInternal()
{
    UWorld* World = GetWorld();
    if (!World || !TileClass || Rows <= 0 || Columns <= 0) return false;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FRotator TileRotation(0.0f, 90.0f, 0.0f);
    ABoardTile* FirstTile = World->SpawnActor<ABoardTile>(TileClass, GetActorLocation(), TileRotation, SpawnParams);
    if (!IsValid(FirstTile)) return false;

    float SpacingX = HorizontalSpacing;
    float SpacingY = VerticalSpacing;
    if (bAutoCalculateSpacing)
    {
        UStaticMeshComponent* Mesh = FirstTile->TileMesh;
        if (!IsValid(Mesh) || !Mesh->GetStaticMesh())
        {
            UE_LOG(LogTemp, Warning, TEXT("BoardManager: Assign a TileMesh static mesh before using automatic spacing."));
            FirstTile->Destroy();
            return false;
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
        return false;
    }

    // Cancellation and tile construction can invoke external callbacks; recheck before destruction.
    if (HasUnitsPreventingBoardGeneration() || IsValid(FirstTile->OccupiedUnit))
    {
        FirstTile->Destroy();
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: Units appeared during regeneration; existing board was preserved."));
        return false;
    }

    TArray<ABoardTile*> NewTiles;
    TArray<ABenchTile*> NewBenchSlots;
    const auto DiscardCandidates = [&]()
    {
        for (ABoardTile* Tile : NewTiles) if (IsValid(Tile)) Tile->Destroy();
        for (ABenchTile* Slot : NewBenchSlots) if (IsValid(Slot)) Slot->Destroy();
    };

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
            if (!IsValid(NewTile))
            {
                DiscardCandidates();
                UE_LOG(LogTemp, Warning, TEXT("BoardManager: Tile generation failed; existing board was preserved."));
                return false;
            }

            NewTile->OwningBoard = this;
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
            NewTiles.Add(NewTile);
        }
    }

    if (BenchTileClass && BenchSlotCount > 0)
    {
        FVector BenchCenter = GetActorLocation();
        TArray<ABoardTile*> PlayerTiles;
        for (ABoardTile* Tile : NewTiles)
            if (Tile->TeamType == EBoardTileTeam::Player) PlayerTiles.Add(Tile);
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
            - FVector((BenchSlotCount - 1) * SpacingX * 0.5f, 0.0f, 0.0f);
        for (int32 Index = 0; Index < BenchSlotCount; ++Index)
        {
            const FVector Location = BenchStartLocation + FVector(Index * SpacingX, 0.0f, 0.0f);
            ABenchTile* Slot = World->SpawnActor<ABenchTile>(BenchTileClass, Location, TileRotation, SpawnParams);
            if (!IsValid(Slot))
            {
                DiscardCandidates();
                UE_LOG(LogTemp, Warning, TEXT("BoardManager: Bench generation failed; existing board was preserved."));
                return false;
            }
            Slot->SlotIndex = Index;
            Slot->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
            NewBenchSlots.Add(Slot);
        }
    }
    else if (BenchSlotCount > 0)
    {
        DiscardCandidates();
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: BenchTileClass is unset; existing board was preserved."));
        return false;
    }

    // Construction callbacks must not register/place units before OnBoardGenerated.
    bool bCandidatesValid = !HasUnitsPreventingBoardGeneration();
    for (ABoardTile* Tile : NewTiles)
        if (!IsValid(Tile) || IsValid(Tile->OccupiedUnit)) bCandidatesValid = false;
    for (ABenchTile* Slot : NewBenchSlots)
        if (!IsValid(Slot) || IsValid(Slot->OccupiedUnit)) bCandidatesValid = false;
    if (!bCandidatesValid)
    {
        DiscardCandidates();
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: Candidates changed during construction; existing board was preserved."));
        return false;
    }
    Reservations.Empty();
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile)) { Tile->OwningBoard = nullptr; Tile->Destroy(); }
    for (ABenchTile* Slot : BenchSlots) if (IsValid(Slot)) Slot->Destroy();
    Tiles = MoveTemp(NewTiles);
    BenchSlots = MoveTemp(NewBenchSlots);
    EffectiveHorizontalSpacing = SpacingX;
    EffectiveVerticalSpacing = SpacingY;
    SavedDeployedUnits.Reset();
    SavedDeployedTiles.Reset();
    bHasSavedPlayerFormation = false;
    bFormationRestoreCompleted = false;

    LastPlacementFailureReason.Reset();
    LastBenchFailureReason.Reset();
    LastFormationFailureReason.Reset();
    return true;
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

// [?±Ïû• ?∞Í≤∞ Í≤ΩÍ≥Ñ] ?ΩÍ∏∞ ?ÑÏö© Î∞∞Ï†ï Í≥ÑÌöç. Í∏∞Ï°¥ ÎØ∏Î∞∞???†ÎãõÍπåÏ? Î®ºÏ? ?òÏö©?òÍ≥† ??ÏßÄÍ∏âÎ∂Ñ???êÎã®?úÎã§. ?ÑÏãú Î∞∞Ïó¥??Î∞∞Ï†ï?Ä ?àÏïΩ???ÑÎãà??
bool ABoardManager::BuildBenchSynchronizationPlan(TArray<ABattleUnitBase*>& OutWaitingUnits,
    TArray<ABenchTile*>& OutAssignedSlots, FString& OutFailureReason) const
{
    OutWaitingUnits.Reset();
    OutAssignedSlots.Reset();
    OutFailureReason.Reset();
    if (bPlacementInProgress || bBoardGenerationInProgress)
    {
        OutFailureReason = TEXT("Another placement is being applied.");
        return false;
    }
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(Mode))
    {
        OutFailureReason = TEXT("Bench synchronization requires a valid game mode.");
        return false;
    }

    TSet<ABattleUnitBase*> WaitingSet;
    for (ABattleUnitBase* Unit : Mode->BattleUnits)
    {
        if (!IsValid(Unit) || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive()
            || Unit->bIsDeployed) continue;
        if (Unit->GetWorld() != GetWorld() || Unit->bInCombat || Unit->CurrentTile != nullptr
            || WaitingSet.Contains(Unit))
        {
            OutFailureReason = TEXT("Waiting roster contains a duplicate unit or an invalid placement state.");
            return false;
        }
        for (ABoardTile* Tile : Tiles)
        {
            if (IsValid(Tile) && Tile->OccupiedUnit == Unit)
            {
                OutFailureReason = TEXT("A waiting unit still occupies a board tile.");
                return false;
            }
        }
        WaitingSet.Add(Unit);
        OutWaitingUnits.Add(Unit);
        OutAssignedSlots.Add(nullptr);
    }

    TSet<ABenchTile*> SeenSlots;
    TSet<int32> SeenIndices;
    TArray<ABenchTile*> AvailableSlots;
    for (ABenchTile* Slot : BenchSlots)
    {
        if (!IsValid(Slot) || Slot->GetWorld() != GetWorld() || SeenSlots.Contains(Slot)
            || Slot->SlotIndex < 0 || Slot->SlotIndex >= BenchSlotCount || SeenIndices.Contains(Slot->SlotIndex))
        {
            OutFailureReason = TEXT("Bench contains an invalid or duplicate slot/index.");
            return false;
        }
        SeenSlots.Add(Slot);
        SeenIndices.Add(Slot->SlotIndex);
        ABattleUnitBase* Occupant = Cast<ABattleUnitBase>(Slot->OccupiedUnit);
        if (WaitingSet.Contains(Occupant))
        {
            const int32 UnitIndex = OutWaitingUnits.IndexOfByKey(Occupant);
            if (!Slot->HasConsistentOccupancy() || OutAssignedSlots[UnitIndex] != nullptr)
            {
                OutFailureReason = TEXT("A waiting unit has inconsistent or duplicate bench occupancy.");
                return false;
            }
            OutAssignedSlots[UnitIndex] = Slot;
        }
        else
        {
            // Reclaim obsolete links as before, but only when the entire plan succeeds.
            // Never overwrite an unknown live actor that another system may still own.
            if (IsValid(Slot->OccupiedUnit)
                && (!IsValid(Occupant) || (Occupant->IsAlive()
                    && (!Mode->BattleUnits.Contains(Occupant) || Occupant->TeamType != EBattleTeam::Player))))
            {
                OutFailureReason = TEXT("Bench contains an unsupported or unregistered live occupant.");
                return false;
            }
            AvailableSlots.Add(Slot);
        }
    }

    int32 UnassignedCount = 0;
    for (ABenchTile* Slot : OutAssignedSlots) if (!Slot) ++UnassignedCount;
    if (UnassignedCount > AvailableSlots.Num())
    {
        OutFailureReason = TEXT("Bench capacity exceeded. No positions or occupancy were changed.");
        return false;
    }
    for (int32 Index = 0; Index < OutWaitingUnits.Num(); ++Index)
    {
        if (OutAssignedSlots[Index]) continue;
        int32 BestIndex = INDEX_NONE;
        float BestDistance = TNumericLimits<float>::Max();
        for (int32 SlotIndex = 0; SlotIndex < AvailableSlots.Num(); ++SlotIndex)
        {
            const float Distance = FVector::Dist2D(
                OutWaitingUnits[Index]->GetActorLocation(), AvailableSlots[SlotIndex]->GetActorLocation());
            if (FMath::IsFinite(Distance) && Distance < BestDistance)
            {
                BestDistance = Distance;
                BestIndex = SlotIndex;
            }
        }
        if (BestIndex == INDEX_NONE)
        {
            OutFailureReason = TEXT("Cannot calculate a valid waiting position.");
            return false;
        }
        OutAssignedSlots[Index] = AvailableSlots[BestIndex];
        AvailableSlots.RemoveAt(BestIndex);
    }
    return true;
}

bool ABoardManager::CanAcceptWaitingUnits(int32 AdditionalUnitCount, FString& OutFailureReason) const
{
    OutFailureReason.Reset();
    if (AdditionalUnitCount < 0)
    {
        OutFailureReason = TEXT("Additional waiting unit count must be nonnegative.");
        return false;
    }
    TArray<ABattleUnitBase*> WaitingUnits;
    TArray<ABenchTile*> AssignedSlots;
    if (!BuildBenchSynchronizationPlan(WaitingUnits, AssignedSlots, OutFailureReason)) return false;
    if (AdditionalUnitCount > BenchSlots.Num() - WaitingUnits.Num())
    {
        OutFailureReason = TEXT("Not enough bench space for the additional waiting units.");
        return false;
    }
    return true;
}

ABenchTile* ABoardManager::GetFreeBenchSlot() const
{
    TArray<ABattleUnitBase*> WaitingUnits;
    TArray<ABenchTile*> AssignedSlots;
    FString Reason;
    if (!BuildBenchSynchronizationPlan(WaitingUnits, AssignedSlots, Reason)) return nullptr;
    for (ABenchTile* Slot : BenchSlots)
        if (Slot->CanPlaceUnit() && !AssignedSlots.Contains(Slot)) return Slot;
    return nullptr;
}

void ABoardManager::SynchronizeBenchOccupancy()
{
    FString Reason;
    if (!TrySynchronizeBenchOccupancy(Reason))
    {
        if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
            Mode->StatusMessage = Reason;
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: %s"), *Reason);
    }
}

// [?åÏú†Í∂?Í≤ΩÍ≥Ñ] ???®Ïàò??Î≥µÍµ¨ Î≤îÏúÑ???ÄÍ∏∞ÏÑù ?êÏú†?Ä ?¥Îèô ?ÑÏπò?¥Îã§. ?†Îãõ ?±Î°ù¬∑Í≥®Îìú¬∑?ÑÏù¥??Î≥ÄÍ≤ΩÏ? ?∏Ï∂ú?êÍ? ?òÌñâ/Ï∑®ÏÜå?úÎã§.
bool ABoardManager::TrySynchronizeBenchOccupancy(FString& OutFailureReason)
{
    TArray<ABattleUnitBase*> WaitingUnits;
    TArray<ABenchTile*> AssignedSlots;
    if (!BuildBenchSynchronizationPlan(WaitingUnits, AssignedSlots, OutFailureReason))
    {
        LastBenchFailureReason = OutFailureReason;
        return false;
    }

    struct FBenchSnapshot
    {
        ABenchTile* Slot;
        AActor* Occupant;
        bool bOccupied;
    };
    TArray<FBenchSnapshot> SavedSlots;
    TArray<int32> NewAssignments;
    TArray<FTransform> SavedTransforms;
    for (ABenchTile* Slot : BenchSlots)
        SavedSlots.Add(FBenchSnapshot{Slot, Slot->OccupiedUnit.Get(), Slot->bOccupied});
    for (int32 Index = 0; Index < WaitingUnits.Num(); ++Index)
    {
        if (AssignedSlots[Index]->OccupiedUnit == WaitingUnits[Index] && AssignedSlots[Index]->bOccupied) continue;
        NewAssignments.Add(Index);
        SavedTransforms.Add(WaitingUnits[Index]->GetActorTransform());
    }

    TGuardValue<bool> PlacementGuard(bPlacementInProgress, true);
    // Preserve established occupants (including a unit being dragged); only new assignments are snapped.
    for (ABenchTile* Slot : BenchSlots)
        if (!AssignedSlots.Contains(Slot)) Slot->ClearTile();
    for (int32 Index : NewAssignments) AssignedSlots[Index]->ClearTile();
    bool bApplied = true;
    for (int32 Index : NewAssignments)
    {
        if (!ApplyUnitPlacement(WaitingUnits[Index], nullptr, AssignedSlots[Index]))
        {
            bApplied = false;
            break;
        }
    }
    if (bApplied)
    {
        FString Reason;
        FUnitPlacementSnapshot Applied;
        for (int32 Index = 0; Index < WaitingUnits.Num(); ++Index)
        {
            if (!CaptureUnitPlacement(WaitingUnits[Index], Applied, Reason)
                || Applied.BenchSlot != AssignedSlots[Index] || Applied.Tile != nullptr)
            {
                bApplied = false;
                break;
            }
        }
    }
    if (bApplied)
    {
        // Later movement callbacks may change an earlier assignment's actor position.
        for (int32 Index : NewAssignments)
        {
            ABattleUnitBase* Unit = WaitingUnits[Index];
            ABenchTile* Slot = AssignedSlots[Index];
            if (!IsValid(Unit) || !IsValid(Slot)) { bApplied = false; break; }
            const FVector Expected = GetUnitLocationOnSurface(Unit, Slot->GetSurfaceLocation());
            if (!Unit->GetActorLocation().Equals(Expected, 0.1f)) { bApplied = false; break; }
        }
    }
    if (bApplied)
    {
        LastBenchFailureReason.Reset();
        OutFailureReason.Reset();
        return true;
    }

    bool bRestored = true;
    for (const FBenchSnapshot& Saved : SavedSlots)
    {
        if (!IsValid(Saved.Slot)) { bRestored = false; continue; }
        const int32 AssignmentIndex = AssignedSlots.IndexOfByKey(Saved.Slot);
        AActor* ExpectedOccupant = AssignmentIndex == INDEX_NONE ? nullptr : WaitingUnits[AssignmentIndex];
        if (IsValid(Saved.Slot->OccupiedUnit) && Saved.Slot->OccupiedUnit != Saved.Occupant
            && Saved.Slot->OccupiedUnit != ExpectedOccupant)
        {
            // An external callback owns this new link; report incomplete rollback instead of erasing it.
            bRestored = false;
            continue;
        }
        Saved.Slot->OccupiedUnit = Saved.Occupant;
        Saved.Slot->bOccupied = Saved.bOccupied;
    }
    for (int32 Offset = 0; Offset < NewAssignments.Num(); ++Offset)
    {
        ABattleUnitBase* Unit = WaitingUnits[NewAssignments[Offset]];
        if (!IsValid(Unit) || Unit->bInCombat || Unit->bIsDeployed || Unit->CurrentTile != nullptr)
        {
            bRestored = false;
            continue;
        }
        bRestored = Unit->SetActorTransform(SavedTransforms[Offset], false, nullptr,
            ETeleportType::TeleportPhysics) && bRestored;
    }
    for (const FBenchSnapshot& Saved : SavedSlots)
        if (!IsValid(Saved.Slot) || Saved.Slot->OccupiedUnit != Saved.Occupant
            || Saved.Slot->bOccupied != Saved.bOccupied) bRestored = false;
    for (int32 Offset = 0; Offset < NewAssignments.Num(); ++Offset)
    {
        ABattleUnitBase* Unit = WaitingUnits[NewAssignments[Offset]];
        if (!IsValid(Unit) || Unit->bInCombat || Unit->bIsDeployed || Unit->CurrentTile != nullptr
            || !Unit->GetActorTransform().Equals(SavedTransforms[Offset], 0.1f)) bRestored = false;
    }
    OutFailureReason = bRestored
        ? TEXT("Bench synchronization failed. Original bench assignments and positions were restored.")
        : TEXT("Bench synchronization failed and rollback could not complete. Inspect affected units and slots.");
    LastBenchFailureReason = OutFailureReason;
    if (!bRestored) UE_LOG(LogTemp, Error, TEXT("BoardManager: %s"), *OutFailureReason);
    return false;
}

bool ABoardManager::HasSavedPlayerFormation() const
{
    return bHasSavedPlayerFormation;
}

bool ABoardManager::RejectFormation(const FString& Reason)
{
    LastFormationFailureReason = Reason;
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
        Mode->StatusMessage = Reason;
    UE_LOG(LogTemp, Warning, TEXT("BoardManager: %s"), *Reason);
    return false;
}

// [ÏßÑÌñâ/?±Ïû• Í≤ΩÍ≥Ñ] ?Ä?•Î≥∏?Ä ?ÑÏû¨ ?°ÌÑ∞?Ä ?Ä??Ï∞∏Ï°∞?¥Îã§. ?Ä?????†Îãõ ÍµêÏ≤¥/?©ÏÑ±/?åÍ¥¥Í∞Ä ?ÑÏöî?òÎ©¥ Î≥µÍ? ?úÏÑú?Ä Ï∞∏Ï°∞ Í∞±Ïã† Í≥ÑÏïΩ??Î®ºÏ? ?©Ïùò?úÎã§.
bool ABoardManager::SavePlayerFormation()
{
    if (bPlacementInProgress || bBoardGenerationInProgress)
        return RejectFormation(TEXT("Cannot save formation during a board update."));
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(Mode) || !Mode->bEncounterReady)
        return RejectFormation(TEXT("Formation save requires a ready encounter."));
    if (Mode->CurrentPhase != EGamePhase::Preparation && Mode->CurrentPhase != EGamePhase::RoundClear)
        return RejectFormation(TEXT("Formation can only be saved before combat."));

    TArray<TObjectPtr<ABattleUnitBase>> NewUnits;
    TArray<TObjectPtr<ABoardTile>> NewTiles;
    TSet<ABoardTile*> FormationTiles;
    for (ABattleUnitBase* Unit : Mode->BattleUnits)
    {
        if (!IsValid(Unit) || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive() || !Unit->bIsDeployed)
            continue;
        FString Reason;
        if (!ValidateUnitPlacementState(Unit, Reason))
            return RejectFormation(FString::Printf(TEXT("Cannot save formation for %s: %s"), *GetNameSafe(Unit), *Reason));
        ABoardTile* Tile = Unit->CurrentTile;
        if (FormationTiles.Contains(Tile))
            return RejectFormation(TEXT("Cannot save formation with duplicate deployed units or tiles."));
        FormationTiles.Add(Tile);
        NewUnits.Add(Unit);
        NewTiles.Add(Tile);
    }
    // Commit only a fully validated snapshot; a failed save keeps the previous one available.
    SavedDeployedUnits = MoveTemp(NewUnits);
    SavedDeployedTiles = MoveTemp(NewTiles);
    bHasSavedPlayerFormation = true;
    bFormationRestoreCompleted = false;
    LastFormationFailureReason.Reset();
    return true;
}

// [ÏßÑÌñâ ?∞Í≤∞ Í≤ΩÍ≥Ñ] ?¨Ìò∏Ï∂?Í∞Ä?•Ìïú Î≥µÍ? API. ?§Ìå® ??LastFormationFailureReason???ïÏù∏?òÍ≥†, Î∂ÄÎ∂?Î≥µÍµ¨ Î∂àÍ? Î©îÏãúÏßÄÎ•??ïÏÉÅ ?±Í≥µ?ºÎ°ú Ï∑®Í∏â?òÏ? ?äÎäî??
bool ABoardManager::RestorePlayerFormation()
{
    if (bPlacementInProgress || bBoardGenerationInProgress)
        return RejectFormation(TEXT("Cannot restore formation during a board update."));
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    const bool bResultReady = IsValid(Mode) && (Mode->bBattleResultPending
        || Mode->CurrentPhase == EGamePhase::RoundClear || Mode->CurrentPhase == EGamePhase::GameOver
        || Mode->CurrentPhase == EGamePhase::Victory);
    if (!bResultReady) return RejectFormation(TEXT("Formation restoration requires a battle result."));
    if (!bHasSavedPlayerFormation)
    {
        // RoundClear -> Victory may notify twice; a completed restoration is an idempotent no-op.
        if (bFormationRestoreCompleted) { LastFormationFailureReason.Reset(); return true; }
        return RejectFormation(TEXT("No saved formation exists. Save before starting combat."));
    }
    if (SavedDeployedUnits.Num() != SavedDeployedTiles.Num())
        return RejectFormation(TEXT("Saved formation unit/tile counts do not match."));

    TSet<ABoardTile*> TargetTiles;
    TSet<ABoardTile*> TouchedTiles;
    TSet<ABattleUnitBase*> FormationUnits;
    TArray<FUnitPlacementSnapshot> BeforeUnits;
    for (int32 Index = 0; Index < SavedDeployedUnits.Num(); ++Index)
    {
        ABattleUnitBase* Unit = SavedDeployedUnits[Index];
        ABoardTile* Target = SavedDeployedTiles[Index];
        if (!IsValid(Unit) || !Mode->BattleUnits.Contains(Unit) || Unit->GetWorld() != GetWorld()
            || Unit->TeamType != EBattleTeam::Player || Unit->bInCombat || !Unit->bIsDeployed
            || !IsValid(Target) || !Tiles.Contains(Target) || Target->TeamType != EBoardTileTeam::Player
            || !Target->HasConsistentOccupancy() || FormationUnits.Contains(Unit) || TargetTiles.Contains(Target))
            return RejectFormation(FString::Printf(TEXT("Invalid saved formation entry %d (%s). No positions were changed."),
                Index, *GetNameSafe(Unit)));
        FormationUnits.Add(Unit);
        TargetTiles.Add(Target);
        TouchedTiles.Add(Target);

        int32 CurrentTileCount = 0;
        for (ABoardTile* Tile : Tiles)
        {
            if (!IsValid(Tile) || Tile->OccupiedUnit != Unit) continue;
            if (!Tile->HasConsistentOccupancy() || Tile != Unit->CurrentTile)
                return RejectFormation(TEXT("Formation unit has inconsistent current tile links. No positions were changed."));
            ++CurrentTileCount;
        }
        if ((Unit->CurrentTile != nullptr && (!IsValid(Unit->CurrentTile) || CurrentTileCount != 1))
            || (Unit->CurrentTile == nullptr && CurrentTileCount != 0))
            return RejectFormation(TEXT("Formation unit has an invalid current tile. No positions were changed."));
        for (ABenchTile* Slot : BenchSlots)
            if (IsValid(Slot) && Slot->OccupiedUnit == Unit)
                return RejectFormation(TEXT("A saved deployed unit also occupies the bench. No positions were changed."));

        ABattleUnitBase* Occupant = Cast<ABattleUnitBase>(Target->OccupiedUnit);
        if (IsValid(Target->OccupiedUnit) && (!IsValid(Occupant) || !SavedDeployedUnits.Contains(Occupant)))
            return RejectFormation(TEXT("A saved target tile is occupied by a unit outside the formation. No positions were changed."));
        FUnitPlacementSnapshot Before;
        Before.Unit = Unit;
        Before.Tile = Unit->CurrentTile;
        Before.Transform = Unit->GetActorTransform();
        Before.bWasDeployed = Unit->bIsDeployed;
        BeforeUnits.Add(Before);
        if (Before.Tile) TouchedTiles.Add(Before.Tile);
    }

    struct FTileSnapshot
    {
        ABoardTile* Tile;
        AActor* Occupant;
        bool bOccupied;
    };
    TArray<FTileSnapshot> BeforeTiles;
    for (ABoardTile* Tile : TouchedTiles)
        BeforeTiles.Add(FTileSnapshot{Tile, Tile->OccupiedUnit, Tile->bOccupied});

    TGuardValue<bool> PlacementGuard(bPlacementInProgress, true);
    for (ABoardTile* Tile : TouchedTiles) Tile->ClearTile();
    bool bApplied = true;
    for (int32 Index = 0; Index < BeforeUnits.Num(); ++Index)
    {
        ABattleUnitBase* Unit = BeforeUnits[Index].Unit;
        // Preserve the existing death policy: restore surviving units without reviving fallen units.
        if (!IsValid(Unit)) { bApplied = false; break; }
        if (!Unit->IsAlive()) continue;
        if (!ApplyUnitPlacement(Unit, SavedDeployedTiles[Index], nullptr)) { bApplied = false; break; }
    }
    if (bApplied)
    {
        FUnitPlacementSnapshot Applied;
        FString Reason;
        for (int32 Index = 0; Index < BeforeUnits.Num(); ++Index)
        {
            ABattleUnitBase* Unit = BeforeUnits[Index].Unit;
            ABoardTile* Target = SavedDeployedTiles[Index];
            if (!IsValid(Unit) || !IsValid(Target)) { bApplied = false; break; }
            if (!Unit->IsAlive()) continue;
            if (!CaptureUnitPlacement(Unit, Applied, Reason) || Applied.Tile != Target
                || !Unit->GetActorLocation().Equals(GetUnitLocationOnSurface(Unit, Target->GetSurfaceLocation()), 0.1f))
            {
                bApplied = false;
                break;
            }
        }
    }
    if (bApplied)
    {
        SavedDeployedUnits.Reset();
        SavedDeployedTiles.Reset();
        bHasSavedPlayerFormation = false;
        bFormationRestoreCompleted = true;
        LastFormationFailureReason.Reset();
        return true;
    }

    // Keep the saved formation for diagnosis/retry, and roll back to the result-stage state.
    bool bRestored = true;
    for (const FTileSnapshot& Before : BeforeTiles)
    {
        if (!IsValid(Before.Tile)) { bRestored = false; continue; }
        ABattleUnitBase* Occupant = Cast<ABattleUnitBase>(Before.Tile->OccupiedUnit);
        if (IsValid(Before.Tile->OccupiedUnit) && !FormationUnits.Contains(Occupant))
        {
            bRestored = false;
            continue;
        }
        Before.Tile->ClearTile();
        Before.Tile->OccupiedUnit = Before.Occupant;
        Before.Tile->bOccupied = Before.bOccupied;
    }
    for (const FUnitPlacementSnapshot& Before : BeforeUnits)
    {
        if (!IsValid(Before.Unit) || Before.Unit->bInCombat) { bRestored = false; continue; }
        Before.Unit->CurrentTile = Before.Tile;
        Before.Unit->bIsDeployed = Before.bWasDeployed;
        bRestored = Before.Unit->SetActorTransform(Before.Transform, false, nullptr,
            ETeleportType::TeleportPhysics) && bRestored;
    }
    for (const FTileSnapshot& Before : BeforeTiles)
        if (!IsValid(Before.Tile) || Before.Tile->OccupiedUnit != Before.Occupant
            || Before.Tile->bOccupied != Before.bOccupied) bRestored = false;
    for (const FUnitPlacementSnapshot& Before : BeforeUnits)
    {
        if (!IsValid(Before.Unit) || Before.Unit->bInCombat || Before.Unit->CurrentTile != Before.Tile
            || Before.Unit->bIsDeployed != Before.bWasDeployed
            || !Before.Unit->GetActorTransform().Equals(Before.Transform, 0.1f)) bRestored = false;
        int32 TileCount = 0;
        for (ABoardTile* Tile : Tiles)
        {
            if (!IsValid(Tile) || Tile->OccupiedUnit != Before.Unit) continue;
            ++TileCount;
            if (Tile != Before.Tile || !Tile->HasConsistentOccupancy()) bRestored = false;
        }
        if (TileCount != (Before.Tile ? 1 : 0)) bRestored = false;
        for (ABenchTile* Slot : BenchSlots)
            if (IsValid(Slot) && Slot->OccupiedUnit == Before.Unit) bRestored = false;
    }
    return RejectFormation(bRestored
        ? TEXT("Formation restoration failed. Result-stage positions were restored; the saved formation is retained.")
        : TEXT("Formation restoration failed and rollback could not complete. Inspect affected units/tiles; the saved formation is retained."));
}

bool ABoardManager::IsUnitMovementPhaseActive(const AAutoBattleGameMode* Mode) const
{
    return IsPlacementEnabled() && IsValid(Mode) && Mode->bEncounterReady
        && (Mode->CurrentPhase == EGamePhase::Preparation
            ? Mode->bPreparationActive
            : Mode->CurrentPhase == EGamePhase::RoundClear);
}

void ABoardManager::SetPlacementEnabled(bool bEnabled)
{
    if (bPlacementEnabled == bEnabled) return;
    bPlacementEnabled = bEnabled;
    if (!bEnabled) CancelPlayerInteractions();
}

void ABoardManager::SetInteractionLocked(FName LockReason, bool bLocked)
{
    if (bLocked)
    {
        const bool bAlreadyLocked = InteractionLockReasons.Contains(LockReason);
        InteractionLockReasons.Add(LockReason);
        if (!bAlreadyLocked) CancelPlayerInteractions();
    }
    else InteractionLockReasons.Remove(LockReason);
}

bool ABoardManager::IsPlacementEnabled() const
{
    return bPlacementEnabled && InteractionLockReasons.IsEmpty()
        && !bPlacementInProgress && !bBoardGenerationInProgress;
}

bool ABoardManager::IsPlacementPhaseActive() const
{
    return IsUnitMovementPhaseActive(Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)));
}

bool ABoardManager::CanProcessWorldInput() const
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    return IsPlacementEnabled() && IsValid(Mode) && Mode->bEncounterReady
        && (IsUnitMovementPhaseActive(Mode)
            || (Mode->CurrentPhase == EGamePhase::Combat && !Mode->bBattleResultPending));
}

bool ABoardManager::CanInteractWithUnit(ABattleUnitBase* Unit) const
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!CanProcessWorldInput() || !IsValid(Unit) || !Mode->BattleUnits.Contains(Unit)
        || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive() || Unit->bInCombat) return false;
    return IsUnitMovementPhaseActive(Mode) || (!Unit->bIsDeployed && Unit->CurrentTile == nullptr);
}

void ABoardManager::CancelPlayerInteractions()
{
    if (!GetWorld()) return;
    TArray<TWeakObjectPtr<AAutoBattlePlayerController>> Players;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        AAutoBattlePlayerController* PC = Cast<AAutoBattlePlayerController>(It->Get());
        if (IsValid(PC)) Players.Add(PC);
    }
    // Selection/drag listeners can create or remove controllers; do not iterate the world list during callbacks.
    for (const TWeakObjectPtr<AAutoBattlePlayerController>& Player : Players)
    {
        AAutoBattlePlayerController* PC = Player.Get();
        if (!IsValid(PC)) continue;
        PC->HoveredTile = nullptr;
        PC->HoveredBenchSlot = nullptr;
        PC->ClearSelection();
    }
}

bool ABoardManager::CaptureUnitPlacement(ABattleUnitBase* Unit, FUnitPlacementSnapshot& OutPlacement,
    FString& OutFailureReason) const
{
    OutPlacement = FUnitPlacementSnapshot();
    OutFailureReason.Reset();
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(Mode) || !IsValid(Unit) || Unit->GetWorld() != GetWorld()
        || !Mode->BattleUnits.Contains(Unit) || Unit->TeamType != EBattleTeam::Player
        || !Unit->IsAlive() || Unit->bInCombat)
    {
        OutFailureReason = TEXT("Placement requires a registered, living, inactive player unit.");
        return false;
    }

    int32 TileCount = 0;
    int32 BenchCount = 0;
    for (ABoardTile* Tile : Tiles)
    {
        if (!IsValid(Tile) || Tile->OccupiedUnit != Unit) continue;
        if (!Tile->HasConsistentOccupancy())
        {
            OutFailureReason = TEXT("Unit tile occupancy flag is inconsistent.");
            return false;
        }
        ++TileCount;
        OutPlacement.Tile = Tile;
    }
    for (ABenchTile* Slot : BenchSlots)
    {
        if (!IsValid(Slot) || Slot->OccupiedUnit != Unit) continue;
        if (!Slot->HasConsistentOccupancy())
        {
            OutFailureReason = TEXT("Unit bench occupancy flag is inconsistent.");
            return false;
        }
        ++BenchCount;
        OutPlacement.BenchSlot = Slot;
    }

    if (Unit->bIsDeployed)
    {
        if (TileCount != 1 || BenchCount != 0 || Unit->CurrentTile != OutPlacement.Tile
            || OutPlacement.Tile->TeamType != EBoardTileTeam::Player)
        {
            OutFailureReason = TEXT("Deployed unit must occupy exactly its current player tile and no bench slot.");
            return false;
        }
    }
    else if (TileCount != 0 || BenchCount != 1 || Unit->CurrentTile != nullptr)
    {
        OutFailureReason = TEXT("Waiting unit must occupy exactly one bench slot and no board tile.");
        return false;
    }

    OutPlacement.Unit = Unit;
    OutPlacement.Transform = Unit->GetActorTransform();
    OutPlacement.PreparationTransform = Unit->PreparationTransform;
    OutPlacement.Placement = Unit->Placement;
    OutPlacement.bWasDeployed = Unit->bIsDeployed;
    return true;
}

bool ABoardManager::ValidateUnitPlacementState(ABattleUnitBase* Unit, FString& OutFailureReason) const
{
    if (bPlacementInProgress || bBoardGenerationInProgress)
    {
        OutFailureReason = TEXT("Placement state is being updated. Retry after the movement finishes.");
        return false;
    }
    FUnitPlacementSnapshot Placement;
    return CaptureUnitPlacement(Unit, Placement, OutFailureReason);
}

bool ABoardManager::RejectPlacement(const FString& Reason)
{
    LastPlacementFailureReason = Reason;
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
        Mode->StatusMessage = Reason;
    return false;
}

// [?ÑÌà¨ ?∞Í≥Ñ] Î≥¥Îìú???§Ï†ú ?°ÌÑ∞ Î∞∞Ïπò??BattleUnitBase.PlaceOnTile???ÑÏûÑ?úÎã§. Î∞∞Ïπò ?íÏù¥ Í∑úÏπô??Î≥ÄÍ≤ΩÌïòÎ©??úÎ©¥ ?ÑÏπò Î≥¥Ï†ï Í≥ÑÏïΩ???®Íªò Ï°∞Ï†ï?úÎã§.
bool ABoardManager::ApplyUnitPlacement(ABattleUnitBase* Unit, ABoardTile* Tile, ABenchTile* BenchSlot)
{
    if (!IsValid(Unit) || !Unit->IsAlive() || Unit->bInCombat) return false;
    if (IsValid(Unit->CurrentTile) && Unit->CurrentTile->OccupiedUnit != Unit) return false;
    if (IsValid(Tile))
    {
        // Keep the combat owner's public placement function responsible for board actor movement.
        if (!Unit->PlaceOnTile(Tile) || !IsValid(Unit) || !IsValid(Tile)) return false;
        const FVector Expected = GetUnitLocationOnSurface(Unit, Tile->GetSurfaceLocation());
        return Unit->GetActorLocation().Equals(Expected, 0.1f);
    }
    if (!IsValid(BenchSlot) || !BenchSlot->CanPlaceUnit() || IsValid(Unit->CurrentTile)) return false;
    const FVector WaitingLocation = GetUnitLocationOnSurface(Unit, BenchSlot->GetSurfaceLocation());
    if (!Unit->SetWaitingPlacement(WaitingLocation)) return false;
    BenchSlot->SetOccupiedUnit(Unit);
    if (BenchSlot->OccupiedUnit != Unit || !BenchSlot->HasConsistentOccupancy()) return false;
    const FVector Expected = GetUnitLocationOnSurface(Unit, BenchSlot->GetSurfaceLocation());
    return Unit->SetActorLocation(Expected,
        false, nullptr, ETeleportType::TeleportPhysics)
        && IsValid(Unit) && Unit->GetActorLocation().Equals(Expected, 0.1f);
}

// [?ëÏóÖ Ï£ºÏùò] Ï§ëÏ≤© BoardManager ?¥Îèô ?îÏ≤≠?Ä ÎßâÏ?Îß? ?∏Î???ÏßÅÏ†ë ?°ÌÑ∞ ?¥Îèô/?êÏú† Î≥ÄÍ≤ΩÍπåÏßÄ Ï∞®Îã®?òÏ????äÎäî??
// ???¥Îèô??ÏΩúÎ∞±??Î™®Îëê ?ùÎÇú ???êÏú† ?∞Í≤∞Í≥??§Ï†ú ?ÑÏπòÎ•??®Íªò ?ïÏù∏?òÍ≥†, Î∂àÏùºÏπòÌïòÎ©?Í∏∞Ï°¥ Î≥µÍµ¨ Í≤ΩÎ°úÎ•??¨Ïö©?úÎã§.
bool ABoardManager::MoveOrSwapUnits(const FUnitPlacementSnapshot& Source, ABoardTile* TargetTile,
    ABenchTile* TargetSlot, ABattleUnitBase* OtherUnit, FString& OutFailureReason)
{
    FUnitPlacementSnapshot Other;
    if (OtherUnit && !CaptureUnitPlacement(OtherUnit, Other, OutFailureReason)) return false;
    if (OtherUnit && (Other.Tile != TargetTile || Other.BenchSlot != TargetSlot))
    {
        OutFailureReason = TEXT("Swap target does not match the other unit's placement.");
        return false;
    }

    // Reject nested movement during actor/overlap callbacks. The guard ends before success events.
    TGuardValue<bool> PlacementGuard(bPlacementInProgress, true);
    const auto ClearSlots = [&]()
    {
        // Do not erase a third unit placed by an external callback during a failed move.
        const auto IsParticipant = [&](AActor* Occupant)
        {
            return !IsValid(Occupant) || Occupant == Source.Unit || Occupant == OtherUnit;
        };
        if (IsValid(Source.Tile) && IsParticipant(Source.Tile->OccupiedUnit)) Source.Tile->ClearTile();
        if (IsValid(Source.BenchSlot) && IsParticipant(Source.BenchSlot->OccupiedUnit)) Source.BenchSlot->ClearTile();
        if (IsValid(TargetTile) && IsParticipant(TargetTile->OccupiedUnit)) TargetTile->ClearTile();
        if (IsValid(TargetSlot) && IsParticipant(TargetSlot->OccupiedUnit)) TargetSlot->ClearTile();
    };
    ClearSlots();
    const bool bMoved = ApplyUnitPlacement(Source.Unit, TargetTile, TargetSlot)
        && (!OtherUnit || ApplyUnitPlacement(OtherUnit, Source.Tile, Source.BenchSlot));
    const auto IsAtExpectedLocation = [](const ABattleUnitBase* Unit, const ABoardTile* Tile,
        const ABenchTile* BenchSlot)
    {
        if (!IsValid(Unit)) return false;
        FVector Surface;
        if (IsValid(Tile)) Surface = Tile->GetSurfaceLocation();
        else if (IsValid(BenchSlot)) Surface = BenchSlot->GetSurfaceLocation();
        else return false;
        return Unit->GetActorLocation().Equals(GetUnitLocationOnSurface(Unit, Surface), 0.1f);
    };
    FUnitPlacementSnapshot Applied;
    if (bMoved && CaptureUnitPlacement(Source.Unit, Applied, OutFailureReason)
        && Applied.Tile == TargetTile && Applied.BenchSlot == TargetSlot
        && IsAtExpectedLocation(Source.Unit, TargetTile, TargetSlot)
        && (!OtherUnit || (CaptureUnitPlacement(OtherUnit, Applied, OutFailureReason)
            && Applied.Tile == Source.Tile && Applied.BenchSlot == Source.BenchSlot
            && IsAtExpectedLocation(OtherUnit, Source.Tile, Source.BenchSlot)))) return true;

    // Restore links/flags directly: retrying PlaceOnTile could fail for the same reason as the move.
    ClearSlots();
    const auto Restore = [this](const FUnitPlacementSnapshot& Saved)
    {
        if (!IsValid(Saved.Unit) || (Saved.Tile && !IsValid(Saved.Tile))
            || (Saved.BenchSlot && !IsValid(Saved.BenchSlot))) return false;
        if (IsValid(Saved.Unit->CurrentTile) && Saved.Unit->CurrentTile->OccupiedUnit != Saved.Unit) return false;
        // Restore logical placement as well as links and visual transform after a failed swap.
        if (!Saved.Unit->SetPreparationPlacement(Saved.PreparationTransform, Saved.Tile, Saved.Placement)) return false;
        if (Saved.BenchSlot) Saved.BenchSlot->SetOccupiedUnit(Saved.Unit);
        const bool bTransformRestored = Saved.Unit->SetActorTransform(
            Saved.Transform, false, nullptr, ETeleportType::TeleportPhysics);
        FString Reason;
        FUnitPlacementSnapshot Restored;
        return bTransformRestored && CaptureUnitPlacement(Saved.Unit, Restored, Reason);
    };
    bool bRestored = Restore(Source);
    if (OtherUnit) bRestored = Restore(Other) && bRestored;
    // Recheck both after all transform callbacks, rather than trusting each individual restore.
    bRestored = CaptureUnitPlacement(Source.Unit, Applied, OutFailureReason)
        && Applied.Tile == Source.Tile && Applied.BenchSlot == Source.BenchSlot
        && Source.Unit->GetActorTransform().Equals(Source.Transform, 0.1f) && bRestored;
    if (OtherUnit)
        bRestored = CaptureUnitPlacement(OtherUnit, Applied, OutFailureReason)
            && Applied.Tile == Other.Tile && Applied.BenchSlot == Other.BenchSlot
            && OtherUnit->GetActorTransform().Equals(Other.Transform, 0.1f) && bRestored;
    OutFailureReason = bRestored
        ? TEXT("Unit movement failed. Original placements were restored.")
        : TEXT("Unit movement failed and rollback could not complete. Inspect the affected units and slots.");
    if (!bRestored) UE_LOG(LogTemp, Error, TEXT("BoardManager: %s"), *OutFailureReason);
    return false;
}

bool ABoardManager::TryPlaceUnitOnTile(ABattleUnitBase* Unit, ABoardTile* TargetTile)
{
    if (!IsPlacementEnabled()) return RejectPlacement(TEXT("Placement is disabled, locked or being updated."));
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsUnitMovementPhaseActive(Mode) || !IsValid(TargetTile) || !Tiles.Contains(TargetTile)
        || TargetTile->TeamType != EBoardTileTeam::Player)
        return RejectPlacement(TEXT("Choose a valid player tile during a placement phase."));

    FUnitPlacementSnapshot Source;
    FString FailureReason;
    if (!CaptureUnitPlacement(Unit, Source, FailureReason)) return RejectPlacement(FailureReason);
    if (!TargetTile->HasConsistentOccupancy())
        return RejectPlacement(TEXT("Target tile occupancy is inconsistent. Positions were not changed."));
    if (TargetTile == Source.Tile)
    {
        LastPlacementFailureReason.Reset();
        return true;
    }

    ABattleUnitBase* OtherUnit = Cast<ABattleUnitBase>(TargetTile->OccupiedUnit);
    if (IsValid(TargetTile->OccupiedUnit) && !IsValid(OtherUnit))
        return RejectPlacement(TEXT("Target tile is occupied by an unsupported actor."));
    if (!Source.bWasDeployed && !OtherUnit && Mode->GetDeployedPlayerCount() >= Mode->MaxBoardUnits)
        return RejectPlacement(TEXT("Deployment limit reached. Move an existing unit instead."));
    if (!MoveOrSwapUnits(Source, TargetTile, nullptr, OtherUnit, FailureReason))
        return RejectPlacement(FailureReason);

    LastPlacementFailureReason.Reset();
    Mode->StatusMessage = TEXT("Unit placed. Select another unit or press Enter to start.");
    // Notify only after both units have consistent final state; listeners retain the existing contract.
    if (OtherUnit && Source.BenchSlot) OnUnitMovedToBench.Broadcast(OtherUnit);
    return true;
}

bool ABoardManager::TryMoveUnitToBench(ABattleUnitBase* Unit, ABenchTile* TargetSlot)
{
    if (!IsPlacementEnabled()) return RejectPlacement(TEXT("Placement is disabled, locked or being updated."));
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    const bool bCombatPhase = IsValid(Mode) && Mode->CurrentPhase == EGamePhase::Combat;
    const bool bPlacementPhase = IsUnitMovementPhaseActive(Mode);
    if (!IsValid(Mode) || !Mode->bEncounterReady || (!bPlacementPhase && !bCombatPhase)
        || (bCombatPhase && Mode->bBattleResultPending)
        || !IsValid(TargetSlot) || !BenchSlots.Contains(TargetSlot))
        return RejectPlacement(TEXT("Choose a valid bench slot during an allowed phase."));

    FUnitPlacementSnapshot Source;
    FString FailureReason;
    if (!CaptureUnitPlacement(Unit, Source, FailureReason)) return RejectPlacement(FailureReason);
    if (Source.bWasDeployed && bCombatPhase)
        return RejectPlacement(TEXT("Deployed allies cannot return to the bench during combat."));
    if (!TargetSlot->HasConsistentOccupancy())
        return RejectPlacement(TEXT("Bench slot occupancy is inconsistent. Positions were not changed."));
    if (TargetSlot == Source.BenchSlot)
    {
        LastPlacementFailureReason.Reset();
        return true;
    }

    ABattleUnitBase* OtherUnit = Cast<ABattleUnitBase>(TargetSlot->OccupiedUnit);
    if (IsValid(TargetSlot->OccupiedUnit) && !IsValid(OtherUnit))
        return RejectPlacement(TEXT("Target bench slot is occupied by an unsupported actor."));
    if (!MoveOrSwapUnits(Source, nullptr, TargetSlot, OtherUnit, FailureReason))
        return RejectPlacement(FailureReason);

    LastPlacementFailureReason.Reset();
    if (Source.bWasDeployed)
    {
        Mode->StatusMessage = TEXT("Unit returned to the bench.");
        OnUnitMovedToBench.Broadcast(Unit);
    }
    return true;
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
    // πÊ«‚ º¯º≠∏¶ ∞Ì¡§«ÿ ∞∞¿∫ ¡∂∞«ø°º≠ ∞∞¿∫ √÷¥‹ ∞Ê∑Œ∏¶ º±≈√«—¥Ÿ.
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
    // ∏µÁ ∞£º± ∫ÒøÎ¿Ã ∞∞¿∫ 30ƒ≠ ∫∏µÂ¿Ãπ«∑Œ BFS∑Œ √Ê∫–«œ¥Ÿ. √‚πﬂ ƒ≠¿∫ ¡°¿Ø«— √§ ≈Ωªˆ«—¥Ÿ.
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
        // ¿Ø¥÷¿ª π´Ω√«ÿµµ ∞Ê∑Œ∞° æ¯¿∏∏È ¡ˆ«¸ªÛ µµ¥ﬁ ∫“∞°, ∞Ê∑Œ∞° ¿÷¿∏∏È ¿œΩ√¿˚¿Œ »•¿‚¿Ã¥Ÿ.
        const bool bTerrainReachable = BuildPath(Unit, Target, true, Path);
        LastMovementMessage = bTerrainReachable ? TEXT("Path temporarily blocked by units or reservations.") : TEXT("No terrain path reaches attack range.");
        return bTerrainReachable ? EUnitPathResult::TemporarilyBlocked : EUnitPathResult::Unreachable;
    }
    if (Path.IsEmpty()) return EUnitPathResult::AlreadyInRange;
    ABoardTile* Destination = Path[0];
    if (!Destination->CanPlaceUnit()) return EUnitPathResult::TemporarilyBlocked;
    // ∞‘¿” Ω∫∑πµÂ «— «‘ºˆ æ»ø°º≠ ∞ÀªÁøÕ øπæ‡¿ª «‘≤≤ ≥°≥Ω¥Ÿ. ¡ﬂ∞£ø° BP/∫Òµø±‚ ƒ›πÈ¿ª »£√‚«œ¡ˆ æ ¥¬¥Ÿ.
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
    // ¡°¿Ø ±≥»Ø¿∫ ∫∏µÂ∞° «— π¯ø° »Æ¡§«—¥Ÿ. Unit¿∫ ≈∏¿œ ≥ª∫Œ ∞™¿ª ¡˜¡¢ ºˆ¡§«œ¡ˆ æ ¥¬¥Ÿ.
    Source->ClearOccupant(); Destination->AssignOccupant(Unit);
    return Unit->CurrentTile == Destination;
}
bool ABoardManager::CancelReservedMove(ABattleUnitBase* Unit, int32 MoveId)
{
    const FMoveReservation* Found = Reservations.Find(Unit);
    if (!Found || Found->MoveId != MoveId) return false;
    ABoardTile* Destination = Found->Destination.Get();
    Reservations.Remove(Unit); // ƒ›πÈ∫∏¥Ÿ ∏’¿˙ ¡¶∞≈«ÿ ¿Á¡¯¿‘/¥ ¿∫ √Îº“∞° ªı øπæ‡¿ª ¡ˆøÏ¡ˆ æ ∞‘ «—¥Ÿ.
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
    if (Reason == EEndPlayReason::Destroyed)
        for (ABenchTile* Slot : BenchSlots) if (IsValid(Slot)) Slot->Destroy();
    BenchSlots.Empty();
    Super::EndPlay(Reason);
}
