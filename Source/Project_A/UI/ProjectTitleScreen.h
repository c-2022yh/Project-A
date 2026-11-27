#pragma once

#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "ProjectTitleScreen.generated.h"

UCLASS()
class UProjectTitleScreen : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    void StartGame();
};

UCLASS()
class AProjectTitleGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AProjectTitleGameMode();
    virtual void PostLogin(APlayerController* Player) override;

protected:
    virtual void BeginPlay() override;
};
