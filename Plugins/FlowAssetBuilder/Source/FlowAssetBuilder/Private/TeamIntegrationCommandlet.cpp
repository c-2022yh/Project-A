#include "TeamIntegrationCommandlet.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

// 이전 조작 테스트맵만 수정한다. 실제 기물 외형/스킬 데이터는 변경하지 않는다.
static bool RepairControllerPreview(UBlueprint* BP)
{
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    int32 Removed = 0, Initialized = 0;
    for (UEdGraph* Graph : Graphs)
    {
        const auto OriginalNodes = Graph->Nodes;
        for (UEdGraphNode* Node : OriginalNodes)
        {
            UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node);
            if (!Getter || Getter->VariableReference.GetMemberName() != TEXT("PlaceholderMesh")) continue;
            UEdGraphPin* Output = Getter->FindPin(TEXT("PlaceholderMesh"));
            if (!Output || Output->LinkedTo.IsEmpty()) return false;
            TSet<UEdGraphNode*> PreviewNodes;
            for (UEdGraphPin* Link : Output->LinkedTo) PreviewNodes.Add(Link->GetOwningNode());
            const auto DirectNodes = PreviewNodes;
            for (UEdGraphNode* Preview : DirectNodes)
            {
                UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Preview);
                if (!Call) return false;
                const FName Name = Call->FunctionReference.GetMemberName();
                if (Name != TEXT("SetStaticMesh") && Name != TEXT("CreateDynamicMaterialInstance")) return false;
                if (Name == TEXT("CreateDynamicMaterialInstance"))
                    for (UEdGraphPin* Link : Call->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->LinkedTo)
                    {
                        auto* Color = Cast<UK2Node_CallFunction>(Link->GetOwningNode());
                        if (!Color || Color->FunctionReference.GetMemberName() != TEXT("SetVectorParameterValue")) return false;
                        PreviewNodes.Add(Color);
                    }
            }
            TArray<UEdGraphPin*> Before, After;
            for (UEdGraphNode* Preview : PreviewNodes)
            {
                for (UEdGraphPin* Link : Preview->FindPinChecked(UEdGraphSchema_K2::PN_Execute)->LinkedTo)
                    if (!PreviewNodes.Contains(Link->GetOwningNode())) Before.Add(Link);
                for (UEdGraphPin* Link : Preview->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo)
                    if (!PreviewNodes.Contains(Link->GetOwningNode())) After.Add(Link);
            }
            if (Before.Num() != 1 || After.Num() != 1) return false;
            for (UEdGraphNode* Preview : PreviewNodes) FBlueprintEditorUtils::RemoveNode(BP, Preview, true);
            FBlueprintEditorUtils::RemoveNode(BP, Getter, true);
            for (UEdGraphPin* In : Before) for (UEdGraphPin* Out : After)
                if (!Schema->TryCreateConnection(In, Out)) return false;
            ++Removed;
        }
        // 현재 UnitBase는 생성 직후 DT 초기화가 필요하다. 기존 팀/배치 흐름 앞에 넣는다.
        const auto Nodes = Graph->Nodes;
        for (UEdGraphNode* Node : Nodes)
        {
            UK2Node_SpawnActorFromClass* Spawn = Cast<UK2Node_SpawnActorFromClass>(Node);
            if (!Spawn) continue;
            UEdGraphPin* Result = Spawn->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
            UK2Node_VariableSet* TeamSetter = nullptr;
            for (UEdGraphPin* Link : Result->LinkedTo)
                if (auto* Setter = Cast<UK2Node_VariableSet>(Link->GetOwningNode()))
                    if (Setter->VariableReference.GetMemberName() == TEXT("TeamType")) TeamSetter = Setter;
            if (!TeamSetter) continue;
            UEdGraphPin* Then = Spawn->FindPinChecked(UEdGraphSchema_K2::PN_Then);
            if (Then->LinkedTo.Num() == 1 && Then->LinkedTo[0]->GetOwningNode()->GetName().StartsWith(TEXT("TeamInitializeUnit"))) continue;
            UClass* UnitClass = FindObject<UClass>(nullptr, TEXT("/Script/Project_A.BattleUnitBase"));
            UFunction* Function = UnitClass ? UnitClass->FindFunctionByName(TEXT("InitializeUnit")) : nullptr;
            if (!Function) return false;
            UK2Node_CallFunction* Init = NewObject<UK2Node_CallFunction>(Graph, MakeUniqueObjectName(Graph, UK2Node_CallFunction::StaticClass(), TEXT("TeamInitializeUnit")));
            Graph->AddNode(Init); Init->CreateNewGuid(); Init->SetFromFunction(Function); Init->AllocateDefaultPins();
            Init->NodePosX = Spawn->NodePosX + 300; Init->NodePosY = Spawn->NodePosY + 250;
            Init->FindPinChecked(TEXT("Data"))->DefaultValue = TEXT("(DataTable=\"/Script/Engine.DataTable'/Game/Data/DT_Units.DT_Units'\",RowName=\"Knight\")");
            Init->FindPinChecked(TEXT("Stars"))->DefaultValue = TEXT("1");
            UEdGraphPin* Team = TeamSetter->FindPinChecked(TEXT("TeamType"));
            Init->FindPinChecked(TEXT("Team"))->DefaultValue = Team->DefaultValue;
            for (UEdGraphPin* Link : Team->LinkedTo)
                if (!Schema->TryCreateConnection(Link, Init->FindPinChecked(TEXT("Team")))) return false;
            if (!Schema->TryCreateConnection(Result, Init->FindPinChecked(UEdGraphSchema_K2::PN_Self))) return false;
            const auto After = Then->LinkedTo; Then->BreakAllPinLinks();
            if (!Schema->TryCreateConnection(Then, Init->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) return false;
            for (UEdGraphPin* Link : After)
                if (!Schema->TryCreateConnection(Init->FindPinChecked(UEdGraphSchema_K2::PN_Then), Link)) return false;
            ++Initialized;
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status == BS_Error) return false;
    BP->MarkPackageDirty();
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
    const FString File = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    const bool Saved = UPackage::SavePackage(BP->GetOutermost(), BP, *File, Args);
    UE_LOG(LogTemp, Display, TEXT("Team controller migration: removed %d obsolete previews, initialized %d spawn paths, saved=%d"), Removed, Initialized, Saved);
    return Saved;
}

UTeamIntegrationCommandlet::UTeamIntegrationCommandlet()
{
    IsClient = false; IsEditor = true; LogToConsole = true;
}

int32 UTeamIntegrationCommandlet::Main(const FString& Params)
{
    // DT 기본값을 컴파일 중 처음 읽으면 AnimBP 로딩이 중첩될 수 있으므로 먼저 로드한다.
    if (!LoadObject<UObject>(nullptr, TEXT("/Game/BluePrints/Units/Animation/ABP_Unit.ABP_Unit"))
        || !LoadObject<UObject>(nullptr, TEXT("/Game/Data/DT_Units.DT_Units"))) return 1;
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/ControlerTestMap/BP_ControlerTestGameMode.BP_ControlerTestGameMode"));
    if (!BP) return 1;
    if (Params.Contains(TEXT("Repair")) && !RepairControllerPreview(BP)) return 2;
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    FString Report;
    for (UEdGraph* Graph : Graphs)
    {
        Report += TEXT("GRAPH ") + Graph->GetName() + TEXT("\n");
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            Report += Node->GetName() + TEXT(" ") + Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() + TEXT("\n");
            for (UEdGraphPin* Pin : Node->Pins)
            {
                Report += FString::Printf(TEXT("  %s %s default=%s object=%s"), Pin->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT"), *Pin->PinName.ToString(), *Pin->DefaultValue, *GetPathNameSafe(Pin->DefaultObject));
                for (UEdGraphPin* Link : Pin->LinkedTo)
                    Report += TEXT(" -> ") + Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString();
                Report += TEXT("\n");
            }
        }
    }
    FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("TeamControllerGraph.txt")), FFileHelper::EEncodingOptions::ForceUTF8);
    return 0;
}
