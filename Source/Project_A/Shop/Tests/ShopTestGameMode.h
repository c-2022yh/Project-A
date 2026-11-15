#pragma once

#include "GameFramework/GameModeBase.h"
#include "../UnitPlacement.h"
#include "ShopTestGameMode.generated.h"

class UTestPlacement;
class AShopTransactionManager;
class UGameViewportClient;
struct FInputKeyEventArgs;
class AStaticMeshActor;
class UStaticMesh;

UCLASS()
class AShopTestGameMode : public AGameModeBase, public IUnitPlacement
{
    GENERATED_BODY()

public:
    AShopTestGameMode();
    virtual void PostLogin(APlayerController* Player) override;
    virtual FUnitPosition GetUnitPosition_Implementation(FGuid Id) const override;
    virtual FGuid GetOccupant_Implementation(FUnitPosition Position) const override;
    virtual FGuid GetHoveredUnit_Implementation() const override;
    virtual bool ApplyUnitChanges_Implementation(const TArray<FRosterUnitChange>& Changes, EShopPhase Phase, int32 Limit) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UPROPERTY()
    TObjectPtr<UTestPlacement> Positions;

    UPROPERTY()
    TObjectPtr<UStaticMesh> Cube;

    UPROPERTY()
    TMap<FGuid, TObjectPtr<AStaticMeshActor>> Pieces;

    UPROPERTY()
    TObjectPtr<AShopTransactionManager> Transactions;

    UPROPERTY()
    TArray<TObjectPtr<AStaticMeshActor>> BenchTiles;

    TWeakObjectPtr<UGameViewportClient> InputViewport;
    FDelegateHandle InputHandle;
    FGuid SelectedPiece;

    FGuid FindPiece(const AActor* Actor) const;
    void HandleBenchClick(const FInputKeyEventArgs& Event);

    AStaticMeshActor* SpawnCube(FVector Position, FVector Scale);
};
