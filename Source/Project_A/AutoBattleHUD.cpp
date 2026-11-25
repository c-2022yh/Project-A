#include "AutoBattleHUD.h"
#include "AutoBattleGameMode.h"
#include "AutoBattlePlayerController.h"
#include "Engine/Canvas.h"
#include "Kismet/GameplayStatics.h"

void AAutoBattleHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner) return;
    const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Mode) return;
    const float Width = Canvas->ClipX;
    const bool bShowingReward = Mode->GetShopMatchState_Implementation().Phase == EShopPhase::RewardSelection;
    // Preparation time and the start button are owned by WBP_BattleHUD.
    // Keep the remaining presentation until it is implemented in the widget.
    if (Mode->CurrentPhase != EGamePhase::Preparation && Mode->CurrentPhase != EGamePhase::Combat
        && !bShowingReward && !Mode->bShowingRoundClear
        && !Mode->bManagedDefeated && !Mode->bManagedTestFinished)
    {
        DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.85f), 0, 0, Width, 72);
        FString Title;
        switch (Mode->CurrentPhase)
        {
            case EGamePhase::Victory: Title = TEXT("VICTORY"); break;
            case EGamePhase::GameOver: Title = TEXT("DEFEAT"); break;
            default: Title = TEXT("ROUND CLEAR"); break;
        }
        DrawText(Title, FLinearColor::White, 14, 8, nullptr, 1.1f);
        DrawText(Mode->StatusMessage, FLinearColor(0.8f, 0.85f, 0.95f), 14, 31, nullptr, 0.7f);
        if (!Mode->bEnableMainMapRoundTest)
            DrawText(TEXT("R: restart"), FLinearColor::White, 14, 51, nullptr, 0.7f);
    }
    const float ButtonY = FMath::Max(110.0f, Canvas->ClipY - 52.0f);
    if (!Mode->bEnableMainMapRoundTest && Mode->bEncounterReady && Mode->CurrentPhase == EGamePhase::RoundClear)
    {
        DrawRect(FLinearColor(0.05f, 0.25f, 0.4f, 0.95f), 20, ButtonY, 160, 36);
        DrawText(TEXT("NEXT ROUND"), FLinearColor::White, 32, ButtonY + 9);
        AddHitBox(FVector2D(20, ButtonY), FVector2D(160, 36), TEXT("NextRound"), true);
    }
    if (!Mode->bEnableMainMapRoundTest)
    {
        DrawRect(FLinearColor(0.2f, 0.2f, 0.25f, 0.95f), Width - 150, ButtonY, 130, 36);
        DrawText(TEXT("RESTART"), FLinearColor::White, Width - 132, ButtonY + 9);
        AddHitBox(FVector2D(Width - 150, ButtonY), FVector2D(130, 36), TEXT("Restart"), true);
    }

    // 체력바와 장착 아이콘은 각 유닛의 UnitStatusWidget에서 표시한다.
}
void AAutoBattleHUD::NotifyHitBoxClick(FName BoxName)
{
    Super::NotifyHitBoxClick(BoxName);
    if (AAutoBattlePlayerController* PC = Cast<AAutoBattlePlayerController>(PlayerOwner))
    {
        if (BoxName == TEXT("NextRound"))
        {
            const AAutoBattleGameMode* Mode = Cast<AAutoBattleGameMode>(UGameplayStatics::GetGameMode(this));
            if (Mode && Mode->CurrentPhase == EGamePhase::RoundClear) PC->RequestStartCombat();
        }
        else if (BoxName == TEXT("Restart")) PC->RequestRestart();
    }
}
