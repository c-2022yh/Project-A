#pragma once
#include "Commandlets/Commandlet.h"
#include "UnitDataReferencesCommandlet.generated.h"

// DT 구조체 필드를 제거하기 전 BP의 실제 연결을 읽기 전용으로 기록한다.
UCLASS()
class UUnitDataReferencesCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UUnitDataReferencesCommandlet();
    virtual int32 Main(const FString& Params) override;
};
