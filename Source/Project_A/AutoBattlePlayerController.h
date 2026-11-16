#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AutoBattlePlayerController.generated.h"

class ABoardTile;
class ABenchTile;
class ABoardManager;
class ABattleUnitBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSelectedUnitChangedEvent, ABattleUnitBase*, Unit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FUnitDragStateChangedEvent, ABattleUnitBase*, Unit,
    bool, bPending, bool, bDragging);

// [협업: 입력/BP/UI] 클릭·선택·드래그의 창구이며, 점유와 배치 정책은 BoardManager에 위임한다.
// BP 입력 그래프에서 누름 -> BeginUnitDrag, 놓음 -> EndUnitDrag, 취소 -> CancelUnitDrag를 연결한다.
// 경제·아이템·판매 처리는 이 클래스가 수행하지 않으며, 아래 API는 기물 드래그만 다룬다.
UCLASS()
class PROJECT_A_API AAutoBattlePlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AAutoBattlePlayerController();
    virtual void PlayerTick(float DeltaSeconds) override;

    // [UI 조회] 현재 선택 기물. 해제 시 nullptr이며, C++에서도 직접 대입하지 말고 SelectUnit/ClearSelection을 사용한다.
    // 직접 대입하면 드래그 취소와 OnSelectedUnitChanged 알림을 우회한다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABattleUnitBase* SelectedUnit = nullptr;

    // [UI/배치 표시] 매 틱 갱신되는 커서의 보드 타일. 유효한 타일을 맞히지 못하면 nullptr이다.
    // 일반 호버에서 유닛을 맞히면 타일이 잡히지 않을 수 있다. 유닛 프로필 호버 정보와는 별개이다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABoardTile* HoveredTile = nullptr;

    // [UI/배치 표시] 매 틱 갱신되는 대기석 호버. 잠금·영역 밖 등에서는 nullptr로 처리한다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection")
    ABenchTile* HoveredBenchSlot = nullptr;

    // [UI 조회] 이동 임계값을 넘은 실제 드래그만 true. 누름 대기까지 확인하려면 IsUnitDragActive를 사용한다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Selection|Drag")
    bool bIsUnitDragging = false;

    // [UI 연결] 선택/해제 알림이며 Unit == nullptr는 선택 해제이다.
    // 수신부는 표시 갱신 위주로 사용한다. 알림 안에서 다시 선택을 변경하면 중첩 알림 순서에 주의해야 한다.
    UPROPERTY(BlueprintAssignable, Category = "Selection")
    FSelectedUnitChangedEvent OnSelectedUnitChanged;

    // Pending begins on press; dragging begins only after the existing movement threshold.
    // [UI/진행 연결] (Unit, bPending, bDragging): 누름 대기=(true,false), 이동 중=(true,true), 종료=(false,false).
    // 드롭 경로의 종료 알림은 배치 처리가 끝난 뒤 전달한다. 파괴된 대상은 Unit == nullptr일 수 있다.
    // 알림 중 잠금·선택·드래그를 다시 변경하는 연계는 다음 프레임의 별도 흐름으로 분리하는 것을 권장한다.
    UPROPERTY(BlueprintAssignable, Category = "Selection|Drag")
    FUnitDragStateChangedEvent OnUnitDragStateChanged;

    // [진행/UI 조회] 기물 누름 대기와 실제 드래그를 함께 조회한다. 아이템 드래그 상태는 포함하지 않는다.
    UFUNCTION(BlueprintPure, Category = "Selection|Drag")
    bool IsUnitDragActive() const;

    // [맵/카메라 설정] 기존 BP 참조 호환용 태그. 현재 C++에서는 이 태그로 카메라를 자동 탐색·전환하지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    FName BattleCameraTag = TEXT("BattleCamera");

    // [BP 입력 연결] 월드 조작 허용 여부와 Canvas HUD HitBox를 검사한 뒤 Visibility 히트를 반환한다.
    // 반환된 액터의 유닛/타일/대기석 구분은 호출자가 수행한다. UMG 전체를 자동 판별하는 함수는 아니다.
    // 팝업·정산의 월드 입력 차단은 BoardManager.SetInteractionLocked 호출을 별도로 연결해야 한다.
    UFUNCTION(BlueprintCallable, Category = "Selection")
    bool GetBoardClickHit(FHitResult& Hit);

    // [UI/입력 연결] 등록된 조작 가능 아군을 선택한다. 다른 기물 선택 시 기존 드래그를 취소하며, 부적합한 입력은 선택을 해제한다.
    UFUNCTION(BlueprintCallable, Category = "Selection")
    void SelectUnit(ABattleUnitBase* Unit);

    // [UI/입력 연결] 진행 중인 드래그를 취소한 뒤 선택을 해제한다. 콜백에서 새 선택이 생기면 그 선택을 보호한다.
    UFUNCTION(BlueprintCallable, Category = "Selection")
    void ClearSelection();

    // [BP 입력 연결] 마우스 누름 시 호출한다. 등록·점유·단계·잠금 검사 실패 시 false이며, 아직 실제 이동은 시작하지 않는다.
    UFUNCTION(BlueprintCallable, Category = "Selection|Drag")
    bool BeginUnitDrag(ABattleUnitBase* Unit);

    // [BP 입력 연결] 마우스 놓음 시 호출한다. true는 배치/교환 성공이며, 단순 클릭 종료도 false를 반환한다.
    // 무효한 드롭은 원래 위치로 복귀한다. 이 함수에는 판매 영역 판정이나 판매 요청이 구현되어 있지 않다.
    UFUNCTION(BlueprintCallable, Category = "Selection|Drag")
    bool EndUnitDrag();

    // [BP 입력 연결] 취소·팝업 잠금 등에 사용한다. 다른 시스템이 이미 배치를 바꾼 경우 과거 위치로 덮어쓰지 않는다.
    UFUNCTION(BlueprintCallable, Category = "Selection|Drag")
    void CancelUnitDrag();

    // [입력 튜닝] 마우스 이동 거리 기준의 클릭/드래그 구분값. BP에서 별도 임계값 판정을 중복 구현하지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Drag",
        meta = (ClampMin = "0.0", UIMin = "0.0"))
    float UnitDragThreshold = 8.0f;

    // [맵/표현 튜닝] 드래그 표시 높이(cm). 배치 점유나 실제 타일 표면 높이를 바꾸는 값이 아니다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection|Drag",
        meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Additional height above the slot while dragging, in centimeters."))
    float UnitDragHeightOffset = 100.0f;

    // [BP 입력 연결] 선택 기물을 보드로 이동/교환한다. 내부적으로 드래그 종료 후 BoardManager를 호출하므로 점유를 별도로 수정하지 않는다.
    UFUNCTION(BlueprintCallable, Category = "Board")
    bool PlaceSelectedUnitOnTile(ABoardTile* Tile);

    // [BP 입력 연결] 선택 기물을 지정 대기석으로 이동/교환한다. 보드에서 내려간 기물의 아이템 연계는 BoardManager 이벤트를 사용한다.
    UFUNCTION(BlueprintCallable, Category = "Board")
    bool MoveSelectedUnitToBench(ABenchTile* Slot);

    // [진행 연결] 기물 드래그·드롭 처리·입력 잠금을 검사하고 배치를 저장한 뒤 GameMode에 시작을 요청한다.
    // 실제 단계 전환은 GameMode/BP 담당이다. GameMode를 직접 호출하면 이 경로의 저장/입력 검사를 우회한다.
    // 아이템 드래그 및 보상 지급 완료 여부는 소유 시스템에서 추가로 통제해야 한다.
    UFUNCTION(BlueprintCallable, Category = "Game")
    void RequestStartCombat();

    // [진행 연결] GameMode의 재시작 요청 이벤트로 전달한다. 보유 골드·아이템·잠금 사유 초기화는 각 소유 파트의 계약이 필요하다.
    UFUNCTION(BlueprintCallable, Category = "Game")
    void RequestRestart();

private:
    struct FScopedDragNotifications;
    int32 DragNotificationDepth = 0;
    bool bDragNotificationPending = false;
    TWeakObjectPtr<ABattleUnitBase> DragNotificationUnit;
    void NotifyUnitDragState(ABattleUnitBase* Unit);

    // Selection can change independently; only this unit owns the drag snapshot.
    TWeakObjectPtr<ABattleUnitBase> DraggedUnit;
    TWeakObjectPtr<ABoardTile> UnitDragStartTile;
    TWeakObjectPtr<ABenchTile> UnitDragStartBenchSlot;
    bool bUnitDragPending = false;
    uint32 SelectionRevision = 0;
    uint32 DragSessionRevision = 0;
    FVector2D UnitDragStartScreenPosition = FVector2D::ZeroVector;
    FTransform UnitDragStartTransform = FTransform::Identity;
    float UnitDragSurfaceZ = 0.0f;

    bool IsPointerOverHUD() const;
    bool IsUnitPlacementPhaseActive() const;
    bool IsUnitInteractionAllowed(ABattleUnitBase* Unit) const;
    void SetSelectedUnitInternal(ABattleUnitBase* Unit);
    ABoardManager* GetBoardManager() const;
    AActor* GetDragDropSlotAtCursor(FHitResult* OutHit = nullptr) const;
    void UpdateDraggedUnitLocation();
    bool IsDraggedUnitPlacementUnchanged() const;
};
