#include "AutoBattleHUD.h"
#include "AutoBattleGameMode.h"
#include "AutoBattlePlayerController.h"
#include "BattleUnitBase.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Kismet/GameplayStatics.h"

void AAutoBattleHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode) return;
    const float Width = Canvas->ClipX;
    DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.85f), 0, 0, Width, 72);
    FString Title;
    switch (Mode->CurrentPhase)
    {
        case EGamePhase::Preparation:
            Title = FString::Printf(TEXT("PREPARATION  %.0fs    Deployed %d / %d"),
                Mode->RemainingPreparationTime, Mode->GetDeployedPlayerCount(), Mode->MaxBoardUnits); break;
        case EGamePhase::Combat:
            Title = FString::Printf(TEXT("COMBAT  %.0fs    Blue %d : Red %d"), Mode->ElapsedCombatTime,
                Mode->GetAliveTeamCount(EBattleTeam::Player), Mode->GetAliveTeamCount(EBattleTeam::Enemy)); break;
        case EGamePhase::Victory: Title = TEXT("VICTORY"); break;
        case EGamePhase::GameOver: Title = TEXT("DEFEAT"); break;
        default: Title = TEXT("ROUND CLEAR"); break;
    }
    DrawText(Title, FLinearColor::White, 14, 8, nullptr, 1.1f);
    DrawText(Mode->StatusMessage, FLinearColor(0.8f, 0.85f, 0.95f), 14, 31, nullptr, 0.7f);
    DrawText(TEXT("LMB: select/place   RMB: cancel   Enter: start   R: restart"), FLinearColor::White, 14, 51, nullptr, 0.7f);
    const bool CanStart = Mode->bEncounterReady && (Mode->CurrentPhase == EGamePhase::Preparation || Mode->CurrentPhase == EGamePhase::RoundClear);
    const float ButtonY = FMath::Max(110.0f, Canvas->ClipY - 52.0f);
    if (CanStart)
    {
        DrawRect(FLinearColor(0.05f, 0.25f, 0.4f, 0.95f), 20, ButtonY, 160, 36);
        DrawText(Mode->CurrentPhase == EGamePhase::RoundClear ? TEXT("NEXT ROUND") : TEXT("START COMBAT"), FLinearColor::White, 32, ButtonY + 9);
        AddHitBox(FVector2D(20, ButtonY), FVector2D(160, 36), TEXT("Start"), true);
    }
    DrawRect(FLinearColor(0.2f, 0.2f, 0.25f, 0.95f), Width - 150, ButtonY, 130, 36);
    DrawText(TEXT("RESTART"), FLinearColor::White, Width - 132, ButtonY + 9);
    AddHitBox(FVector2D(Width - 150, ButtonY), FVector2D(130, 36), TEXT("Restart"), true);

    const AAutoBattlePlayerController* PC = Cast<AAutoBattlePlayerController>(PlayerOwner);
    for (ABattleUnitBase* Unit : Mode->BattleUnits)
    {
        if (!IsValid(Unit) || !Unit->IsAlive()) continue;
        const FVector Anchor = Unit->GetActorLocation() + FVector(0, 0, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 45);
        // Canvas coordinates exclude the camera's letterbox/pillarbox offset.
        // Project through the same scene view used to draw this HUD.
        const FVector Projected = Canvas->Project(Anchor);
        if (Projected.Z <= 0 || Projected.X < 0 || Projected.X > Canvas->ClipX
            || Projected.Y < 0 || Projected.Y > Canvas->ClipY) continue;
        const FVector2D Screen(Projected.X, Projected.Y);
        const FLinearColor Color = Unit->TeamType == EBattleTeam::Player ? FLinearColor(0.1f, 0.55f, 1) : FLinearColor(1, 0.2f, 0.15f);
        DrawRect(FLinearColor::Black, Screen.X - 31, Screen.Y, 62, 9);
        DrawRect(Color, Screen.X - 30, Screen.Y + 1, 60 * FMath::Clamp(Unit->CurrentHP / FMath::Max(1.0f, Unit->MaxHP), 0.0f, 1.0f), 7);
        const FString Label = PC && PC->SelectedUnit == Unit ? TEXT("SELECTED")
            : Unit->TeamType == EBattleTeam::Player ? (Unit->bIsDeployed ? TEXT("ALLY") : TEXT("WAITING")) : TEXT("ENEMY");
        DrawText(Label, PC && PC->SelectedUnit == Unit ? FLinearColor::Yellow : Color, Screen.X - 30, Screen.Y - 18, nullptr, 0.8f);
    }
}
void AAutoBattleHUD::NotifyHitBoxClick(FName BoxName)
{
    Super::NotifyHitBoxClick(BoxName);
    if (AAutoBattlePlayerController* PC = Cast<AAutoBattlePlayerController>(PlayerOwner))
    {
        if (BoxName == TEXT("Start")) PC->RequestStartCombat();
        else if (BoxName == TEXT("Restart")) PC->RequestRestart();
    }
}
