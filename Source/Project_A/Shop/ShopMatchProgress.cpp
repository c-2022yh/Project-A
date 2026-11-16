#include "ShopMatchProgress.h"
#include "../AutoBattleGameMode.h"
#include "Engine/World.h"

FShopMatchState ReadShopMatchState(const UWorld* World)
{
    const AGameModeBase* Mode = World ? World->GetAuthGameMode() : nullptr;
    if (!IsValid(Mode)) return {};
    if (Mode->GetClass()->ImplementsInterface(UShopMatchProgress::StaticClass()))
        return IShopMatchProgress::Execute_GetShopMatchState(Mode);

    const AAutoBattleGameMode* Battle = Cast<AAutoBattleGameMode>(Mode);
    if (!Battle) return {};

    EShopPhase Phase = EShopPhase::Inactive;
    switch (Battle->CurrentPhase)
    {
        case EGamePhase::Preparation:
            if (Battle->bEncounterReady && Battle->bPreparationActive) Phase = EShopPhase::Preparation;
            break;
        case EGamePhase::Combat:
            Phase = Battle->bBattleResultPending ? EShopPhase::Settlement : EShopPhase::Combat;
            break;
        case EGamePhase::RoundClear: Phase = EShopPhase::Settlement; break;
        case EGamePhase::GameOver:
        case EGamePhase::Victory: Phase = EShopPhase::Finished; break;
    }
    return { Phase, Battle->CurrentRound, Battle->MaxBoardUnits };
}
