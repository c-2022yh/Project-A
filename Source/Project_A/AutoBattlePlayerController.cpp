#include "AutoBattlePlayerController.h"

#include "BoardTile.h"
#include "BattleUnitBase.h"
#include "Components/CapsuleComponent.h"

AAutoBattlePlayerController::AAutoBattlePlayerController()
{
    //마우스 커서 표시
    bShowMouseCursor = true;

    //클릭 이벤트 사용
    bEnableClickEvents = true;

    //마우스 오버 이벤트 사용
    bEnableMouseOverEvents = true;
}

void AAutoBattlePlayerController::BeginPlay()
{
    Super::BeginPlay();

    //게임 + UI 입력을 모두 받을 수 있게 설정
    FInputModeGameAndUI InputMode;
    SetInputMode(InputMode);
}

void AAutoBattlePlayerController::SelectUnit(ABattleUnitBase* Unit)
{
    if (Unit == nullptr)
    {
        ClearSelection();
        return;
    }

    SelectedUnit = Unit;
}

void AAutoBattlePlayerController::ClearSelection()
{
    SelectedUnit = nullptr;
}

bool AAutoBattlePlayerController::PlaceSelectedUnitOnTile(ABoardTile* Tile)
{
    if (SelectedUnit == nullptr || Tile == nullptr)
    {
        return false;
    }

    //이미 다른 유닛이 있는 타일이면 배치 불가
    if (!Tile->CanPlaceUnit())
    {
        return false;
    }

    //현재는 플레이어 진영 타일에만 배치 가능
    if (Tile->TeamType != EBoardTileTeam::Player)
    {
        return false;
    }

    // 타일 중앙으로 이동
    FVector TargetLocation = Tile->GetActorLocation();

    //캐릭터 캡슐이 타일 안으로 박히지 않도록
    //캡슐 절반 높이만큼 위로 올린다.
    TargetLocation.Z += SelectedUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

    SelectedUnit->SetActorLocation(TargetLocation);

    Tile->SetOccupiedUnit(SelectedUnit);

    ClearSelection();

    return true;
}