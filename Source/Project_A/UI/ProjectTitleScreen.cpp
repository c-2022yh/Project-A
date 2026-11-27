#include "ProjectTitleScreen.h"
#include "BasicButton.h"
#include "ProjectUITheme.h"
#include "../UnitTypes.h"
#include "Animation/SkeletalMeshActor.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

#define LOCTEXT_NAMESPACE "ProjectTitleScreen"

namespace
{
const FName TestBattleMap(TEXT("/Game/UITestMap/MainMap_Intergration1"));

void AddShowcaseUnit(UWorld& World, const UDataTable& Units, FName Id, FVector Location, float Yaw)
{
    const FUnitDefinition* Unit = Units.FindRow<FUnitDefinition>(Id, TEXT("Title showcase"), false);
    if (!Unit || !Unit->Visuals.SkeletalMesh) return;

    auto* Actor = World.SpawnActor<ASkeletalMeshActor>(Location, FRotator(0.f, Yaw, 0.f));
    if (!Actor) return;
    auto* Mesh = Actor->GetSkeletalMeshComponent();
    Mesh->SetSkeletalMeshAsset(Unit->Visuals.SkeletalMesh);
    FTransform Showcase = Unit->Visuals.VisualTransform;
    Showcase.MultiplyScale3D(FVector(0.34));
    Actor->SetActorTransform(Showcase * Actor->GetActorTransform());
    for (int32 Index = 0; Index < Unit->Visuals.MaterialOverrides.Num(); ++Index)
        if (Unit->Visuals.MaterialOverrides[Index]) Mesh->SetMaterial(Index, Unit->Visuals.MaterialOverrides[Index]);
    if (Unit->Animations.IdleAnimation) Mesh->PlayAnimation(Unit->Animations.IdleAnimation, true);
}
}

TSharedRef<SWidget> UProjectTitleScreen::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
        Canvas->SetClipping(EWidgetClipping::ClipToBounds);
        WidgetTree->RootWidget = Canvas;

        auto* Tint = WidgetTree->ConstructWidget<UBorder>();
        Tint->SetBrushColor(FLinearColor(0.008f, 0.016f, 0.036f, 0.24f));
        Tint->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* TintSlot = Canvas->AddChildToCanvas(Tint);
        TintSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        TintSlot->SetOffsets(FMargin(0.f));

        auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
        auto* Heading = UProjectUITheme::Label(*WidgetTree, LOCTEXT("Heading", "PROJECT A"), 60, UProjectUITheme::Text, true);
        Column->AddChildToVerticalBox(Heading);
        auto* Rule = WidgetTree->ConstructWidget<UBorder>();
        Rule->SetBrushColor(UProjectUITheme::Gold);
        auto* Line = WidgetTree->ConstructWidget<USizeBox>();
        Line->SetWidthOverride(87.f);
        Line->SetHeightOverride(2.f);
        Line->SetContent(Rule);
        auto* LineSlot = Column->AddChildToVerticalBox(Line);
        LineSlot->SetHorizontalAlignment(HAlign_Left);
        LineSlot->SetPadding(FMargin(3.f, 12.f, 0.f, 34.f));

        auto* Start = CreateWidget<UBasicButton>(GetOwningPlayer(), UBasicButton::StaticClass());
        Start->SetLabel(LOCTEXT("Start", "게임 시작"));
        Start->Action = [this] { StartGame(); };
        auto* StartSize = WidgetTree->ConstructWidget<USizeBox>();
        StartSize->SetWidthOverride(260.f);
        StartSize->SetHeightOverride(64.f);
        StartSize->SetContent(Start);
        auto* StartSlot = Column->AddChildToVerticalBox(StartSize);
        StartSlot->SetHorizontalAlignment(HAlign_Left);

        UProjectUITheme::Pin(*Canvas, *Column, FVector2D(0.085, 0.53), FVector2D(0, 0.5), FVector2D::ZeroVector);
    }
    return Super::RebuildWidget();
}

void UProjectTitleScreen::StartGame()
{
    UGameplayStatics::OpenLevel(this, TestBattleMap);
}

AProjectTitleGameMode::AProjectTitleGameMode()
{
    DefaultPawnClass = nullptr;
}

void AProjectTitleGameMode::BeginPlay()
{
    Super::BeginPlay();
    const auto* Units = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Units.DT_Units"));
    if (!Units) return;
    AddShowcaseUnit(*GetWorld(), *Units, TEXT("Knight"), FVector(0, 165, 0), 97.f);
    AddShowcaseUnit(*GetWorld(), *Units, TEXT("Archer"), FVector(115, 251, 0), 118.f);
    AddShowcaseUnit(*GetWorld(), *Units, TEXT("Mage"), FVector(90, 72, 0), 86.f);
}

void AProjectTitleGameMode::PostLogin(APlayerController* Player)
{
    Super::PostLogin(Player);
    if (!Player || !Player->IsLocalController() || !FApp::CanEverRender()) return;

    auto* Camera = GetWorld()->SpawnActor<ACameraActor>(FVector(-510, 0, 135), FRotator(-6, 0, 0));
    if (Camera)
    {
        Camera->GetCameraComponent()->SetFieldOfView(57.f);
        Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
        Player->SetViewTarget(Camera);
    }

    auto* Title = CreateWidget<UProjectTitleScreen>(Player);
    if (!Title) return;
    Title->SetIsFocusable(true);
    Title->AddToPlayerScreen();
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(Title->TakeWidget());
    Player->SetInputMode(Mode);
    Player->bShowMouseCursor = true;
}

#undef LOCTEXT_NAMESPACE
