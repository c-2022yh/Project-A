#include "BoardManager.h"
#include "BoardTile.h"
#include "Components/SceneComponent.h"

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
    if (TileClass == nullptr) return;
    
    UWorld* World = GetWorld();

	if (World == nullptr) return;

    //기존 데이터가 남아있다면 배열 초기화
    Tiles.Empty();

    for (int32 Row = 0; Row < Rows; Row++)
    {
        for (int32 Col = 0; Col < Columns; Col++)
        {
            float BoardWidth = (Columns - 1) * HorizontalSpacing;
            float BoardHeight = (Rows - 1) * VerticalSpacing;

            float X = Col * HorizontalSpacing - BoardWidth * 0.5f;
            float Y = Row * VerticalSpacing - BoardHeight * 0.5f;

            //홀수 행은 오른쪽으로 반 칸 이동
            if (Row % 2 == 1)
            {
                X += HorizontalSpacing * 0.5f;
            }

            FVector SpawnLocation = GetActorLocation() + FVector(X, Y, 0.0f);
            FActorSpawnParameters SpawnParams;

            ABoardTile* NewTile = World->SpawnActor<ABoardTile>(
                TileClass,
                SpawnLocation,
                FRotator(0.0f, 90.0f, 0.0f),
                SpawnParams
            );

            if (NewTile != nullptr)
            {
                NewTile->Row = Col;
                NewTile->Column = Row;

				//하위 액터로 들어가게
                NewTile->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

                Tiles.Add(NewTile);
            }
        }
    }
}