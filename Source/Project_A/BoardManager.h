#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AutoBattleGameMode.h"
#include "UnitTypes.h"
#include "BoardManager.generated.h"

class ABoardTile;
class ABenchTile;
class ABattleUnitBase;
class UMaterialInterface;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBoardGeneratedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUnitMovedToBenchEvent, ABattleUnitBase*, Unit);

// ë³´ë“œÂ·?€ê¸°ì„ ?ì„±, ?ìœ  ê²€ì¦? ë°°ì¹˜/êµí™˜, ?„íˆ¬ ??ë°°ì¹˜ ?€?¥ê³¼ ë³µê?
// ?¸ë? ?ŒíŠ¸??ê³µê°œ APIë¥??¬ìš©?˜ê³  Tiles/BenchSlots, ?€???ìœ ê°? ? ë‹› CurrentTile/bIsDeployedë¥?ì§ì ‘ ?˜ì •?˜ì? ?ŠëŠ”??
// ?±ì¥ ?°ê³„: ?˜ë ¹ ??CanAcceptWaitingUnits -> ?Œìœ  ?œìŠ¤???±ë¡ -> TrySynchronizeBenchOccupancy ê²°ê³¼ ?•ì¸.
// ?˜ìš©??ì¡°íšŒ???ˆì•½???„ë‹ˆë¯€ë¡??¤ì œ ?¤íŒ¨ ??êµ¬ë§¤Â·ë³´ìƒ ì§€ê¸‰ì˜ ì·¨ì†Œ/?¬ì‹œ?„ëŠ” ?¸ì¶œ?ê? ì²˜ë¦¬?œë‹¤.
UCLASS()
class PROJECT_A_API ABoardManager : public AActor
{
    GENERATED_BODY()

public:
    ABoardManager();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    USceneComponent* SceneRoot;

    // [ë§?BP ?¤ì •] ?ì„±??ë³´ë“œ ?€??BP. ?ë™ ê°„ê²© ?¬ìš© ??BP??TileMesh??ë©”ì‹œê°€ ì§€?•ë˜???ˆì–´???œë‹¤.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    TSubclassOf<ABoardTile> TileClass;

    // [ë§?BP ?¤ì •] ?ì„±???€ê¸°ì„ BP. ?œì‹œ ë©”ì‹œ?€ Visibility ì¶©ëŒ???¤ì •?˜ë©°, ë³´ë“œ ?ì„± ?„ì— ì§€?•í•œ??
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Bench")
    TSubclassOf<ABenchTile> BenchTileClass;

    // [?•ì • ê·œì¹™] ?€ê¸°ì„ 7ì¹? ?„ì¬ ?ì„±???¤ë¬¼ ?¬ë¡¯ ?˜ëŠ” BenchSlots?€ ?˜ìš©??APIë¡??•ì¸?œë‹¤.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board|Bench")
    int32 BenchSlotCount = 7;

    // [ë§??°ê²°] GameMode.WaitingAreaOffset???”í•˜???€ê¸°ì„ ë°°ì¹˜ ë³´ì •ê°? ?ì„± ?´í›„ ê°’ì„ ë°”ê¿”??ê¸°ì¡´ ?¬ë¡¯???ë™ ?¬ë°°ì¹˜ë˜ì§€???ŠëŠ”??
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Bench",
        meta = (DisplayName = "Bench Start Offset", ToolTip = "Offset from the default first bench slot position."))
    FVector BenchAreaOffset = FVector::ZeroVector;

    // [ë§??¤ì •] ê¸°ë³¸ 6??x 5?? ì´?30ì¹? ì¶œì „ ?œë„?€ ë³„ê°œ?´ë©°, ?°í????ìœ  ì¤?ê·œê²© ë³€ê²??¬ìƒ?±ì? ì§€?í•˜ì§€ ?ŠëŠ”??
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Rows = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board")
    int32 Columns = 5;

    // [ë§?ì§„ì˜ ?°ê²°] ?Œì˜ ?”ë“œ Y ìª½ë????„êµ° ?‰ì„ ì§€?•í•œ?? ê¸°ë³¸ 3?‰ì´ë©? ?˜ë¨¸ì§€????ë°°ì¹˜ ?ì—­?´ë‹¤.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ClampMin = "0", ToolTip = "Number of player rows from the negative world Y side."))
    int32 PlayerRows = 3;

    // [?„íŠ¸ ?°ê²°] ?ì„± ???„êµ° ?€??ë©”ì‹œ??ë¨¸í‹°ë¦¬ì–¼ ?¬ë¡¯ 0???ìš©?œë‹¤. ?¸ë²„/? íƒ ?œì‹œ ë¨¸í‹°ë¦¬ì–¼ê³?ë³„ë„ ê³„ì•½???„ìš”?˜ë‹¤.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Player tile material for mesh slot 0."))
    TObjectPtr<UMaterialInterface> PlayerTileMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Board|Teams",
        meta = (ToolTip = "Enemy tile material for mesh slot 0."))
    TObjectPtr<UMaterialInterface> EnemyTileMaterial = nullptr;

    // [ë§??„íŠ¸ ?°ê²°] ì²?ë³´ë“œ ?€??ë©”ì‹œ???”ë“œ ë°”ìš´?œë¡œ ê°„ê²©??ê³„ì‚°?œë‹¤. ë©”ì‹œ ?¬ê¸°/?Œì „ ë³€ê²????ì„± ê²°ê³¼ë¥??•ì¸?œë‹¤.
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

    // [?„íˆ¬/ë§?ì¡°íšŒ] ë§ˆì?ë§??ì„± ?±ê³µ ???•ì •???”ë“œ ê°„ê²©. ?…ë ¥ ?¤ì •ê°’ê³¼ êµ¬ë¶„?˜ë©°, ?¡ê° ê±°ë¦¬Â·ê²½ë¡œ ?ìƒ‰ APIë¥??€? í•˜ì§€ ?ŠëŠ”??
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveHorizontalSpacing = 0.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Spacing")
    float EffectiveVerticalSpacing = 0.0f;

    // [?„íˆ¬/?±ì¥ ì¡°íšŒ] ë³´ë“œ ?€??ëª©ë¡. ì¡°íšŒ ?„ìš©?¼ë¡œ ?¬ìš©?˜ê³  ì¢Œí‘œ ì¡°íšŒ??GetTile??ê¶Œì¥?œë‹¤. ?¬ìƒ????ê¸°ì¡´ ?€??ì°¸ì¡°??ë¬´íš¨?”ëœ??
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board")
    TArray<ABoardTile*> Tiles;

    // [?±ì¥/UI ì¡°íšŒ] ?€ê¸°ì„ ëª©ë¡. ë°°ì—´ ?¸ë±?¤ì? SlotIndexë¥??¼ë™?˜ì? ë§ê³  GetBenchSlot???¬ìš©?œë‹¤. ì§ì ‘ Add/Remove/?ìœ  ?˜ì • ê¸ˆì?.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Board|Bench")
    TArray<ABenchTile*> BenchSlots;

    // [ë§?ì´ˆê¸°???°ê²°] BeginPlay?ì„œ???¸ì¶œ?œë‹¤. ?±ë¡??? ë‹›Â·?ìœ ?ê? ?ˆìœ¼ë©?ê±°ë??˜ë©°, ëª¨ë“  ???€???ì„± ?±ê³µ ??êµì²´?œë‹¤.
    // ?€??BP ?ì„±/?Œê´´ ì½œë°±?ì„œ ? ë‹› ì§€ê¸‰Â·ë“±ë¡Â·ë°°ì¹˜ë? ?œì‘?˜ì? ?ŠëŠ”?? ?„ì† ?°ê²°?€ OnBoardGenerated/ì§„í–‰ ?´ë‹¹??ì¤€ë¹??„ë£Œ ?ë¦„???°ë¥¸??
    UFUNCTION(BlueprintCallable, Category = "Board")
    void GenerateBoard();

    // [ì§„í–‰ ?°ê²°] ?„ì²´ ?”ë“œ ì¡°ì‘ ?ˆìš© ?¤ìœ„ì¹? false???„ì¬ ? íƒ/?œë˜ê·¸ë„ ?´ì œ?œë‹¤. trueë¡?ë°”ê¿”???¬ìœ ë³?? ê¸ˆ?€ ?´ì œ?˜ì? ?ŠëŠ”??
    UFUNCTION(BlueprintCallable, Category = "Board|Interaction")
    void SetPlacementEnabled(bool bEnabled);

    // Each owner must use its own stable reason and release only that reason. Repeated calls are idempotent.
    // [UI/?•ì‚° ?°ê²°] ?? PopupInventory ?ëŠ” Settlement ?¬ìœ ë¡?????true, ?«ì„ ??ê°™ì? ?¬ìœ ë¡?falseë¥??¸ì¶œ?œë‹¤.
    // ?¬ëŸ¬ ?Œìœ ??ì¤‘ì²© ì°½ì? ?œë¡œ ?¤ë¥¸ ?¬ìœ ë¥??¬ìš©?´ì•¼ ?œë‹¤. ?™ì¼ ?¬ìœ ??ì°¸ì¡° ?Ÿìˆ˜ë¥??¸ì? ?ŠëŠ” ì¤‘ë³µ ?ˆìš© ?¤ìœ„ì¹˜ì´??
    // ?¬ì‹œ???”ë©´ ?œê±° ???´ì œ ì±…ì„???´ë‹¹ ?Œìœ ?ì—ê²??ˆë‹¤. GameMode ì§ì ‘ ?œì‘?´ë‚˜ ê²½ì œ ?œìŠ¤?œì„ ?ë™ ? ê·¸ì§€???ŠëŠ”??
    UFUNCTION(BlueprintCallable, Category = "Board|Interaction")
    void SetInteractionLocked(FName LockReason, bool bLocked);

    // [UI/ì§„í–‰ ì¡°íšŒ] ?„ì²´ ?¤ìœ„ì¹˜Â·ì‚¬? ë³„ ? ê¸ˆÂ·ë³´ë“œ ê°±ì‹  ?¬ë?ë§?ê²€?¬í•œ?? ?„ì¬ ê²Œì„ ?¨ê³„?ì„œ???…ë ¥ ?ˆìš©?€ CanProcessWorldInput???¬ìš©?œë‹¤.
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool IsPlacementEnabled() const;

    // [ì§„í–‰ ì¡°íšŒ] ì¤€ë¹??œì„± ?íƒœ ?ëŠ” RoundClear?ì„œ ë°°ì¹˜ ê°€?¥í•œì§€ ?•ì¸?œë‹¤. RoundClear ??ë³´ìƒ ? íƒ/ì§§ì? ?•ì‚° êµ¬ë¶„?€ ?¸ë? ? ê¸ˆ?¼ë¡œ ?°ê²°?œë‹¤.
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool IsPlacementPhaseActive() const;

    // [?…ë ¥/UI ì¡°íšŒ] ?¨ê³„Â·ì¤€ë¹??„ë£ŒÂ·? ê¸ˆ??ê³µí†µ ?•ì±…. ì¤€ë¹?ë³´ìƒ ?¨ê³„?€ ?•ì‚° ?€ê¸°ê? ?„ë‹Œ ?„íˆ¬ ì¤??€ê¸°ì„ ì¡°ì‘???„í•œ ?…êµ¬?´ë‹¤.
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool CanProcessWorldInput() const;

    // [?…ë ¥/?±ì¥ ì¡°íšŒ] ê¸°ë¬¼ë³?ì¡°ì‘ ê°€???¬ë?. ?„íˆ¬ ì¤?ì¶œì „ ê¸°ë¬¼?€ ?œì™¸?˜ê³  ?€ê¸?ê¸°ë¬¼ë§??ˆìš©?œë‹¤. ?¬ë¡¯ ?˜ìš© ?¬ë?ê¹Œì? ë³´ì¥?˜ì? ?ŠëŠ”??
    UFUNCTION(BlueprintPure, Category = "Board|Interaction")
    bool CanInteractWithUnit(ABattleUnitBase* Unit) const;

    // [ë§?ì§„í–‰ ?°ê²°] ?ì„± ?±ê³µ ????ë²??„ë‹¬?˜ë©°, ???œì ë¶€?????€??ëª©ë¡??ì¡°íšŒ?????ˆë‹¤.
    // ???´ë²¤???ì²´??? ë‹› ?±ë¡/Encounter ì¤€ë¹??„ë£Œë¥??»í•˜ì§€ ?ŠëŠ”?? GameMode??ê¸°ì¡´ ?¤ì • ?ë¦„ê³?ì¤‘ë³µ ?ì„±?˜ì? ?ŠëŠ”??
    UPROPERTY(BlueprintAssignable, Category = "Board")
    FBoardGeneratedEvent OnBoardGenerated;

    // [?„íˆ¬/ë°°ì¹˜ ì¡°íšŒ] 0ë¶€???œì‘?˜ëŠ” (Row, Column)?¼ë¡œ ì¡°íšŒ?œë‹¤. ì¢Œí‘œê°€ ?†ìœ¼ë©?nullptr?´ë©°, ë°°ì—´ ?„ì¹˜ë¡?ì¢Œí‘œë¥?ì¶”ì •?˜ì? ?ŠëŠ”??
    UFUNCTION(BlueprintPure, Category = "Board")
    ABoardTile* GetTile(int32 Row, int32 Column) const;

    // [ë³´ìƒ/?ë™ ë°°ì¹˜ ì¡°íšŒ] ?„êµ° ?ì—­ ?„ë³´ ëª©ë¡ë§?ë°˜í™˜?œë‹¤. ë¹ˆì¹¸Â·ì¶œì „ ?œë„ ê²€?¬ëŠ” TryPlaceUnitOnTile?ì„œ ë³„ë„ë¡??˜í–‰?œë‹¤.
    UFUNCTION(BlueprintPure, Category = "Board")
    TArray<ABoardTile*> GetPlayerTiles() const;

    UFUNCTION(BlueprintPure, Category = "Board")
    TArray<ABoardTile*> GetEnemyTiles() const;

    // [?±ì¥/UI ì¡°íšŒ] 0ë¶€???œì‘?˜ëŠ” SlotIndexë¡?ì¡°íšŒ?œë‹¤. ì¡´ì¬?˜ì? ?Šìœ¼ë©?nullptr?´ë©°, ë¹ˆì¹¸ ?¬ë???ë³„ë„?´ë‹¤.
    UFUNCTION(BlueprintPure, Category = "Board|Bench")
    ABenchTile* GetBenchSlot(int32 SlotIndex) const;

    // Compatibility entry point; reports failures through LastBenchFailureReason and the log.
    // [ê¸°ì¡´ BP ?¸í™˜] ë°˜í™˜ê°??†ëŠ” ?™ê¸°?? ?¤íŒ¨??LastBenchFailureReason/StatusMessage/ë¡œê·¸ë¡??•ì¸?œë‹¤. ? ê·œ ì§€ê¸??ë¦„?€ Try ë²„ì „??ê¶Œì¥?œë‹¤.
    UFUNCTION(BlueprintCallable, Category = "Board|Bench")
    void SynchronizeBenchOccupancy();

    // Assign all registered waiting units, or leave the bench unchanged on validation/capacity failure.
    // [?±ì¥/ë³´ìƒ ?°ê²°] ?±ë¡???€ê¸?? ë‹› ?„ì²´ë¥?ê²€??·ë°°?•í•œ?? ê¸°ì¡´ ?•ìƒ ?ìœ ?€ ?œë˜ê·?ì¤‘ì¸ ê¸°ì¡´ ê¸°ë¬¼ ?„ì¹˜??? ì??œë‹¤.
    // ?©ëŸ‰/?íƒœ ê²€???¤íŒ¨??ë³€ê²???ë°˜í™˜?˜ê³ , ?´ë™ ì¤??¤íŒ¨???ìƒ ë³µêµ¬ë¥??œë„?œë‹¤. ë³µêµ¬ ë¶ˆê? ?í™©???¤íŒ¨ ?¬ìœ ë¡?ë°˜í™˜?œë‹¤.
    // ?±ë¡Â·ê³¨ë“œÂ·?„ì´?œÂ·ë³´???„ë£Œ ?íƒœ??ë³€ê²½í•˜ì§€ ?ŠëŠ”?? falseë¥?ë¬´ì‹œ?˜ê³  ì§€ê¸??„ë£Œ ì²˜ë¦¬?˜ì? ?ŠëŠ”??
    UFUNCTION(BlueprintCallable, Category = "Board|Bench")
    bool TrySynchronizeBenchOccupancy(FString& OutFailureReason);

    // Read-only queries, not reservations. Includes registered units still awaiting a bench assignment.
    // [êµ¬ë§¤/ë³´ìƒ ?¬ì „ ì¡°íšŒ] AdditionalUnitCount???ìœ¼ë¡?ì¶”ê????€ê¸?ê¸°ë¬¼ ?˜ì´ë©? ?´ë? ?±ë¡??ë¯¸ë°°??ê¸°ë¬¼??ê³µê°„ ê³„ì‚°???¬í•¨?œë‹¤.
    // 0?€ ?„ì¬ ?€ê¸?? ë‹› ?„ì²´??ë°°ì • ê°€??ê²€?¬ì´?? ?ë™ ë³´ë“œ ë°°ì¹˜/?©ì„±?¼ë¡œ ?¤ì œ ì¶”ê? ?˜ê? ?¬ë¼ì§€ë©??±ì¥ ?ŒíŠ¸ê°€ ê·??˜ë? ê²°ì •?œë‹¤.
    UFUNCTION(BlueprintPure, Category = "Board|Bench")
    bool CanAcceptWaitingUnits(int32 AdditionalUnitCount, FString& OutFailureReason) const;

    // Returns an actually empty slot not already needed by a registered waiting unit; may return null.
    // [?±ì¥ ì¡°íšŒ] ?¤ì œ ë¹„ì–´ ?ˆê³  ê¸°ì¡´ ë¯¸ë°°??ê¸°ë¬¼?ê²Œ ?„ìš”?˜ì? ?Šì? ?¬ë¡¯ë§?ë°˜í™˜?œë‹¤. ê³µê°„ ?ˆì•½?´ë‚˜ ?ìœ  ë³€ê²½ì? ?˜ì? ?ŠëŠ”??
    // ?•ë¦¬ ê°€?¥í•œ ?¤ë˜???ìœ ê°€ ?ˆìœ¼ë©??˜ìš©??ì¡°íšŒ???±ê³µ?´ë„ ???¨ìˆ˜??nullptr?????ˆìœ¼ë¯€ë¡? ?±ë¡ ??Try ?™ê¸°??ê²°ê³¼ë¥?ê¸°ì??¼ë¡œ ?œë‹¤.
    UFUNCTION(BlueprintPure, Category = "Board|Bench")
    ABenchTile* GetFreeBenchSlot() const;

    // [UI/ì§„ë‹¨ ì¡°íšŒ] ë§ˆì?ë§?Try ?™ê¸°???¤íŒ¨ ?´ìœ . ?±ê³µ ??ë¹„ìš´?? ?œìˆ˜ ?˜ìš©??ì¡°íšŒ???´ìœ ???´ë‹¹ OutFailureReason?¼ë¡œ ë°›ëŠ”??
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Bench")
    FString LastBenchFailureReason;

    // [ì§„í–‰ ?°ê²°] ì¤€ë¹?RoundClear?ì„œ ?„ì¬ ?´ì•„ ?ˆëŠ” ì¶œì „ ?„êµ°???€??ì°¸ì¡°ë¥??€?¥í•œ?? ê²€ì¦??¤íŒ¨ ???´ì „ ?€?¥ë³¸??? ì??œë‹¤.
    // ?„íˆ¬ë¥??¤ì œë¡??œì‘?˜ëŠ” ?Œìœ  ?ŒíŠ¸ê°€ ?±ê³µ ?¬ë?ë¥??•ì¸?´ì•¼ ?œë‹¤. ?€???Œì¼/UnitId ê¸°ë°˜ ?êµ¬ ë³´ê? ê¸°ëŠ¥?€ ?„ë‹ˆ??
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool SavePlayerFormation();

    // [ì§„í–‰/?•ì‚° ?°ê²°] ê²°ê³¼ ?€ê¸??ëŠ” ê²°ê³¼ ?¨ê³„?ì„œ ?ì¡´ ê¸°ë¬¼???€???€?¼ë¡œ ë³µê??œí‚¨?? ?¬ë§ ê¸°ë¬¼ ë¶€?œÂ·ì²´???Œë³µ?€ ?˜í–‰?˜ì? ?ŠëŠ”??
    // ê²°ê³¼ ?¨ê³„ ?Œë¦¼?ì„œ ?ë™ ?¸ì¶œ?œë‹¤. falseë©??€?¥ë³¸???¨ê¸°ê³?LastFormationFailureReason???•ì¸?˜ë„ë¡??œë‹¤.
    // ?€??? ë‹›???©ì„±/?ë§¤/?Œê´´?˜ê±°????Encounterë¡?êµì²´?˜ê¸° ?„ì— ë³µê? ?œì„œë¥?ì§„í–‰/?±ì¥ ?ŒíŠ¸?€ ë§ì¶°???œë‹¤.
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool RestorePlayerFormation();

    // Existence only; restoration validates referenced units/tiles before changing state.
    // [ì§„í–‰ ì¡°íšŒ] ë¯¸ì†Œë¹??€?¥ë³¸ ì¡´ì¬ ?¬ë?ë§?ë°˜í™˜?œë‹¤. ì°¸ì¡° ? íš¨?±Â·ìœ ???˜Â·ë³µê·€ ?±ê³µê¹Œì? ë³´ì¥?˜ëŠ” ê°’ì? ?„ë‹ˆ??
    UFUNCTION(BlueprintPure, Category = "Board|Placement")
    bool HasSavedPlayerFormation() const;

    // [ì§„í–‰/UI ì§„ë‹¨] ë§ˆì?ë§??€??ë³µê? ?¤íŒ¨ ?´ìœ . ?±ê³µ ??ë¹„ìš°ë©? ?ë™ ë³µê? ?¤íŒ¨????ê°’ê³¼ ë¡œê·¸???¨ê¸´??
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Placement")
    FString LastFormationFailureReason;

    // [?…ë ¥/ë°°ì¹˜ ?°ê²°] ?±ë¡???„êµ°??ë³´ë“œ ?´ë™/êµí™˜ ì§„ì…?? ë¹ˆì¹¸?¼ë¡œ ì¶”ê? ì¶œì „???Œë§Œ GameMode.MaxBoardUnitsë¥?ê²€?¬í•œ??
    // ?¸ì›??? ì??˜ëŠ” êµí™˜?€ ?œë„?ì„œ???ˆìš©?œë‹¤. ë°°ì¹˜ ?±ê³µê³?ê²½ì œ/?„ì´??ì²˜ë¦¬ ?±ê³µ?€ ë³„ê°œ?´ë©° falseë©??¤íŒ¨ ?´ìœ ë¥??•ì¸?œë‹¤.
    // BattleUnitBase.PlaceOnTile ì§ì ‘ ?¸ì¶œ?€ ???¨ìˆ˜???¨ê³„Â·?œë„Â·?€ê¸°ì„ ?ìœ  ê²€?¬ë? ?°íšŒ?œë‹¤.
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool TryPlaceUnitOnTile(ABattleUnitBase* Unit, ABoardTile* TargetTile);

    // [?…ë ¥/?Œìˆ˜ ?°ê²°] ì§€???¬ë¡¯?¼ë¡œ ?Œìˆ˜/?€ê¸°ì„ ?´ë™/êµí™˜?œë‹¤. ë§Œì„?´ì–´??? íš¨???ë??€??êµí™˜?€ ê°€?¥í•˜??
    // ?„íˆ¬ ì¤‘ì—???€ê¸°ì„ ê¸°ë¬¼?¼ë¦¬??ì¡°ì‘ë§??ˆìš©?œë‹¤. ë³´ë“œ?ì„œ ?´ë ¤ê°?ê²½ìš°?ë§Œ OnUnitMovedToBenchë¥??Œë¦°??
    UFUNCTION(BlueprintCallable, Category = "Board|Placement")
    bool TryMoveUnitToBench(ABattleUnitBase* Unit, ABenchTile* TargetSlot);

    // Checks one inactive player unit at a stable placement boundary; does not repair state.
    // [?‘ì—… ì§„ë‹¨] ?±ë¡??ë¹„ì „???„êµ° 1ê¸°ì˜ ë³´ë“œ/?€ê¸°ì„ ì¤‘ë³µ ?ìœ ?€ ?‘ë°©???°ê²°??ê²€?¬í•œ?? ?íƒœë¥??ë™ ?˜ë¦¬?˜ê±°???¬ë¡¯???•ë³´?˜ì? ?ŠëŠ”??
    UFUNCTION(BlueprintPure, Category = "Board|Placement")
    bool ValidateUnitPlacementState(ABattleUnitBase* Unit, FString& OutFailureReason) const;

    // [UI/ì§„ë‹¨ ì¡°íšŒ] ë§ˆì?ë§??´ë™/êµí™˜ API ?¤íŒ¨ ?´ìœ . ?•ìƒ ?±ê³µ ??ë¹„ìš´?? ë¬¸ì???ì²´ë¥?ê·œì¹™ ?ì • ?¤ë¡œ ?¬ìš©?˜ì? ?ŠëŠ”??
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Board|Placement")
    FString LastPlacementFailureReason;

    // [?±ì¥/?„ì´???°ê²°] ë³´ë“œ?ì„œ ?€ê¸°ì„?¼ë¡œ ?´ë ¤ê°?ê¸°ë¬¼ë§??µì??œë‹¤. êµí™˜?¼ë¡œ ?´ë ¤ê°??ë????¬í•¨?˜ë©°, ?€ê¸°ì„ ???´ë™?ëŠ” ë°œìƒ?˜ì? ?ŠëŠ”??
    // Roster/?„ì´???´ë‹¹?€ ?¬ê¸°???¥ì°© ?´ì œÂ·?„ì´?œì°½ ë°˜í™˜???°ê²°?œë‹¤. ë°˜í™˜ ?±ê³µ ?¬ë?ë¥?ë°›ê±°??ê²½ì œ ì²˜ë¦¬ë¥??˜ëŒë¦¬ëŠ” ê³„ì•½?€ ?„ì¬ ?†ë‹¤.
    // ?˜ì‹  ???ìœ ???´ë? ?•ì •?˜ì–´ ?ˆë‹¤. ?„ì´??ì²˜ë¦¬?€ ë³„ë„ë¡?? íƒ??ë³€ê²½í•œ?¤ë©´ Controller ê³µê°œ APIë¥??¬ìš©?œë‹¤.
    UPROPERTY(BlueprintAssignable, Category = "Board|Bench")
    FUnitMovedToBenchEvent OnUnitMovedToBench;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Board|Movement", meta=(ClampMin="0.1"))
    float UnstartedReservationTimeout = 2.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Board|Validation")
    FString LastMovementMessage;

    UFUNCTION(BlueprintPure, Category="Board|Path") ABoardTile* GetTileByHex(int32 Q, int32 R) const;
    UFUNCTION(BlueprintPure, Category="Board|Path") TArray<ABoardTile*> GetNeighbors(ABoardTile* Tile) const;
    ABoardTile* FindCombatLandingTile(const ABattleUnitBase* Unit, const ABattleUnitBase* Target) const;
    bool TryCombatLanding(ABattleUnitBase* Unit, ABattleUnitBase* Target, int32 ActionId);
    UFUNCTION(BlueprintPure, Category="Board|Units") TArray<ABattleUnitBase*> GetUnitsOnBoard() const;
    // °æ·Î¿¡´Â Ãâ¹ß Ä­À» Á¦¿ÜÇÏ°í µµÂø Ä­±îÁö ´ã´Â´Ù. ÀÌ¹Ì »ç°Å¸® ¾ÈÀÌ¸é ºó °æ·Î·Î ¼º°øÇÑ´Ù.
    UFUNCTION(BlueprintCallable, Category="Board|Path")
    bool FindPathToAttackRange(ABattleUnitBase* Unit, ABattleUnitBase* Target, TArray<ABoardTile*>& Path) const;
    UFUNCTION(BlueprintCallable, Category="Board|Movement")
    EUnitPathResult TryReserveNextStep(ABattleUnitBase* Unit, ABattleUnitBase* Target, int32 MoveId, ABoardTile*& NextTile);
    UFUNCTION(BlueprintPure, Category="Board|Movement")
    bool IsMoveReservationValid(ABattleUnitBase* Unit, int32 MoveId, ABoardTile* Destination) const;
    UFUNCTION(BlueprintCallable, Category="Board|Movement") bool CommitReservedMove(ABattleUnitBase* Unit, int32 MoveId);
    UFUNCTION(BlueprintCallable, Category="Board|Movement") bool CancelReservedMove(ABattleUnitBase* Unit, int32 MoveId);
    UFUNCTION(BlueprintCallable, Category="Board|Movement") void ReleaseUnitTiles(ABattleUnitBase* Unit);
    // ±âÁ¸ Tile.SetOccupiedUnitµµ ÀÌ ÁØºñ ¹èÄ¡ °ËÁõÀ» »ç¿ëÇÑ´Ù. ÀüÅõ ÀÌµ¿Àº Commit¸¸ Çã¿ëÇÑ´Ù.
    UFUNCTION(BlueprintCallable, Category="Board|Placement") bool TryPlaceUnit(ABattleUnitBase* Unit, ABoardTile* Tile);
    UFUNCTION(BlueprintCallable, Category="Board|Movement") void PruneInvalidReservations();

private:

    friend class ABoardTile;
    struct FMoveReservation
    {
        TWeakObjectPtr<ABattleUnitBase> Unit;
        TWeakObjectPtr<ABoardTile> Source;
        TWeakObjectPtr<ABoardTile> Destination;
        int32 MoveId = 0;
        double CreatedAt = 0.0;
    };
    TMap<TWeakObjectPtr<ABattleUnitBase>, FMoveReservation> Reservations;
    bool bReleasingUnits = false;
    bool OwnsTile(const ABoardTile* Tile) const;
    bool BuildPath(ABattleUnitBase* Unit, ABattleUnitBase* Target, bool bIgnoreUnits, TArray<ABoardTile*>& Path) const;
    void ClearTileOccupancy(ABoardTile* Tile);
    void NotifyTileRemoved(ABoardTile* Tile);

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
