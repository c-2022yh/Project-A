#include "AutoBattlePlayerController.h"

#include "AutoBattleGameMode.h"
#include "BattleUnitBase.h"
#include "BenchTile.h"
#include "BoardManager.h"
#include "BoardTile.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"

AAutoBattlePlayerController::AAutoBattlePlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
    bAutoManageActiveCameraTarget = false;
}

void AAutoBattlePlayerController::SelectUnit(ABattleUnitBase* Unit)
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode || !IsUnitInteractionAllowed(Unit) || !Mode->BattleUnits.Contains(Unit))
    {
        ClearSelection();
        return;
    }
    SelectedUnit = Unit;
}

void AAutoBattlePlayerController::ClearSelection()
{
    CancelUnitDrag();
    SelectedUnit = nullptr;
}

bool AAutoBattlePlayerController::BeginUnitDrag(ABattleUnitBase* Unit)
{
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (IsPointerOverHUD() || !GetMousePosition(MouseX, MouseY) || !IsUnitInteractionAllowed(Unit)) return false;

    CancelUnitDrag();
    SelectUnit(Unit);
    if (!IsValid(SelectedUnit)) return false;

    UnitDragStartScreenPosition = FVector2D(MouseX, MouseY);
    UnitDragStartTransform = SelectedUnit->GetActorTransform();
    UnitDragSurfaceZ = IsValid(SelectedUnit->CurrentTile)
        ? SelectedUnit->CurrentTile->GetSurfaceLocation().Z
        : SelectedUnit->GetActorLocation().Z - SelectedUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 1.0f;
    bUnitDragPending = true;
    bIsUnitDragging = false;
    return true;
}

bool AAutoBattlePlayerController::EndUnitDrag()
{
    if (!bUnitDragPending) return false;
    const bool bWasDragging = bIsUnitDragging;
    bUnitDragPending = false;
    bIsUnitDragging = false;
    if (!IsValid(SelectedUnit) || !bWasDragging) return false;

    AActor* DropSlot = GetDragDropSlotAtCursor();
    SelectedUnit->SetActorTransform(UnitDragStartTransform, false, nullptr, ETeleportType::TeleportPhysics);
    if (ABoardTile* Tile = Cast<ABoardTile>(DropSlot)) return PlaceSelectedUnitOnTile(Tile);
    if (ABenchTile* BenchSlot = Cast<ABenchTile>(DropSlot)) return MoveSelectedUnitToBench(BenchSlot);
    return false;
}

void AAutoBattlePlayerController::CancelUnitDrag()
{
    if ((bUnitDragPending || bIsUnitDragging) && IsValid(SelectedUnit))
        SelectedUnit->SetActorTransform(UnitDragStartTransform, false, nullptr, ETeleportType::TeleportPhysics);
    bUnitDragPending = false;
    bIsUnitDragging = false;
}

bool AAutoBattlePlayerController::PlaceSelectedUnitOnTile(ABoardTile* Tile)
{
    if (!IsValid(SelectedUnit) || !IsValid(Tile)) return false;
    ABoardManager* Board = GetBoardManager();
    if (!IsValid(Board) || !Board->TryPlaceUnitOnTile(SelectedUnit, Tile)) return false;
    ClearSelection();
    return true;
}

bool AAutoBattlePlayerController::MoveSelectedUnitToBench(ABenchTile* Slot)
{
    if (!IsValid(SelectedUnit) || !IsValid(Slot)) return false;
    ABoardManager* Board = GetBoardManager();
    if (!IsValid(Board) || !Board->TryMoveUnitToBench(SelectedUnit, Slot)) return false;
    ClearSelection();
    return true;
}

ABoardManager* AAutoBattlePlayerController::GetBoardManager() const
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    return IsValid(Mode) ? Mode->BoardManager.Get() : nullptr;
}

bool AAutoBattlePlayerController::GetBoardClickHit(FHitResult& Hit)
{
    Hit = FHitResult();
    if (IsPointerOverHUD()) return false;
    return GetHitResultUnderCursor(ECC_Visibility, true, Hit);
}

bool AAutoBattlePlayerController::IsPointerOverHUD() const
{
    float X = 0.0f;
    float Y = 0.0f;
    if (!GetMousePosition(X, Y)) return false;
    return GetHUD() && GetHUD()->GetHitBoxAtCoordinates(FVector2D(X, Y), true) != nullptr;
}

bool AAutoBattlePlayerController::IsUnitPlacementPhaseActive() const
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode) return false;
    if (Mode->CurrentPhase == EGamePhase::Preparation) return Mode->bPreparationActive;
    return Mode->CurrentPhase == EGamePhase::RoundClear;
}

bool AAutoBattlePlayerController::IsUnitInteractionAllowed(const ABattleUnitBase* Unit) const
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!IsValid(Mode) || !IsValid(Unit) || !Mode->BattleUnits.Contains(Unit)
        || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive()) return false;
    if (IsUnitPlacementPhaseActive()) return !Unit->bInCombat;
    return Mode->CurrentPhase == EGamePhase::Combat
        && !Mode->bBattleResultPending
        && !Unit->bInCombat && !Unit->bIsDeployed && !IsValid(Unit->CurrentTile);
}

AActor* AAutoBattlePlayerController::GetDragDropSlotAtCursor(FHitResult* OutHit) const
{
    if (OutHit) *OutHit = FHitResult();
    if (IsPointerOverHUD() || !IsValid(SelectedUnit)) return nullptr;

    FVector RayOrigin;
    FVector RayDirection;
    if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection)) return nullptr;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UnitDragDrop), true);
    QueryParams.AddIgnoredActor(SelectedUnit);
    if (const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
        for (ABattleUnitBase* Unit : Mode->BattleUnits)
            if (IsValid(Unit)) QueryParams.AddIgnoredActor(Unit);

    FHitResult Hit;
    if (!GetWorld() || !GetWorld()->LineTraceSingleByChannel(
        Hit, RayOrigin, RayOrigin + RayDirection * 100000.0f, ECC_Visibility, QueryParams)) return nullptr;
    if (OutHit) *OutHit = Hit;
    AActor* HitActor = Hit.GetActor();
    return Cast<ABoardTile>(HitActor) || Cast<ABenchTile>(HitActor) ? HitActor : nullptr;
}

void AAutoBattlePlayerController::UpdateDraggedUnitLocation()
{
    if (!IsValid(SelectedUnit)) return;
    FVector RayOrigin;
    FVector RayDirection;
    if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection) || FMath::IsNearlyZero(RayDirection.Z)) return;
    const float RayDistance = (UnitDragSurfaceZ - RayOrigin.Z) / RayDirection.Z;
    if (!FMath::IsFinite(RayDistance) || RayDistance < 0.0f) return;
    FVector NewLocation = RayOrigin + RayDirection * RayDistance;
    NewLocation.Z += SelectedUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f
        + FMath::Max(0.0f, UnitDragHeightOffset);
    SelectedUnit->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

void AAutoBattlePlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    HoveredTile = nullptr;
    HoveredBenchSlot = nullptr;
    const bool bCanInteractThisPhase = IsUnitPlacementPhaseActive()
        || (Mode && Mode->CurrentPhase == EGamePhase::Combat);
    if (!Mode || !bCanInteractThisPhase)
    {
        ClearSelection();
        return;
    }

    if (IsValid(SelectedUnit) && !IsUnitInteractionAllowed(SelectedUnit)) ClearSelection();
    if (IsValid(Mode->BoardManager)) Mode->BoardManager->SynchronizeBenchOccupancy();

    if (bUnitDragPending)
    {
        float MouseX = 0.0f;
        float MouseY = 0.0f;
        if (!IsUnitInteractionAllowed(SelectedUnit))
        {
            CancelUnitDrag();
        }
        else if (GetMousePosition(MouseX, MouseY)
            && (FVector2D(MouseX, MouseY) - UnitDragStartScreenPosition).Size() >= FMath::Max(0.0f, UnitDragThreshold))
        {
            bIsUnitDragging = true;
        }
        if (bIsUnitDragging) UpdateDraggedUnitLocation();
    }

    FHitResult Hit;
    AActor* SlotActor = nullptr;
    if (bIsUnitDragging) SlotActor = GetDragDropSlotAtCursor(&Hit);
    else if (!IsPointerOverHUD() && GetHitResultUnderCursor(ECC_Visibility, true, Hit)) SlotActor = Hit.GetActor();
    HoveredTile = Cast<ABoardTile>(SlotActor);
    HoveredBenchSlot = Cast<ABenchTile>(SlotActor);

    if (IsValid(SelectedUnit) && SelectedUnit->IsAlive())
        DrawDebugCircle(GetWorld(), SelectedUnit->GetActorLocation(),
            SelectedUnit->GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.4f,
            32, FColor::Yellow, false, -1, 0, 8, FVector::ForwardVector, FVector::RightVector, false);
}

void AAutoBattlePlayerController::RequestStartCombat()
{
    if (bUnitDragPending || bIsUnitDragging) return;
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        if (IsUnitPlacementPhaseActive() && IsValid(Mode->BoardManager))
            Mode->BoardManager->SavePlayerFormation();
        Mode->RequestCombatStart();
    }
}

void AAutoBattlePlayerController::RequestRestart()
{
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
        Mode->RequestBattleRestart();
}
