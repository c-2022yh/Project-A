#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../UnitProjectileVisualLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitProjectileVisualApplyTest, "ProjectA.Unit.ProjectileVisualApplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnitProjectileVisualApplyTest::RunTest(const FString& Parameters)
{
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("Sphere"), Sphere) || !TestNotNull(TEXT("Cube"), Cube)) return false;
    UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>();
    UMaterialInterface* DefaultMaterial = Sphere->GetMaterial(0);
    UMaterialInstanceDynamic* Override = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(MD_Surface), Component);
    // 이전 BP의 크기/재질이 남아 있어도 새 DT 값으로 교체돼야 한다.
    Component->SetRelativeScale3D(FVector(0.15f));
    Component->SetMaterial(3, Override);
    FUnitProjectileVisualDefinition Visual;
    Visual.Mesh = Sphere; Visual.Material = Override; Visual.Scale = FVector(0.3f, 0.4f, 0.5f);
    Visual.Rotation = FRotator(10.f, 20.f, 30.f);
    TestTrue(TEXT("Visual applied"), UUnitProjectileVisualLibrary::ApplyProjectileVisual(Component, Visual));
    TestTrue(TEXT("Correct mesh/material"), Component->GetStaticMesh() == Sphere && Component->GetMaterial(0) == Override);
    TestTrue(TEXT("DT scale replaces old component scale without multiplication"), Component->GetRelativeScale3D().Equals(Visual.Scale));
    TestTrue(TEXT("DT mesh rotation applied"), Component->GetRelativeRotation().Equals(Visual.Rotation));
    TestTrue(TEXT("Valid visual shown"), Component->IsVisible());
    TestEqual(TEXT("Old extra material overrides removed"), Component->OverrideMaterials.Num(), 1);

    // 같은 메시라도 Material=None이면 이전 Override를 지우고 메시 기본 재질을 복원한다.
    Visual.Material = nullptr;
    Visual.Rotation = FRotator::ZeroRotator;
    UUnitProjectileVisualLibrary::ApplyProjectileVisual(Component, Visual);
    TestTrue(TEXT("None material uses original mesh material"), Component->GetMaterial(0) == DefaultMaterial);
    TestEqual(TEXT("No stale overrides"), Component->OverrideMaterials.Num(), 0);
    TestTrue(TEXT("Previous mesh correction does not leak into next visual"), Component->GetRelativeRotation().IsNearlyZero());
    TestTrue(TEXT("Source mesh material unchanged"), Sphere->GetMaterial(0) == DefaultMaterial);

    Visual.Mesh = Cube; Visual.Scale = FVector(0.1f);
    UUnitProjectileVisualLibrary::ApplyProjectileVisual(Component, Visual);
    TestTrue(TEXT("Another unit can use different mesh and scale"), Component->GetStaticMesh() == Cube && Component->GetRelativeScale3D().Equals(Visual.Scale));
    TestTrue(TEXT("New mesh uses its own default material"), Component->GetMaterial(0) == Cube->GetMaterial(0));
    Visual.Mesh = nullptr;
    TestFalse(TEXT("None mesh reports no visual"), UUnitProjectileVisualLibrary::ApplyProjectileVisual(Component, Visual));
    TestTrue(TEXT("None mesh clears old geometry and visibility"), !Component->GetStaticMesh() && !Component->IsVisible());
    Visual.Mesh = Sphere;
    TestTrue(TEXT("Valid mesh restores visibility"), UUnitProjectileVisualLibrary::ApplyProjectileVisual(Component, Visual) && Component->IsVisible());
    TestFalse(TEXT("Missing component handled"), UUnitProjectileVisualLibrary::ApplyProjectileVisual(nullptr, Visual));
    // Actor 역할의 부모는 비행 방향, 자식 메시는 DT의 원본 축 보정만 담당한다.
    USceneComponent* FlightRoot = NewObject<USceneComponent>();
    if (!TestTrue(TEXT("Visual attaches to flight root"), Component->AttachToComponent(FlightRoot, FAttachmentTransformRules::KeepRelativeTransform))) return false;
    Visual.Rotation = FRotator(0.f, -90.f, 0.f); // arrow_bow의 +Y 화살촉을 +X에 맞춘다.
    UUnitProjectileVisualLibrary::ApplyProjectileVisual(Component, Visual);
    for (const FVector& Direction : {FVector(1,0,0), FVector(0,1,0), FVector(-1,-2,0.5f), FVector(0,0,1)})
    {
        FlightRoot->SetWorldRotation(Direction.Rotation());
        TestTrue(TEXT("Corrected +Y arrow tip follows 3D flight direction"), Component->GetRightVector().Equals(Direction.GetSafeNormal(), 0.001f));
        TestTrue(TEXT("Flight rotation preserves DT scale"), Component->GetRelativeScale3D().Equals(Visual.Scale));
    }
    return true;
}
#endif
