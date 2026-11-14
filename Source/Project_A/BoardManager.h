#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AutoBattleGameMode.h"
#include "BoardManager.generated.h"

class ABoardTile;
class ABenchTile;
class ABattleUnitBase;
class UMaterialInterface;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBoardGeneratedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUnitMovedToBenchEvent, ABattleUnitBase*, Unit);

// 보드·대기석 생성, 점유 검증, 배치/교환, 전투 전 배치 저장과 복귀
// 외부 파트는 공개 API를 사용하고 Tiles/BenchSlots, 타일 점유값, 유닛 CurrentTile/bIsDeployed를 직접 수정하지 않는다.
// 성장 연계: 수령 전 CanAcceptWaitingUnits -> 소유 시스템 등록 -> TrySynchronizeBenchOccupancy 결과 확인.
// 수용량 조회는 예약이 아니므로 실제 실패 시 구매·보상 지급의 취소/재시도는 호출자가 처리한다.
UCLASS()
class PROJECT_A_API ABoardManager : public AActor
{
    GENERATED_BODY()

public:
    ABoardManager();

protected:
    virtual void BeginPlay() override;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    USceneComponent* SceneRoot;

    // [맵/BP 설정] 생성할 보드 타일 BP. 자동 간격 사용 시 BP의 TileMesh에 메시가 지정되어 있어야 한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    TSubclassOf<ABoardTile> TileClass;

    // [맵/BP 설정] 생성할 대기석 BP. 표시 메시와 Visibility 충돌을 설정하며, 보드 생성 전에 지정한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Bench")
    TSubclassOf<ABenchTile> BenchTileClass;

    // [확정 규칙] 대기석 7칸. 현재 생성된 실물 슬롯 수는 BenchSlots와 수용량 API로 확인한다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board|Bench")
    int32 BenchSlotCount = 7;

    // [맵 연결] GameMode.WaitingAreaOffset에 더하는 대기석 배치 보정값. 생성 이후 값을 바꿔도 기존 슬롯이 자동 재배치되지는 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Bench",
        meta = (DisplayName = "Bench Start Offset", ToolTip = "Offset from the default first bench slot position."))
    FVector BenchAreaOffset = FVector::ZeroVector;

    // [맵 설정] 기본 6행 x 5열, 총 30칸. 출전 한도와 별개이며, 런타임 점유 중 규격 변경/재생성은 지원하지 않는다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Rows = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Columns = 5;

    // [맵/진영 연결] 음의 월드 Y 쪽부터 아군 행을 지정한다. 기본 3행이며, 나머지는 적 배치 영역이다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ClampMin = "0", ToolTip = "Number of player rows from the negative world Y side."))
    int32 PlayerRows = 3;

    // [아트 연결] 생성 시 아군 타일 메시의 머티리얼 슬롯 0에 적용한다. 호버/선택 표시 머티리얼과 별도 계약이 필요하다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Player tile material for mesh slot 0."))
    TObjectPtr<UMaterialInterface> PlayerTileMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Enemy tile material for mesh slot 0."))
    TObjectPtr<UMaterialInterface> EnemyTileMaterial = nullptr;

    // [맵/아트 연결] 첫 보드 타일 메시의 월드 바운드로 간격을 계산한다. 메시 크기/회전 변경 시 생성 결과를 확인한다.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing")
    bool bAutoCalculateSpacing = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.0", UIMax = "0.2", EditCondition = "bAutoCalculateSpacing"))
    float GapRatio = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.01", EditCondition = "!bAutoCalculateSpacing"))
    float HorizontalSpacing = 173.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Spacing",
        meta = (ClampMin = "0.01", EditCondition = "!bAutoCalculateSpacing"))
    float VerticalSpacing = 150.0f;

    // [전투/맵 조회] 마지막 생성 성공 시 확정한 월드 간격. 입력 설정값과 구분하며, 육각 거리·경로 탐색 API를 대신하지 않는다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveHorizontalSpacing = 0.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveVerticalSpacing = 0.0f;

    // [전투/성장 조회] 보드 타일 목록. 조회 전용으로 사용하고 좌표 조회는 GetTile을 권장한다. 재생성 시 기존 타일 참조는 무효화된다.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    TArray<ABoardTile*> Tiles;

    // [성장/UI 조회] 대기석 목록. 배열 인덱스와 SlotIndex를 혼동하지 말고 GetBenchSlot을 사용한다. 직접 Add/Remove/점유 수정 금지.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board|Bench")
    TArray<ABenchTile*> BenchSlots;

    // [맵/초기화 연결] BeginPlay에서도 호출된다. 등록된 유닛·점유자가 있으면 거부하며, 모든 새 타일 생성 성공 후 교체한다.
    // 타일 BP 생성/파괴 콜백에서 유닛 지급·등록·배치를 시작하지 않는다. 후속 연결은 OnBoardGenerated/진행 담당의 준비 완료 흐름을 따른다.
    UFUNCTION(BlueprintCallable, Category = "Board")
    void GenerateBoard();

    // [진행 연결] 전체 월드 조작 허용 스위치. false는 현재 선택/드래그도 해제한다. true로 바꿔도 사유별 잠금은 해제되지 않는다.
    UFUNCTION(BlueprintCallable, Category = "Board|Interaction")
    void SetPlacementEnabled(bool bEnabled);

    // Each owner must use its own stable reason and release only that reason. Repeated calls are idempotent.
    // [UI/정산 연결] 예: PopupInventory 또는 Settlement 사유로 열 때 true, 닫을 때 같은 사유로 false를 호출한다.
    // 여러 소유자/중첩 창은 서로 다른 사유를 사용해야 한다. 동일 사유는 참조 횟수를 세지 않는 중복 허용 스위치이다.
    // 재시작/화면 제거 시 해제 책임도 해당 소유자에게 있다. GameMode 직접 시작이나 경제 시스템을 자동 잠그지는 않는다.
    UFUNCTION(BlueprintCallable, Category = "Board|Interaction")
    void SetInteractionLocked(FName LockReason, bool bLocked);

    // [UI/진행 조회] 전체 스위치·사유별 잠금·보드 갱신 여부만 검사한다. 현재 게임 단계에서의 입력 허용은 CanProcessWorldInput을 사용한다.
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool IsPlacementEnabled() const;

    // [진행 조회] 준비 활성 상태 또는 RoundClear에서 배치 가능한지 확인한다. RoundClear 내 보상 선택/짧은 정산 구분은 외부 잠금으로 연결한다.
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool IsPlacementPhaseActive() const;

    // [입력/UI 조회] 단계·준비 완료·잠금의 공통 정책. 준비/보상 단계와 정산 대기가 아닌 전투 중 대기석 조작을 위한 입구이다.
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool CanProcessWorldInput() const;

    // [입력/성장 조회] 기물별 조작 가능 여부. 전투 중 출전 기물은 제외하고 대기 기물만 허용한다. 슬롯 수용 여부까지 보장하지 않는다.
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool CanInteractWithUnit(ABattleUnitBase* Unit) const;

    // [맵/진행 연결] 생성 성공 후 한 번 전달하며, 이 시점부터 새 타일 목록을 조회할 수 있다.
    // 이 이벤트 자체는 유닛 등록/Encounter 준비 완료를 뜻하지 않는다. GameMode의 기존 설정 흐름과 중복 생성하지 않는다.
    UPROPERTY(BlueprintAssignable, Category = "Board")
    FBoardGeneratedEvent OnBoardGenerated;

    // [전투/배치 조회] 0부터 시작하는 (Row, Column)으로 조회한다. 좌표가 없으면 nullptr이며, 배열 위치로 좌표를 추정하지 않는다.
    UFUNCTION(BlueprintPure, Category = "Board")
    ABoardTile* GetTile(int32 Row, int32 Column) const;

    // [보상/자동 배치 조회] 아군 영역 후보 목록만 반환한다. 빈칸·출전 한도 검사는 TryPlaceUnitOnTile에서 별도로 수행한다.
    UFUNCTION(BlueprintPure, Category = "Board")
    TArray<ABoardTile*> GetPlayerTiles() const;

    UFUNCTION(BlueprintPure, Category = "Board")
    TArray<ABoardTile*> GetEnemyTiles() const;

    // [성장/UI 조회] 0부터 시작하는 SlotIndex로 조회한다. 존재하지 않으면 nullptr이며, 빈칸 여부는 별도이다.
    UFUNCTION(BlueprintPure, Category = "Board|Bench")
    ABenchTile* GetBenchSlot(int32 SlotIndex) const;

    // Compatibility entry point; reports failures through LastBenchFailureReason and the log.
    // [기존 BP 호환] 반환값 없는 동기화. 실패는 LastBenchFailureReason/StatusMessage/로그로 확인한다. 신규 지급 흐름은 Try 버전을 권장한다.
    UFUNCTION(BlueprintCallable, Category = "Board|Bench")
    void SynchronizeBenchOccupancy();

    // Assign all registered waiting units, or leave the bench unchanged on validation/capacity failure.
    // [성장/보상 연결] 등록된 대기 유닛 전체를 검사·배정한다. 기존 정상 점유와 드래그 중인 기존 기물 위치는 유지한다.
    // 용량/상태 검사 실패는 변경 전 반환하고, 이동 중 실패는 원상 복구를 시도한다. 복구 불가 상황도 실패 사유로 반환한다.
    // 등록·골드·아이템·보상 완료 상태는 변경하지 않는다. false를 무시하고 지급 완료 처리하지 않는다.
    UFUNCTION(BlueprintCallable, Category = "Board|Bench")
    bool TrySynchronizeBenchOccupancy(FString& OutFailureReason);

    // Read-only queries, not reservations. Includes registered units still awaiting a bench assignment.
    // [구매/보상 사전 조회] AdditionalUnitCount는 앞으로 추가할 대기 기물 수이며, 이미 등록된 미배정 기물도 공간 계산에 포함된다.
    // 0은 현재 대기 유닛 전체의 배정 가능 검사이다. 자동 보드 배치/합성으로 실제 추가 수가 달라지면 성장 파트가 그 수를 결정한다.
    UFUNCTION(BlueprintPure, Category = "Board|Bench")
    bool CanAcceptWaitingUnits(int32 AdditionalUnitCount, FString& OutFailureReason) const;

    // Returns an actually empty slot not already needed by a registered waiting unit; may return null.
    // [성장 조회] 실제 비어 있고 기존 미배정 기물에게 필요하지 않은 슬롯만 반환한다. 공간 예약이나 점유 변경은 하지 않는다.
    // 정리 가능한 오래된 점유가 있으면 수용량 조회는 성공해도 이 함수는 nullptr일 수 있으므로, 등록 후 Try 동기화 결과를 기준으로 한다.
    UFUNCTION(BlueprintPure, Category = "Board|Bench")
    ABenchTile* GetFreeBenchSlot() const;

    // [UI/진단 조회] 마지막 Try 동기화 실패 이유. 성공 시 비운다. 순수 수용량 조회의 이유는 해당 OutFailureReason으로 받는다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Bench")
    FString LastBenchFailureReason;

    // [진행 연결] 준비/RoundClear에서 현재 살아 있는 출전 아군의 타일 참조를 저장한다. 검증 실패 시 이전 저장본을 유지한다.
    // 전투를 실제로 시작하는 소유 파트가 성공 여부를 확인해야 한다. 저장 파일/UnitId 기반 영구 보관 기능은 아니다.
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool SavePlayerFormation();

    // [진행/정산 연결] 결과 대기 또는 결과 단계에서 생존 기물을 저장 타일로 복귀시킨다. 사망 기물 부활·체력 회복은 수행하지 않는다.
    // 결과 단계 알림에서 자동 호출된다. false면 저장본을 남기고 LastFormationFailureReason을 확인하도록 한다.
    // 저장 유닛을 합성/판매/파괴하거나 새 Encounter로 교체하기 전에 복귀 순서를 진행/성장 파트와 맞춰야 한다.
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool RestorePlayerFormation();

    // Existence only; restoration validates referenced units/tiles before changing state.
    // [진행 조회] 미소비 저장본 존재 여부만 반환한다. 참조 유효성·유닛 수·복귀 성공까지 보장하는 값은 아니다.
    UFUNCTION(BlueprintPure, Category = "Board|Placement")
    bool HasSavedPlayerFormation() const;

    // [진행/UI 진단] 마지막 저장/복귀 실패 이유. 성공 시 비우며, 자동 복귀 실패도 이 값과 로그에 남긴다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Placement")
    FString LastFormationFailureReason;

    // [입력/배치 연결] 등록된 아군의 보드 이동/교환 진입점. 빈칸으로 추가 출전할 때만 GameMode.MaxBoardUnits를 검사한다.
    // 인원이 유지되는 교환은 한도에서도 허용한다. 배치 성공과 경제/아이템 처리 성공은 별개이며 false면 실패 이유를 확인한다.
    // BattleUnitBase.PlaceOnTile 직접 호출은 이 함수의 단계·한도·대기석 점유 검사를 우회한다.
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool TryPlaceUnitOnTile(ABattleUnitBase* Unit, ABoardTile* TargetTile);

    // [입력/회수 연결] 지정 슬롯으로 회수/대기석 이동/교환한다. 만석이어도 유효한 상대와의 교환은 가능하다.
    // 전투 중에는 대기석 기물끼리의 조작만 허용한다. 보드에서 내려간 경우에만 OnUnitMovedToBench를 알린다.
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool TryMoveUnitToBench(ABattleUnitBase* Unit, ABenchTile* TargetSlot);

    // Checks one inactive player unit at a stable placement boundary; does not repair state.
    // [협업 진단] 등록된 비전투 아군 1기의 보드/대기석 중복 점유와 양방향 연결을 검사한다. 상태를 자동 수리하거나 슬롯을 확보하지 않는다.
    UFUNCTION(BlueprintPure, Category = "Board|Placement")
    bool ValidateUnitPlacementState(ABattleUnitBase* Unit, FString& OutFailureReason) const;

    // [UI/진단 조회] 마지막 이동/교환 API 실패 이유. 정상 성공 시 비운다. 문자열 자체를 규칙 판정 키로 사용하지 않는다.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Placement")
    FString LastPlacementFailureReason;

    // [성장/아이템 연결] 보드에서 대기석으로 내려간 기물만 통지한다. 교환으로 내려간 상대도 포함하며, 대기석 내 이동에는 발생하지 않는다.
    // Roster/아이템 담당은 여기서 장착 해제·아이템창 반환을 연결한다. 반환 성공 여부를 받거나 경제 처리를 되돌리는 계약은 현재 없다.
    // 수신 시 점유는 이미 확정되어 있다. 아이템 처리와 별도로 선택을 변경한다면 Controller 공개 API를 사용한다.
    UPROPERTY(BlueprintAssignable, Category = "Board|Bench")
    FUnitMovedToBenchEvent OnUnitMovedToBench;

private:
    struct FUnitPlacementSnapshot;
    bool bPlacementInProgress = false;
    bool bBoardGenerationInProgress = false;
    bool bBoardGenerationNotificationInProgress = false;
    bool bPlacementEnabled = true;
    TSet<FName> InteractionLockReasons;

    bool GenerateBoardInternal();
    bool HasUnitsPreventingBoardGeneration() const;
    void CancelPlayerInteractions();

    bool CaptureUnitPlacement(ABattleUnitBase* Unit, FUnitPlacementSnapshot& OutPlacement,
        FString& OutFailureReason) const;
    bool ApplyUnitPlacement(ABattleUnitBase* Unit, ABoardTile* Tile, ABenchTile* BenchSlot);
    bool MoveOrSwapUnits(const FUnitPlacementSnapshot& Source, ABoardTile* TargetTile,
        ABenchTile* TargetSlot, ABattleUnitBase* OtherUnit, FString& OutFailureReason);
    bool RejectPlacement(const FString& Reason);
    bool RejectFormation(const FString& Reason);
    bool bHasSavedPlayerFormation = false;
    bool bFormationRestoreCompleted = false;

    UPROPERTY(Transient)
    TArray<TObjectPtr<ABattleUnitBase>> SavedDeployedUnits;

    UPROPERTY(Transient)
    TArray<TObjectPtr<ABoardTile>> SavedDeployedTiles;

    bool BuildBenchSynchronizationPlan(TArray<ABattleUnitBase*>& OutWaitingUnits,
        TArray<ABenchTile*>& OutAssignedSlots, FString& OutFailureReason) const;
    bool IsUnitMovementPhaseActive(const AAutoBattleGameMode* Mode) const;

    UFUNCTION()
    void HandleGamePhaseChanged(EGamePhase Phase);
};
