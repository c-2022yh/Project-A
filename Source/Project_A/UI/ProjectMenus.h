#pragma once

#include "Blueprint/UserWidget.h"
#include "ProjectMenus.generated.h"

class UBorder;
class UProjectUITheme;
class UTextBlock;
class UVerticalBox;

UENUM(BlueprintType)
enum class EProjectGameResult : uint8
{
    RoundClear,
    RoundDefeat,
    Victory,
    Defeat
};

struct FProjectMenuActions
{
    FSimpleDelegate Restart;
    FSimpleDelegate ReturnToTitle;
    FSimpleDelegate Quit;
};

UCLASS(Blueprintable)
class UGameResultScreen : public UUserWidget
{
    GENERATED_BODY()

public:
    void Configure(UProjectUITheme& InTheme, const FProjectMenuActions& InActions);
    UFUNCTION(BlueprintCallable, Category="UI|Result")
    void Present(EProjectGameResult Result, int32 Round, int32 ClearedRounds, int32 TotalRounds);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY(Transient) TObjectPtr<UProjectUITheme> Theme;
    UPROPERTY(Transient) TObjectPtr<UBorder> Backdrop;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Heading;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Buttons;

    FProjectMenuActions Actions;
};

UCLASS(Blueprintable)
class UPauseMenu : public UUserWidget
{
    GENERATED_BODY()

public:
    void Configure(UProjectUITheme& InTheme, const FProjectMenuActions& InActions, FSimpleDelegate InResume);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
    UPROPERTY(Transient) TObjectPtr<UProjectUITheme> Theme;
    FProjectMenuActions Actions;
    FSimpleDelegate Resume;
};
