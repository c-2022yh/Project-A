#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AutoBattleHUD.generated.h"

// 임시 Canvas HUD. UI가 준비되면 HUDClass를 교체하여 사용할 수 있다.
UCLASS()
class PROJECT_A_API AAutoBattleHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
};
