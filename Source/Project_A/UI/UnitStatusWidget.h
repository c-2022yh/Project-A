#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UnitStatusWidget.generated.h"

class ABattleUnitBase;
class UTexture2D;
struct FItemDefinition;

// 머리 위 상태 전용 공통 위젯. 전투/상점 상태를 소유하지 않고 전달받은 값을 표시한다.
UCLASS(Blueprintable)
class PROJECT_A_API UUnitStatusWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UUnitStatusWidget(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style", meta=(ClampMin="40")) float BarWidth = 124.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style", meta=(ClampMin="3")) float HealthHeight = 9.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style", meta=(ClampMin="2")) float ManaHeight = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style", meta=(ClampMin="8")) float ItemIconSize = 17.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style", meta=(ClampMin="1")) float HealthPerSegment = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style") FLinearColor AllyHealthColor = FLinearColor(0.12f, 0.78f, 0.34f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style") FLinearColor EnemyHealthColor = FLinearColor(0.90f, 0.16f, 0.18f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style") FLinearColor ShieldColor = FLinearColor(0.82f, 0.92f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Status|Style") FLinearColor ManaColor = FLinearColor(0.08f, 0.50f, 0.96f);

    // 연결 직후 현재 값도 동기화하므로 BeginPlay 이전/이후 초기화 순서에 의존하지 않는다.
    UFUNCTION(BlueprintCallable, Category="Unit Status") void SetUnit(ABattleUnitBase* Unit);
    UFUNCTION(BlueprintCallable, Category="Unit Status") void RefreshStatus();
    // 실제 아이템 소유권은 ItemManager에 둔다. 통합 담당이 장착 변경 시 아이콘만 전달한다.
    UFUNCTION(BlueprintCallable, Category="Unit Status") void SetItemIcons(const TArray<UTexture2D*>& Icons);
    void SetEquipment(const TArray<FItemDefinition>& Equipment);
    FVector2D GetStatusDrawSize() const;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativePreConstruct() override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
    UPROPERTY(Transient) TWeakObjectPtr<ABattleUnitBase> SourceUnit;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> ItemIcons;
    TArray<FText> ItemLabels;
    float Health = 0.f;
    float MaxHealth = 1.f;
    float Shield = 0.f;
    float ManaFraction = 0.f;
    bool bEnemy = false;
    bool bShowMana = true;
    void BindUnit();
    void UnbindUnit();
    UFUNCTION() void HandleUnitChanged(ABattleUnitBase* Unit);
};
