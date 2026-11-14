#include "ShopTestGameMode.h"
#include "TestPlacement.h"
#include "../BenchManager.h"
#include "../RosterManager.h"
#include "../ShopManager.h"
#include "../ShopTransactionManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

namespace
{
FVector BenchLocation(int32 Slot)
{
    return FVector((Slot - (UBenchManager::SlotCount - 1) * 0.5) * 165, 0, 0);
}
}

AShopTestGameMode::AShopTestGameMode()
{
    DefaultPawnClass = nullptr;
    HUDClass = AHUD::StaticClass();
}

void AShopTestGameMode::PostLogin(APlayerController* Player)
{
    Super::PostLogin(Player);
    Player->bShowMouseCursor = true;
    FInputModeGameAndUI InputMode;
    InputMode.SetHideCursorDuringCapture(false);
    Player->SetInputMode(InputMode);
    ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(FVector(0, -1500, 1350), FRotator(-38, 90, 0));
    if (Camera)
    {
        Camera->GetCameraComponent()->SetFieldOfView(52);
        Player->SetViewTarget(Camera);
    }
}

void AShopTestGameMode::BeginPlay()
{
    Super::BeginPlay();
    Positions = NewObject<UTestPlacement>(this);
    Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    SpawnCube(FVector(0, 0, -48), FVector(14, 4, 0.4));
    for (int32 Slot = 0; Slot < UBenchManager::SlotCount; ++Slot)
        SpawnCube(BenchLocation(Slot) - FVector(0, 0, 16), FVector(1.45, 1.6, 0.2));

    auto* Shop = GetWorld()->SpawnActor<AShopManager>();
    auto* Roster = GetWorld()->SpawnActor<ARosterManager>();
    auto* Items = GetWorld()->SpawnActor<AItemManager>();
    auto* Transactions = GetWorld()->SpawnActor<AShopTransactionManager>();
    if (!Shop || !Roster || !Items || !Transactions) return;

    Shop->StartingGold = 20;
    for (const FName Name : { FName(TEXT("Tank")), FName(TEXT("Archer")), FName(TEXT("Mage")) })
        Shop->Catalog.Add({ Name, FText::FromName(Name), 1 });

    Roster->Placement.SetObject(this);
    Roster->Placement.SetInterface(this);
    Transactions->Shop = Shop;
    Transactions->Roster = Roster;
    Transactions->Items = Items;
    ensure(Transactions->EnterPreparation(1, 2));
}

AStaticMeshActor* AShopTestGameMode::SpawnCube(FVector Position, FVector Scale)
{
    if (!Cube) return nullptr;
    auto* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(Position, FRotator::ZeroRotator);
    if (!Actor) return nullptr;
    Actor->SetMobility(EComponentMobility::Movable);
    Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Actor->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Actor->SetActorScale3D(Scale);
    return Actor;
}

FUnitPosition AShopTestGameMode::GetUnitPosition_Implementation(FGuid Id) const
{
    return Positions->GetUnitPosition_Implementation(Id);
}

FGuid AShopTestGameMode::GetOccupant_Implementation(FUnitPosition Position) const
{
    return Positions->GetOccupant_Implementation(Position);
}

FGuid AShopTestGameMode::GetHoveredUnit_Implementation() const
{
    APlayerController* Player = GetWorld()->GetFirstPlayerController();
    FHitResult Hit;
    if (Player && Player->GetHitResultUnderCursor(ECC_Visibility, false, Hit))
        for (const auto& Entry : Pieces)
            if (Entry.Value == Hit.GetActor()) return Entry.Key;
    return {};
}

bool AShopTestGameMode::ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit)
{
    TMap<FGuid, AStaticMeshActor*> NewPieces;
    for (const auto& Change : Changes)
    {
        if ((Change.Edit == ERosterUnitEdit::Add || Change.Edit == ERosterUnitEdit::Move)
            && (Change.Destination.Location != EUnitLocation::Bench || !UBenchManager::IsValidSlot(Change.Destination.Column)))
            return false;
    }

    for (const auto& Change : Changes)
    {
        if (Change.Edit != ERosterUnitEdit::Add) continue;
        auto* Piece = SpawnCube(BenchLocation(Change.Destination.Column) + FVector(0, 0, 45), FVector(0.8, 0.8, 1));
        if (!Piece)
        {
            for (const auto& Entry : NewPieces) Entry.Value->Destroy();
            return false;
        }
        NewPieces.Add(Change.Unit.Id, Piece);
    }

    if (!Positions->ApplyUnitChanges_Implementation(Changes, Phase, Limit))
    {
        for (const auto& Entry : NewPieces) Entry.Value->Destroy();
        return false;
    }

    for (const auto& Entry : NewPieces) Pieces.Add(Entry.Key, Entry.Value);
    for (const auto& Change : Changes)
    {
        const FGuid Id = Change.Unit.Id;
        auto* Piece = Pieces.FindRef(Id).Get();
        if (!Piece) continue;
        if (Change.Edit == ERosterUnitEdit::Remove)
        {
            Piece->Destroy();
            Pieces.Remove(Id);
            continue;
        }
        const double Scale = Change.StarVisualScale;
        Piece->SetActorScale3D(FVector(0.8 * Scale, 0.8 * Scale, Scale));
        Piece->SetActorLocation(BenchLocation(GetUnitPosition_Implementation(Id).Column) + FVector(0, 0, 50 * Scale - 6));
    }
    return true;
}
