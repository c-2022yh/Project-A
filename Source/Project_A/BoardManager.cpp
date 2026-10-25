#include "BoardManager.h"
#include "BoardTile.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

ABoardManager::ABoardManager()
{
    PrimaryActorTick.bCanEverTick = false;
    
    //씬 루트 컴포넌트 생성
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
}

void ABoardManager::BeginPlay()
{
    Super::BeginPlay();

    GenerateBoard();
}

void ABoardManager::GenerateBoard()
{
    UWorld* World = GetWorld();
    if (!World || !TileClass || Rows <= 0 || Columns <= 0)
    {
        return;
    }

    // 보드를 다시 생성할 때 배열뿐 아니라 기존 타일 액터도 제거한다.
    for (ABoardTile* Tile : Tiles)
    {
        if (IsValid(Tile))
        {
            Tile->Destroy();
        }
    }
    Tiles.Empty();
    EffectiveHorizontalSpacing = 0.0f;
    EffectiveVerticalSpacing = 0.0f;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FRotator TileRotation(0.0f, 90.0f, 0.0f);

    // BP 생성 시 설정과 메시 스케일까지 반영하려고 실제 타일을 생성하여 크기를 측정한다.
    // 측정에 사용한 액터는 첫 번째 보드 타일로 재사용한다.
    ABoardTile* FirstTile = World->SpawnActor<ABoardTile>(
        TileClass, GetActorLocation(), TileRotation, SpawnParams);
    if (!FirstTile)
    {
        return;
    }

    float SpacingX = HorizontalSpacing;
    float SpacingY = VerticalSpacing;
    if (bAutoCalculateSpacing)
    {
        UStaticMeshComponent* Mesh = FirstTile->TileMesh;
        if (!IsValid(Mesh) || !Mesh->GetStaticMesh())
        {
            UE_LOG(LogTemp, Warning, TEXT("BoardManager: Assign a TileMesh static mesh before using automatic spacing."));
            FirstTile->Destroy();
            return;
        }

        const FVector TileSize = Mesh->CalcBounds(Mesh->GetComponentTransform()).BoxExtent * 2.0;
        const float GapMultiplier = 1.0f + FMath::Max(0.0f, GapRatio);
        SpacingX = TileSize.X * GapMultiplier;
        SpacingY = TileSize.Y * 0.75f * GapMultiplier;
    }

    if (!FMath::IsFinite(SpacingX) || !FMath::IsFinite(SpacingY)
        || SpacingX <= KINDA_SMALL_NUMBER || SpacingY <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("BoardManager: Tile spacing must be finite and greater than zero."));
        FirstTile->Destroy();
        return;
    }

    EffectiveHorizontalSpacing = SpacingX;
    EffectiveVerticalSpacing = SpacingY;

    // 보드를 중앙에 맞출 때 엇갈린 행이 가로로 반 칸 더 차지하는 폭도 포함한다.
    const float StaggerWidth = Rows > 1 ? SpacingX * 0.5f : 0.0f;
    const float BoardWidth = (Columns - 1) * SpacingX + StaggerWidth;
    const float BoardHeight = (Rows - 1) * SpacingY;
    const int32 ClampedPlayerRows = FMath::Clamp(PlayerRows, 0, Rows);

    for (int32 Row = 0; Row < Rows; ++Row)
    {
        for (int32 Col = 0; Col < Columns; ++Col)
        {
            const float RowOffset = Row % 2 == 1 ? SpacingX * 0.5f : 0.0f;
            const FVector SpawnLocation = GetActorLocation() + FVector(
                Col * SpacingX + RowOffset - BoardWidth * 0.5f,
                Row * SpacingY - BoardHeight * 0.5f,
                0.0f);

            ABoardTile* NewTile = Row == 0 && Col == 0
                ? FirstTile
                : World->SpawnActor<ABoardTile>(TileClass, SpawnLocation, TileRotation, SpawnParams);
            if (!NewTile)
            {
                continue;
            }

            NewTile->SetActorLocation(SpawnLocation);
            NewTile->Row = Row;
            NewTile->Column = Col;
            NewTile->HexQ = Col - (Row - (Row & 1)) / 2;
            NewTile->HexR = Row;

            const bool bPlayerTile = Row < ClampedPlayerRows;
            NewTile->TeamType = bPlayerTile ? EBoardTileTeam::Player : EBoardTileTeam::Enemy;

            UMaterialInterface* TeamMaterial = bPlayerTile
                ? PlayerTileMaterial.Get() : EnemyTileMaterial.Get();
            if (IsValid(NewTile->TileMesh) && TeamMaterial)
            {
                NewTile->TileMesh->SetMaterial(0, TeamMaterial);
            }

            NewTile->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
            Tiles.Add(NewTile);
        }
    }
    OnBoardGenerated.Broadcast();
}

ABoardTile* ABoardManager::GetTile(int32 Row, int32 Column) const
{
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && Tile->Row == Row && Tile->Column == Column) return Tile;
    return nullptr;
}

TArray<ABoardTile*> ABoardManager::GetPlayerTiles() const
{
    TArray<ABoardTile*> Result;
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && Tile->TeamType == EBoardTileTeam::Player) Result.Add(Tile);
    return Result;
}

TArray<ABoardTile*> ABoardManager::GetEnemyTiles() const
{
    TArray<ABoardTile*> Result;
    for (ABoardTile* Tile : Tiles)
        if (IsValid(Tile) && Tile->TeamType == EBoardTileTeam::Enemy) Result.Add(Tile);
    return Result;
}
