#include "ShopMatchProgress.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/World.h"

FShopMatchState ReadShopMatchState(const UWorld* World)
{
    const AGameModeBase* Mode = World ? World->GetAuthGameMode() : nullptr;
    return IsValid(Mode) && Mode->GetClass()->ImplementsInterface(UShopMatchProgress::StaticClass())
        ? IShopMatchProgress::Execute_GetShopMatchState(Mode) : FShopMatchState();
}
