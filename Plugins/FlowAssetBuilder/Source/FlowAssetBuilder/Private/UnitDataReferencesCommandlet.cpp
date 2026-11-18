#include "UnitDataReferencesCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"

UUnitDataReferencesCommandlet::UUnitDataReferencesCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}
int32 UUnitDataReferencesCommandlet::Main(const FString& Params)
{
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Assets;
    Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets, true);
    FString Report;
    const bool bRefresh = FParse::Param(*Params, TEXT("Refresh"));
    int32 Count = 0;
    for (const FAssetData& Asset : Assets)
    {
        if (!Asset.PackageName.ToString().StartsWith(TEXT("/Game/"))) continue;
        UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
        if (!BP) return 1;
        ++Count;
        bool bTouched = false;
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            const bool bUnitStruct = Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin)
            {
                UObject* Type = Pin ? Pin->PinType.PinSubCategoryObject.Get() : nullptr;
                return Type && (Type->GetName() == TEXT("UnitDefinition") || Type->GetName() == TEXT("UnitAnimationDefinition"));
            });
            if (!bUnitStruct) continue;
            if (bRefresh)
                if (UK2Node* K2Node = Cast<UK2Node>(Node)) { K2Node->ReconstructNode(); bTouched = true; }
            Report += FString::Printf(TEXT("\nBP=%s Graph=%s Node=%s\n"), *BP->GetPathName(), *Graph->GetName(), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            for (UEdGraphPin* Pin : Node->Pins)
            {
                Report += FString::Printf(TEXT("  %s linked=%d default=%s\n"), *Pin->PinName.ToString(), Pin->LinkedTo.Num(), *Pin->DefaultValue);
                for (UEdGraphPin* Link : Pin->LinkedTo)
                    Report += FString::Printf(TEXT("    -> %s.%s\n"), *Link->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *Link->PinName.ToString());
            }
        }
        if (bTouched)
        {
            // 구조체에 관련된 노드만 갱신한다. 기존 실행선/애니메이션 연결은 이름으로 보존한다.
            FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
            FKismetEditorUtilities::CompileBlueprint(BP);
            if (BP->Status == BS_Error) return 1;
            FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
            const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
            if (!UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args)) return 1;
        }
    }
    Report = FString::Printf(TEXT("Blueprints scanned: %d\n"), Count) + Report;
    const FString Path = FPaths::ProjectSavedDir() / (bRefresh ? TEXT("UnitDataCleanup/BlueprintReferences.refreshed.txt") : TEXT("UnitDataCleanup/BlueprintReferences.txt"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveStringToFile(Report, *Path, FFileHelper::EEncodingOptions::ForceUTF8)) return 1;
    UE_LOG(LogTemp, Display, TEXT("UNIT_DATA_REFERENCES_OK: %d blueprints, %s"), Count, *Path);
    return 0;
}
