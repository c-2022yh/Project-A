#include "UnitProjectileVisualLibrary.h"
#include "Components/StaticMeshComponent.h"

bool UUnitProjectileVisualLibrary::ApplyProjectileVisual(UStaticMeshComponent* ProjectileMesh, const FUnitProjectileVisualDefinition& Visual)
{
    if (!IsValid(ProjectileMesh)) return false;
    // 같은 메시로 재설정해도 이전 Override는 자동으로 지워지지 않으므로 먼저 비운다.
    ProjectileMesh->EmptyOverrideMaterials();
    ProjectileMesh->SetStaticMesh(Visual.Mesh);
    ProjectileMesh->SetRelativeScale3D(Visual.Scale);
    ProjectileMesh->SetRelativeRotation(Visual.Rotation);
    ProjectileMesh->SetVisibility(Visual.Mesh != nullptr);
    if (!Visual.Mesh) return false;
    if (Visual.Material) ProjectileMesh->SetMaterial(0, Visual.Material);
    return true;
}
