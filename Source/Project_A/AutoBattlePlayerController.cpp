#include "AutoBattlePlayerController.h"
#include "AutoBattleGameMode.h"
#include "BattleUnitBase.h"
#include "BoardTile.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
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
    if (!Mode || Mode->CurrentPhase != EGamePhase::Preparation || !Mode->bPreparationActive || !IsValid(Unit)
        || !Mode->BattleUnits.Contains(Unit) || Unit->TeamType != EBattleTeam::Player || !Unit->IsAlive())
    { ClearSelection(); return; }
    SelectedUnit = Unit;
}
void AAutoBattlePlayerController::ClearSelection() { SelectedUnit = nullptr; }
bool AAutoBattlePlayerController::PlaceSelectedUnitOnTile(ABoardTile* Tile)
{
    AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode || Mode->CurrentPhase != EGamePhase::Preparation || !Mode->bPreparationActive || !Mode->bEncounterReady
        || !IsValid(SelectedUnit) || !IsValid(Tile) || SelectedUnit->TeamType != EBattleTeam::Player) return false;
    if (!SelectedUnit->bIsDeployed && Mode->GetDeployedPlayerCount() >= Mode->MaxBoardUnits)
    { Mode->StatusMessage = TEXT("Deployment limit reached. Move an existing unit instead."); return false; }
    if (!SelectedUnit->PlaceOnTile(Tile))
    { Mode->StatusMessage = TEXT("Choose an empty player tile."); return false; }
    Mode->StatusMessage = TEXT("Unit placed. Select another unit or press Enter to start.");
    ClearSelection();
    return true;
}
bool AAutoBattlePlayerController::GetBoardClickHit(FHitResult& Hit)
{
    Hit = FHitResult();
    float X, Y;
    if (!GetMousePosition(X, Y)) return false;
    if (GetHUD() && GetHUD()->GetHitBoxAtCoordinates(FVector2D(X, Y), true)) return false;
    // Preserve complex tracing for the custom hex tile mesh.
    return GetHitResultUnderCursor(ECC_Visibility, true, Hit);
}
void AAutoBattlePlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    HoveredTile = nullptr;
    if (!Mode || Mode->CurrentPhase != EGamePhase::Preparation) { ClearSelection(); return; }
    FHitResult Hit;
    if (GetHitResultUnderCursor(ECC_Visibility, true, Hit)) HoveredTile = Cast<ABoardTile>(Hit.GetActor());
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
