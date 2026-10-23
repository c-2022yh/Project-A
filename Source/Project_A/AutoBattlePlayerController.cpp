#include "AutoBattlePlayerController.h"
#include "AutoBattleGameMode.h"
#include "BattleUnitBase.h"
#include "BoardTile.h"
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

AAutoBattlePlayerController::AAutoBattlePlayerController()
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
    bAutoManageActiveCameraTarget = false;
}
void AAutoBattlePlayerController::BeginPlay()
{
    Super::BeginPlay();
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    TArray<AActor*> Cameras;
    UGameplayStatics::GetAllActorsOfClass(this, ACameraActor::StaticClass(), Cameras);
    AActor* Camera = Cameras.IsEmpty() ? nullptr : Cameras[0];
    for (AActor* Candidate : Cameras)
        if (Candidate->ActorHasTag(BattleCameraTag)) { Camera = Candidate; break; }
    if (Camera) SetViewTarget(Camera);
}
void AAutoBattlePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAutoBattlePlayerController::HandlePrimaryClick);
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAutoBattlePlayerController::ClearSelection);
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AAutoBattlePlayerController::RequestStartCombat);
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AAutoBattlePlayerController::RequestRestart);
}
void AAutoBattlePlayerController::SelectUnit(ABattleUnitBase* Unit)
{
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode || Mode->CurrentPhase != EGamePhase::Preparation || !IsValid(Unit)
        || !Mode->BattleUnits.Contains(Unit) || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive())
    { ClearSelection(); return; }
    SelectedUnit = Unit;
}
void AAutoBattlePlayerController::ClearSelection() { SelectedUnit = nullptr; }
bool AAutoBattlePlayerController::PlaceSelectedUnitOnTile(ABoardTile* Tile)
{
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode || Mode->CurrentPhase != EGamePhase::Preparation || !Mode->bEncounterReady
        || !IsValid(SelectedUnit) || !IsValid(Tile) || SelectedUnit->TeamType != EBattleTeam::Player) return false;
    if (!SelectedUnit->bIsDeployed && Mode->GetDeployedPlayerCount() >= Mode->MaxBoardUnits)
    { Mode->StatusMessage = TEXT("Deployment limit reached. Move an existing unit instead."); return false; }
    if (!SelectedUnit->PlaceOnTile(Tile))
    { Mode->StatusMessage = TEXT("Choose an empty player tile."); return false; }
    Mode->StatusMessage = TEXT("Unit placed. Select another unit or press Enter to start.");
    ClearSelection();
    return true;
}
void AAutoBattlePlayerController::HandlePrimaryClick()
{
    float X, Y;
    if (GetMousePosition(X, Y) && GetHUD() && GetHUD()->GetHitBoxAtCoordinates(FVector2D(X, Y), true)) return;
    FHitResult Hit;
    if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit)) return;
    if (ABattleUnitBase* Unit = Cast<ABattleUnitBase>(Hit.GetActor())) SelectUnit(Unit);
    else if (ABoardTile* Tile = Cast<ABoardTile>(Hit.GetActor()))
    {
        if (IsValid(SelectedUnit)) PlaceSelectedUnitOnTile(Tile);
        else if (ABattleUnitBase* Occupant = Cast<ABattleUnitBase>(Tile->OccupiedUnit)) SelectUnit(Occupant);
    }
}
void AAutoBattlePlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    HoveredTile = nullptr;
    if (!Mode || Mode->CurrentPhase != EGamePhase::Preparation) { ClearSelection(); return; }
    FHitResult Hit;
    if (GetHitResultUnderCursor(ECC_Visibility, false, Hit)) HoveredTile = Cast<ABoardTile>(Hit.GetActor());
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
        if (Mode->CurrentPhase == EGamePhase::RoundClear) Mode->AdvanceRound();
        else Mode->StartCombatPhase();
    }
}
void AAutoBattlePlayerController::RequestRestart()
{
    if (AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this))) Mode->RestartBattle();
}
