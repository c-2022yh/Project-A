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

// Drop callbacks see the final placement before receiving the drag-ended notification.
struct AAutoBattlePlayerController::FScopedDragNotifications
{
    explicit FScopedDragNotifications(AAutoBattlePlayerController& InOwner) : Owner(InOwner)
    {
        ++Owner.DragNotificationDepth;
    }
    ~FScopedDragNotifications()
    {
        if (--Owner.DragNotificationDepth == 0 && Owner.bDragNotificationPending)
        {
            ABattleUnitBase* Unit = Owner.DragNotificationUnit.Get();
            Owner.bDragNotificationPending = false;
            Owner.DragNotificationUnit.Reset();
            Owner.NotifyUnitDragState(Unit);
        }
    }
    AAutoBattlePlayerController& Owner;
};

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
    if (SelectedUnit == Unit) return;
    const uint32 PreviousRevision = SelectionRevision;
    CancelUnitDrag();
    if (SelectionRevision != PreviousRevision || IsUnitDragActive() || !IsUnitInteractionAllowed(Unit)) return;
    SetSelectedUnitInternal(Unit);
}

void AAutoBattlePlayerController::ClearSelection()
{
    const uint32 PreviousRevision = SelectionRevision;
    CancelUnitDrag();
    if (SelectionRevision == PreviousRevision && !IsUnitDragActive()) SetSelectedUnitInternal(nullptr);
}

void AAutoBattlePlayerController::SetSelectedUnitInternal(ABattleUnitBase* Unit)
{
    if (SelectedUnit == Unit) return;
    SelectedUnit = Unit;
    ++SelectionRevision;
    OnSelectedUnitChanged.Broadcast(Unit);
}

bool AAutoBattlePlayerController::IsUnitDragActive() const
{
    return bUnitDragPending || bIsUnitDragging;
}

// [협업 주의] 드롭 중 알림 지연은 최종 배치를 보여 주기 위한 것이다.
// TODO: Broadcast 중 선택/잠금/드래그를 다시 바꾸면 다른 수신자에게 과거 알림이 뒤늦게 전달될 수 있다.
// 현재 수신부는 상태 표시 위주로 사용하고 보드/입력 상태 변경은 별도 흐름으로 분리한다.
void AAutoBattlePlayerController::NotifyUnitDragState(ABattleUnitBase* Unit)
{
    if (DragNotificationDepth > 0)
    {
        bDragNotificationPending = true;
        DragNotificationUnit = Unit;
        return;
    }
    OnUnitDragStateChanged.Broadcast(IsValid(Unit) ? Unit : nullptr, bUnitDragPending, bIsUnitDragging);
}

bool AAutoBattlePlayerController::BeginUnitDrag(ABattleUnitBase* Unit)
{
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (IsPointerOverHUD() || !GetMousePosition(MouseX, MouseY) || !IsUnitInteractionAllowed(Unit)) return false;

    ABoardManager* Board = GetBoardManager();
    FString FailureReason;
    if (!IsValid(Board) || !Board->ValidateUnitPlacementState(Unit, FailureReason)) return false;

    const uint32 PreviousSelectionRevision = SelectionRevision;
    CancelUnitDrag();
    if (SelectionRevision != PreviousSelectionRevision) return false;
    if (IsUnitDragActive()) return false;
    SelectUnit(Unit);
    if (SelectedUnit != Unit || !IsUnitInteractionAllowed(Unit) || IsUnitDragActive()
        || !IsValid(Board) || !Board->ValidateUnitPlacementState(Unit, FailureReason)) return false;

    DraggedUnit = SelectedUnit;
    UnitDragStartTile = SelectedUnit->CurrentTile;
    for (ABenchTile* Slot : Board->BenchSlots)
        if (IsValid(Slot) && Slot->OccupiedUnit == SelectedUnit) UnitDragStartBenchSlot = Slot;
    UnitDragStartScreenPosition = FVector2D(MouseX, MouseY);
    UnitDragStartTransform = SelectedUnit->GetActorTransform();
    UnitDragSurfaceZ = IsValid(SelectedUnit->CurrentTile)
        ? SelectedUnit->CurrentTile->GetSurfaceLocation().Z
        : SelectedUnit->GetActorLocation().Z - SelectedUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 1.0f;
    bUnitDragPending = true;
    bIsUnitDragging = false;
    const uint32 Session = ++DragSessionRevision;
    NotifyUnitDragState(Unit);
    return DragSessionRevision == Session && bUnitDragPending && DraggedUnit.Get() == Unit;
}

bool AAutoBattlePlayerController::EndUnitDrag()
{
    if (!bUnitDragPending) return false;
    FScopedDragNotifications Notifications(*this);
    ABattleUnitBase* Unit = DraggedUnit.Get();
    const uint32 Session = DragSessionRevision;
    const bool bWasDragging = bIsUnitDragging;
    TWeakObjectPtr<AActor> DropSlot = bWasDragging && Unit == SelectedUnit && IsUnitInteractionAllowed(Unit)
        && IsDraggedUnitPlacementUnchanged()
        ? GetDragDropSlotAtCursor() : nullptr;

    // Restore before placement so BoardManager also snapshots the original transform for rollback.
    CancelUnitDrag();
    if (!bWasDragging || DragSessionRevision != Session + 1 || IsUnitDragActive()
        || !IsValid(Unit) || Unit != SelectedUnit || !IsUnitInteractionAllowed(Unit)) return false;

    if (ABoardTile* Tile = Cast<ABoardTile>(DropSlot.Get())) return PlaceSelectedUnitOnTile(Tile);
    if (ABenchTile* BenchSlot = Cast<ABenchTile>(DropSlot.Get())) return MoveSelectedUnitToBench(BenchSlot);
    return false;
}

void AAutoBattlePlayerController::CancelUnitDrag()
{
    ABattleUnitBase* Unit = DraggedUnit.Get();
    const bool bHadDrag = IsUnitDragActive();
    const bool bRestoreTransform = bHadDrag
        && IsDraggedUnitPlacementUnchanged();
    const FTransform StartTransform = UnitDragStartTransform;

    // Clear ownership before moving the actor; repeated cancellation is harmless.
    DraggedUnit.Reset();
    UnitDragStartTile.Reset();
    UnitDragStartBenchSlot.Reset();
    bUnitDragPending = false;
    bIsUnitDragging = false;
    UnitDragStartScreenPosition = FVector2D::ZeroVector;
    UnitDragStartTransform = FTransform::Identity;
    UnitDragSurfaceZ = 0.0f;
    const uint32 CancelRevision = bHadDrag ? ++DragSessionRevision : DragSessionRevision;

    if (bRestoreTransform && IsValid(Unit) && !Unit->bInCombat)
        Unit->SetActorTransform(StartTransform, false, nullptr, ETeleportType::TeleportPhysics);
    if (bHadDrag && DragSessionRevision == CancelRevision)
        NotifyUnitDragState(Unit);
}

bool AAutoBattlePlayerController::IsDraggedUnitPlacementUnchanged() const
{
    const ABattleUnitBase* Unit = DraggedUnit.Get();
    if (!IsValid(Unit) || !Unit->IsAlive() || Unit->bInCombat) return false;
    if (const ABoardTile* Tile = UnitDragStartTile.Get())
        return Unit->bIsDeployed && Unit->CurrentTile == Tile && Tile->bOccupied && Tile->OccupiedUnit == Unit;
    if (const ABenchTile* Slot = UnitDragStartBenchSlot.Get())
        return !Unit->bIsDeployed && !IsValid(Unit->CurrentTile) && Slot->bOccupied && Slot->OccupiedUnit == Unit;
    return false;
}

bool AAutoBattlePlayerController::PlaceSelectedUnitOnTile(ABoardTile* Tile)
{
    FScopedDragNotifications Notifications(*this);
    // Blueprint callers may place directly without first calling EndUnitDrag.
    ABattleUnitBase* Unit = SelectedUnit;
    const uint32 ExpectedRevision = DragSessionRevision + (IsUnitDragActive() ? 1u : 0u);
    CancelUnitDrag();
    if (DragSessionRevision != ExpectedRevision || IsUnitDragActive()
        || !IsValid(Unit) || SelectedUnit != Unit || !IsValid(Tile)) return false;
    ABoardManager* Board = GetBoardManager();
    if (!IsValid(Board) || !Board->TryPlaceUnitOnTile(Unit, Tile)) return false;
    // A successful placement event may select a different unit or begin a new drag.
    if (SelectedUnit == Unit && !bUnitDragPending) ClearSelection();
    return true;
}

bool AAutoBattlePlayerController::MoveSelectedUnitToBench(ABenchTile* Slot)
{
    FScopedDragNotifications Notifications(*this);
    ABattleUnitBase* Unit = SelectedUnit;
    const uint32 ExpectedRevision = DragSessionRevision + (IsUnitDragActive() ? 1u : 0u);
    CancelUnitDrag();
    if (DragSessionRevision != ExpectedRevision || IsUnitDragActive()
        || !IsValid(Unit) || SelectedUnit != Unit || !IsValid(Slot)) return false;
    ABoardManager* Board = GetBoardManager();
    if (!IsValid(Board) || !Board->TryMoveUnitToBench(Unit, Slot)) return false;
    if (SelectedUnit == Unit && !bUnitDragPending) ClearSelection();
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
    const ABoardManager* Board = GetBoardManager();
    if (!IsValid(Board) || !Board->CanProcessWorldInput() || IsPointerOverHUD()) return false;
    return GetHitResultUnderCursor(ECC_Visibility, true, Hit);
}

// [UI 연결 경계] Canvas HitBox만 확인한다. UMG 팝업을 포함한 전역 차단은 BoardManager의 사유별 잠금 호출이 필요하다.
bool AAutoBattlePlayerController::IsPointerOverHUD() const
{
    float X = 0.0f;
    float Y = 0.0f;
    if (!GetMousePosition(X, Y)) return false;
    return GetHUD() && GetHUD()->GetHitBoxAtCoordinates(FVector2D(X, Y), true) != nullptr;
}

bool AAutoBattlePlayerController::IsUnitPlacementPhaseActive() const
{
    const ABoardManager* Board = GetBoardManager();
    return IsValid(Board) && Board->IsPlacementPhaseActive();
}

bool AAutoBattlePlayerController::IsUnitInteractionAllowed(ABattleUnitBase* Unit) const
{
    const ABoardManager* Board = GetBoardManager();
    return IsValid(Board) && Board->CanInteractWithUnit(Unit);
}

AActor* AAutoBattlePlayerController::GetDragDropSlotAtCursor(FHitResult* OutHit) const
{
    if (OutHit) *OutHit = FHitResult();
    ABattleUnitBase* UnitBeingDragged = DraggedUnit.Get();
    if (IsPointerOverHUD() || !IsUnitInteractionAllowed(UnitBeingDragged)) return nullptr;

    FVector RayOrigin;
    FVector RayDirection;
    if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection)) return nullptr;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(UnitDragDrop), true);
    QueryParams.AddIgnoredActor(UnitBeingDragged);
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

// [전투/배치 경계] 여기의 액터 이동은 드래그 표현일 뿐이며 타일 점유를 바꾸지 않는다. 다른 파트는 이 월드 위치로 소속 칸을 추정하지 않는다.
void AAutoBattlePlayerController::UpdateDraggedUnitLocation()
{
    ABattleUnitBase* Unit = DraggedUnit.Get();
    if (!IsValid(Unit)) return;
    FVector RayOrigin;
    FVector RayDirection;
    if (!DeprojectMousePositionToWorld(RayOrigin, RayDirection) || FMath::IsNearlyZero(RayDirection.Z)) return;
    const float RayDistance = (UnitDragSurfaceZ - RayOrigin.Z) / RayDirection.Z;
    if (!FMath::IsFinite(RayDistance) || RayDistance < 0.0f) return;
    FVector NewLocation = RayOrigin + RayDirection * RayDistance;
    NewLocation.Z += Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 1.0f
        + FMath::Max(0.0f, UnitDragHeightOffset);
    Unit->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

void AAutoBattlePlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    const ABoardManager* Board = GetBoardManager();
    HoveredTile = nullptr;
    HoveredBenchSlot = nullptr;
    if (!IsValid(Board) || !Board->CanProcessWorldInput())
    {
        ClearSelection();
        return;
    }

    if (SelectedUnit != nullptr && (!IsValid(SelectedUnit) || !IsUnitInteractionAllowed(SelectedUnit))) ClearSelection();

    if (bUnitDragPending)
    {
        const uint32 Session = DragSessionRevision;
        float MouseX = 0.0f;
        float MouseY = 0.0f;
        if (DraggedUnit.Get() != SelectedUnit || !IsUnitInteractionAllowed(DraggedUnit.Get())
            || !IsDraggedUnitPlacementUnchanged())
        {
            CancelUnitDrag();
        }
        else if (GetMousePosition(MouseX, MouseY)
            && (FVector2D(MouseX, MouseY) - UnitDragStartScreenPosition).Size() >= FMath::Max(0.0f, UnitDragThreshold))
        {
            if (!bIsUnitDragging)
            {
                bIsUnitDragging = true;
                NotifyUnitDragState(DraggedUnit.Get());
            }
        }
        if (DragSessionRevision != Session) return;
        if (bIsUnitDragging) UpdateDraggedUnitLocation();
    }

    // Listeners may have changed popup locks or the game phase during this tick.
    if (!IsValid(Board) || !Board->CanProcessWorldInput()) return;
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

// [진행 연결 경계] 시작 요청 시점의 저장이다. BP가 실제 시작을 지연하며 배치를 바꾼다면 실제 시작 직전 저장 계약을 별도로 맞춘다.
void AAutoBattlePlayerController::RequestStartCombat()
{
    const ABoardManager* Board = GetBoardManager();
    if (IsUnitDragActive() || DragNotificationDepth > 0 || !IsValid(Board) || !Board->IsPlacementEnabled()) return;
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        if (IsUnitPlacementPhaseActive())
        {
            if (!IsValid(Mode->BoardManager) || !Mode->BoardManager->SavePlayerFormation()) return;
        }
        Mode->RequestCombatStart();
    }
}

void AAutoBattlePlayerController::RequestRestart()
{
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this)))
        Mode->RequestBattleRestart();
}
