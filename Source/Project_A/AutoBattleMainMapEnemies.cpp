#include "AutoBattleGameMode.h"
#include "BoardManager.h"
#include "BoardTile.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

bool AAutoBattleGameMode::PrepareManagedEnemies(int32 Round, FString& Error)
{
    const UDataTable* Table = RoundEnemyFormationsTable.LoadSynchronous();
    if (RoundsTable.IsNull() && (!Table || Table->GetRowStruct() != FRoundEnemyFormation::StaticStruct()))
    {
        Error = TEXT("Assign a Round Enemy Formation data table in the main GameMode class defaults.");
        return false;
    }

    const FName RowName(*FString::Printf(TEXT("R%d"), Round));
    const auto* Unified = GetUnifiedRound(Round);
    const FRoundEnemyFormation* Formation = RoundsTable.IsNull()
        ? Table->FindRow<FRoundEnemyFormation>(RowName, TEXT("Round enemy formation"), false) : nullptr;
    if ((!Unified && !Formation) || (Unified ? Unified->Enemies.IsEmpty() : Formation->Enemies.IsEmpty()))
    {
        Error = FString::Printf(TEXT("Add at least one enemy to row %s in the enemy formation table."), *RowName.ToString());
        return false;
    }

    const TArray<FRoundEnemySpawn>& Entries = Unified ? Unified->Enemies : Formation->Enemies;
    UDataTable* Units = MainMapUnitDefinitions.LoadSynchronous();
    UClass* SpawnClass = MainMapUnitClass.LoadSynchronous();
    if (!Units || Units->GetRowStruct() != FUnitDefinition::StaticStruct()
        || !SpawnClass || SpawnClass->HasAnyClassFlags(CLASS_Abstract))
    {
        Error = TEXT("Configure the shared unit class and unit table.");
        return false;
    }

    TMap<ABattleUnitBase*, ABoardTile*> PreviousEnemies;
    for (ABattleUnitBase* Unit : BattleUnits)
        if (IsValid(Unit) && Unit->TeamType == EBattleTeam::Enemy)
            PreviousEnemies.Add(Unit, Unit->CurrentTile);

    TArray<ABoardTile*> Targets;
    TSet<ABoardTile*> ReservedTargets;
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        const FRoundEnemySpawn& Entry = Entries[Index];
        ABoardTile* Tile = BoardManager->GetTile(Entry.Row, Entry.Column);
        AActor* Occupant = IsValid(Tile) ? Tile->OccupiedUnit : nullptr;
        if (!IsValid(Tile) || Tile->TeamType != EBoardTileTeam::Enemy || Tile->bBlocksMovement || Tile->IsReserved()
            || (IsValid(Occupant) && !PreviousEnemies.Contains(Cast<ABattleUnitBase>(Occupant))) || ReservedTargets.Contains(Tile)
            || Entry.UnitRow.IsNone() || Entry.Stars < 1 || Entry.Stars > 2
            || !Units->FindRow<FUnitDefinition>(Entry.UnitRow, TEXT("Round enemy formation"), false))
        {
            Error = FString::Printf(TEXT("Invalid enemy entry %d (%s, %d stars, row %d, column %d)."),
                Index + 1, *Entry.UnitRow.ToString(), Entry.Stars, Entry.Row, Entry.Column);
            return false;
        }
        Targets.Add(Tile);
        ReservedTargets.Add(Tile);
    }

    // 이전 적을 남긴 채 새 적의 데이터 초기화를 먼저 검증한다.
    TArray<ABattleUnitBase*> Spawned;
    const auto RemoveSpawned = [this, &Spawned]()
    {
        for (ABattleUnitBase* Unit : Spawned)
        {
            BoardManager->ReleaseUnitTiles(Unit);
            Unit->Destroy();
        }
    };
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        const FRoundEnemySpawn& Entry = Entries[Index];
        ABattleUnitBase* Unit = GetWorld()->SpawnActor<ABattleUnitBase>(
            SpawnClass, Targets[Index]->GetSurfaceLocation(), FRotator::ZeroRotator, Params);
        if (IsValid(Unit)) Spawned.Add(Unit);
        FDataTableRowHandle UnitData;
        UnitData.DataTable = Units;
        UnitData.RowName = Entry.UnitRow;
        Error = TEXT("Could not spawn an enemy actor.");
        if (!IsValid(Unit) || !Unit->InitializeUnit(UnitData, EBattleTeam::Enemy, Entry.Stars, Error))
        {
            RemoveSpawned();
            return false;
        }
    }

    for (const auto& Previous : PreviousEnemies) BoardManager->ReleaseUnitTiles(Previous.Key);
    for (int32 Index = 0; Index < Spawned.Num(); ++Index)
    {
        if (Spawned[Index]->PlaceOnTile(Targets[Index])) continue;
        RemoveSpawned();
        for (const auto& Previous : PreviousEnemies)
            if (IsValid(Previous.Value) && !Previous.Key->PlaceOnTile(Previous.Value))
                UE_LOG(LogTemp, Error, TEXT("Could not restore previous enemy %s after a placement failure."), *GetNameSafe(Previous.Key));
        Error = FString::Printf(TEXT("Could not place enemy entry %d."), Index + 1);
        return false;
    }

    for (const auto& Previous : PreviousEnemies)
    {
        BattleUnits.Remove(Previous.Key);
        Previous.Key->Destroy();
    }
    BattleUnits.Append(Spawned);
    for (ABattleUnitBase* Unit : Spawned) Unit->PrepareSkillsForRound();
    UE_LOG(LogTemp, Display, TEXT("ROUND_ENEMY_FORMATION: round=%d enemies=%d"), Round, Spawned.Num());
    return true;
}
