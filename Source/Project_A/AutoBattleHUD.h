#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AutoBattleHUD.generated.h"

// Temporary canvas HUD; replace HUDClass with a custom UI when artwork is ready.
UCLASS()
class PROJECT_A_API AAutoBattleHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
};
