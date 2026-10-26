#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AutoBattleHUD.generated.h"

// Canvas fallback for unit health, battle results, restart and next round.
UCLASS()
class PROJECT_A_API AAutoBattleHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
};
