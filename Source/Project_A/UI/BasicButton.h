#pragma once

#include "Blueprint/UserWidget.h"
#include "BasicButton.generated.h"

class UButton;
class UProjectUITheme;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBasicButtonClicked);

UCLASS(Blueprintable)
class PROJECT_A_API UBasicButton : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetTheme(UProjectUITheme& InTheme);

    UFUNCTION(BlueprintCallable, Category="UI")
    void SetLabel(const FText& InLabel);

    void SetBody(UWidget* Content);

    UPROPERTY(BlueprintAssignable, Category="UI")
    FBasicButtonClicked OnClicked;

    TFunction<void()> Action;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UProjectUITheme> Theme;

    UPROPERTY(Transient)
    TObjectPtr<UButton> Button;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Label;

    UPROPERTY(Transient)
    TObjectPtr<UWidget> Body;

    UPROPERTY(EditAnywhere, Category="UI")
    FText LabelText;

    UFUNCTION()
    void Clicked();
};
