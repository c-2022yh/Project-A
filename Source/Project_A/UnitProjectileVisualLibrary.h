#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnitTypes.h"
#include "UnitProjectileVisualLibrary.generated.h"

class UStaticMeshComponent;

// 공통 BP는 투사체 생성 직후 이 노드를 한 번 호출한다. 이동/피해는 처리하지 않는다.
UCLASS()
class PROJECT_A_API UUnitProjectileVisualLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // None 메시: 외형 제거 후 false. None 머티리얼: 메시 기본 재질. Scale/Rotation: 메시 상대 보정.
    UFUNCTION(BlueprintCallable, Category="Unit|Visual")
    static bool ApplyProjectileVisual(UStaticMeshComponent* ProjectileMesh, const FUnitProjectileVisualDefinition& Visual);
};
