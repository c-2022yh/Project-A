using UnrealBuildTool;
public class FlowAssetBuilder : ModuleRules {
 public FlowAssetBuilder(ReadOnlyTargetRules Target) : base(Target) {
 PCHUsage=PCHUsageMode.NoPCHs;
 bUseUnity=false;
 PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","UnrealEd","BlueprintGraph","KismetCompiler","AssetRegistry"});
 }
}
