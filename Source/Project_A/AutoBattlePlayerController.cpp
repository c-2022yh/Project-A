#include "AutoBattlePlayerController.h"
#include "AutoBattleGameMode.h"
#include "BattleUnitBase.h"
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
    if (!Mode || !IsUnitPlacementPhaseActive() || !IsValid(Unit)
        || !Mode->BattleUnits.Contains(Unit) || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive())
    { ClearSelection(); return; }
    SelectedUnit = Unit;
}
void AAutoBattlePlayerController::ClearSelection()
{
    CancelUnitDrag();
    SelectedUnit = nullptr;
}

bool AAutoBattlePlayerController::BeginUnitDrag(ABattleUnitBase* Unit)
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (!Mode || !IsUnitPlacementPhaseActive()
        || IsPointerOverHUD() || !GetMousePosition(MouseX, MouseY))
    {
        return false;
    }

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
    if (!IsValid(SelectedUnit)) return false;

    // A press and release without meaningful movement remains a normal selection click.
    if (!bWasDragging) return false;

    ABoardTile* DropTile = GetDragDropTileAtCursor();
    SelectedUnit->SetActorTransform(UnitDragStartTransform, false, nullptr, ETeleportType::TeleportPhysics);
    if (!IsValid(DropTile)) return false;
    return PlaceSelectedUnitOnTile(DropTile);
}

void AAutoBattlePlayerController::CancelUnitDrag()
{
    if ((bUnitDragPending || bIsUnitDragging) && IsValid(SelectedUnit))
    {
        SelectedUnit->SetActorTransform(UnitDragStartTransform, false, nullptr, ETeleportType::TeleportPhysics);
    }
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

ABoardManager* AAutoBattlePlayerController::GetBoardManager() const
{
	const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
	return IsValid(Mode) ? Mode->BoardManager.Get() : nullptr;
}
bool AAutoBattlePlayerController::GetBoardClickHit(FHitResult& Hit)
{
    Hit = FHitResult();
    if (IsPointerOverHUD()) return false;
    // Preserve complex tracing for the custom hex tile mesh.
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
    // 보상 선택 페이즈가 아직 존재하지 않아, RoundClear를 유닛 이동 가능 페이즈로 임시 지정. 추후 변경 가능
    return Mode->CurrentPhase == EGamePhase::RoundClear;
}

ABoardTile* AAutoBattlePlayerController::GetDragDropTileAtCursor(FHitResult* OutHit) const
{
    if (OutHit) *OutHit = FHitResult();
    if (IsPointerOverHUD() || !IsValid(SelectedUnit)) return nullptr;

    FVector RayOrigin;
    FVector RayDirection;
    if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection)) return nullptr;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UnitDragDrop), true);
    QueryParams.AddIgnoredActor(SelectedUnit);
    if (const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        // This trace answers only which board tile is under the cursor; units must not occlude that tile.
        for (ABattleUnitBase* Unit : Mode->BattleUnits)
        {
            if (IsValid(Unit)) QueryParams.AddIgnoredActor(Unit);
        }
    }
    FHitResult Hit;
    if (!GetWorld() || !GetWorld()->LineTraceSingleByChannel(
        Hit, RayOrigin, RayOrigin + RayDirection * 100000.0f, ECC_Visibility, QueryParams))
    {
        return nullptr;
    }

    if (OutHit) *OutHit = Hit;
    return Cast<ABoardTile>(Hit.GetActor());
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
    if (!Mode || !IsUnitPlacementPhaseActive())
    {
        ClearSelection();
        return;
    }

    if (bUnitDragPending)
    {
        float MouseX = 0.0f;
        float MouseY = 0.0f;
        if (!IsValid(SelectedUnit) || !SelectedUnit->IsAlive())
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
    if (bIsUnitDragging)
    {
        HoveredTile = GetDragDropTileAtCursor(&Hit);
    }
    else if (GetHitResultUnderCursor(ECC_Visibility, true, Hit))
    {
        HoveredTile = Cast<ABoardTile>(Hit.GetActor());
    }
    if (IsValid(SelectedUnit) && SelectedUnit->IsAlive())
    {
        DrawDebugCircle(GetWorld(), SelectedUnit->GetActorLocation(),
            SelectedUnit->GetCapsuleComponent()->GetScaledCapsuleRadius() * 1.4f,
            32, FColor::Yellow, false, -1, 0, 8, FVector::ForwardVector, FVector::RightVector, false);
    }
}
void AAutoBattlePlayerController::RequestStartCombat()
{
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        Mode->RequestCombatStart();
    }
}
void AAutoBattlePlayerController::RequestRestart()
{
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this))) Mode->RequestBattleRestart();
}
