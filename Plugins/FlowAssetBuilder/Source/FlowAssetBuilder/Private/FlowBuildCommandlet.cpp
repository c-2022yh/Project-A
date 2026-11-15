#include "FlowBuildCommandlet.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "KismetCompiler.h"
#include "K2Node_GetDataTableRow.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "UObject/StructOnScope.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallDelegate.h"
#include "Kismet/KismetGuidLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "TimerManager.h"
#include "K2Node_Event.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_ExecutionSequence.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/Script.h"
static int32 FinalizeFlow();
static int32 VerifyCompleteFlow();
static int32 ConnectFlow();
static int32 PolishFlow();
static int32 CompleteFlow(const FString& Params);
static int32 VerifyFlow();
static int32 ExtendFlow();
IMPLEMENT_MODULE(FDefaultModuleImpl, FlowAssetBuilder)
UFlowBuildCommandlet::UFlowBuildCommandlet(){ IsClient=false; IsEditor=true; LogToConsole=true; }
static void SaveFlow(UObject* Obj){
 FAssetRegistryModule::AssetCreated(Obj);
 Obj->MarkPackageDirty(); FSavePackageArgs A; A.TopLevelFlags=RF_Public|RF_Standalone;
 FString File=FPackageName::LongPackageNameToFilename(Obj->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
 if(!UPackage::SavePackage(Obj->GetOutermost(),Obj,*File,A)) UE_LOG(LogTemp,Fatal,TEXT("Save failed: %s"),*File);
}
int32 UFlowBuildCommandlet::Main(const FString& Params){
 if(Params.Contains(TEXT("Polish")))return PolishFlow();
 if(Params.Contains(TEXT("Connect")))return ConnectFlow();
 if(Params.Contains(TEXT("Finalize")))return FinalizeFlow();
 if(Params.Contains(TEXT("FullVerify")))return VerifyCompleteFlow();
 if(Params.Contains(TEXT("Complete")))return CompleteFlow(Params);
 if(Params.Contains(TEXT("Verify")))return VerifyFlow();
 if(Params.Contains(TEXT("Extend")))return ExtendFlow();
 const FString Root=TEXT("/Game/BluePrints/GameFlow/");
 for(const TCHAR* N:{TEXT("E_GamePhase"),TEXT("ST_RoundDefinition"),TEXT("DT_Rounds"),TEXT("BP_RoundManager")}){
  if(FPackageName::DoesPackageExist(Root+N)){UE_LOG(LogTemp,Error,TEXT("Refusing overwrite: %s"),N); return 2;}
 }
 auto E=CastChecked<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(CreatePackage(*(Root+TEXT("E_GamePhase"))),TEXT("E_GamePhase"),RF_Public|RF_Standalone));
 TArray<FString> Phases={TEXT("Initializing"),TEXT("Preparation"),TEXT("Combat"),TEXT("ResolvingCombat"),TEXT("Reward"),TEXT("Finished")};
 for(int32 I=0;I<Phases.Num();++I){ FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(E); FEnumEditorUtils::SetEnumeratorDisplayName(E,I,FText::FromString(Phases[I])); }
 auto S=FStructureEditorUtils::CreateUserDefinedStruct(CreatePackage(*(Root+TEXT("ST_RoundDefinition"))),TEXT("ST_RoundDefinition"),RF_Public|RF_Standalone);
 TArray<FGuid> Old; for(auto& V:FStructureEditorUtils::GetVarDesc(S))Old.Add(V.VarGuid);
 for(const TCHAR* N:{TEXT("RoundNumber"),TEXT("MaxDeployedUnits"),TEXT("CombatDurationSeconds")}){
  FEdGraphPinType T; T.PinCategory=FString(N)==TEXT("CombatDurationSeconds")?UEdGraphSchema_K2::PC_Real:UEdGraphSchema_K2::PC_Int;
  if(T.PinCategory==UEdGraphSchema_K2::PC_Real)T.PinSubCategory=UEdGraphSchema_K2::PC_Double;
  if(!FStructureEditorUtils::AddVariable(S,T))return 3;
  auto G=FStructureEditorUtils::GetVarDesc(S).Last().VarGuid;
  if(!FStructureEditorUtils::RenameVariable(S,G,N))return 4;
 }
 for(auto G:Old)FStructureEditorUtils::RemoveVariable(S,G);
 FStructureEditorUtils::CompileStructure(S);
 auto D=NewObject<UDataTable>(CreatePackage(*(Root+TEXT("DT_Rounds"))),TEXT("DT_Rounds"),RF_Public|RF_Standalone); D->RowStruct=S;
 FString CSV=TEXT("Name,RoundNumber,MaxDeployedUnits,CombatDurationSeconds\n");
 for(int I=1;I<=5;++I)CSV+=FString::Printf(TEXT("R%d,%d,%d,60\n"),I,I,I+1);
 auto Errors=D->CreateTableFromCSVString(CSV); if(!Errors.IsEmpty()){for(auto& Err:Errors)UE_LOG(LogTemp,Error,TEXT("%s"),*Err);return 5;}
 if(D->GetRowNames().Num()!=5)return 6;
 auto B=FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),CreatePackage(*(Root+TEXT("BP_RoundManager"))),TEXT("BP_RoundManager"),BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass());
 auto Var=[&](FName Name,FName Category,UObject* Type=nullptr,const FString& Default=TEXT("")){
  FEdGraphPinType T;T.PinCategory=Category;T.PinSubCategoryObject=Type;
  FBlueprintEditorUtils::AddMemberVariable(B,Name,T,Default);
 };
 Var(TEXT("CurrentPhase"),UEdGraphSchema_K2::PC_Byte,E);
 for(const TCHAR* N:{TEXT("CurrentRoundNumber"),TEXT("PendingNextRoundNumber"),TEXT("EffectiveDeploymentLimit"),TEXT("DeploymentLimitRoundNumber"),TEXT("DeployedAllyCount")})Var(N,UEdGraphSchema_K2::PC_Int);
 Var(TEXT("CurrentRoundDefinition"),UEdGraphSchema_K2::PC_Struct,S);
 Var(TEXT("RoundsTable"),UEdGraphSchema_K2::PC_Object,UDataTable::StaticClass(),D->GetPathName());
 for(const TCHAR* N:{TEXT("bUnitDragging"),TEXT("bItemDragging"),TEXT("bInteractionStateReady"),TEXT("bCanStartCombat"),TEXT("bTransitionInProgress"),TEXT("bRoundDataValid"),TEXT("bIntegrationReady")})Var(N,UEdGraphSchema_K2::PC_Boolean,nullptr,TEXT("false"));
 Var(TEXT("StartBlockMessage"),UEdGraphSchema_K2::PC_Text,nullptr,TEXT("기물을 배치하세요"));
 // Pure Blueprint start gate. Unknown integration/interaction state always blocks.
 auto Graph=FBlueprintEditorUtils::CreateNewGraph(B,TEXT("CanStartCombat"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());
 FBlueprintEditorUtils::AddFunctionGraph<UFunction>(B,Graph,true,nullptr);
 UK2Node_FunctionEntry* Entry=nullptr;
 for(auto Node:Graph->Nodes)if(auto X=Cast<UK2Node_FunctionEntry>(Node))Entry=X;
 Entry->AddExtraFlags(FUNC_BlueprintPure|FUNC_Public);
 auto Result=NewObject<UK2Node_FunctionResult>(Graph);Graph->AddNode(Result);Result->CreateNewGuid();Result->AllocateDefaultPins();
 FEdGraphPinType BoolType;BoolType.PinCategory=UEdGraphSchema_K2::PC_Boolean;
 auto Output=Result->CreateUserDefinedPin(TEXT("bAllowed"),BoolType,EGPD_Input);
 auto Schema=GetDefault<UEdGraphSchema_K2>();int32 Pos=0;
 auto Get=[&](FName Name)->UEdGraphPin* {auto N=NewObject<UK2Node_VariableGet>(Graph);Graph->AddNode(N);N->CreateNewGuid();N->VariableReference.SetSelfMember(Name);N->AllocateDefaultPins();N->NodePosX=-1200;N->NodePosY=Pos;Pos+=100;return N->GetValuePin();};
 auto Math=[&](FName Name,UEdGraphPin* A,UEdGraphPin* C=nullptr,const FString& Constant=TEXT(""))->UEdGraphPin* {
  auto N=NewObject<UK2Node_CallFunction>(Graph);Graph->AddNode(N);N->CreateNewGuid();N->SetFromFunction(UKismetMathLibrary::StaticClass()->FindFunctionByName(Name));N->AllocateDefaultPins();N->NodePosX=-600;N->NodePosY=Pos;Pos+=100;
  if(!Schema->TryCreateConnection(A,N->FindPinChecked(TEXT("A"))))UE_LOG(LogTemp,Fatal,TEXT("Cannot connect A: %s"),*Name.ToString());
  if(auto Pin=N->FindPin(TEXT("B"))){if(C){if(!Schema->TryCreateConnection(C,Pin))UE_LOG(LogTemp,Fatal,TEXT("Cannot connect B"));}else Schema->TrySetDefaultValue(*Pin,Constant);}
  return N->GetReturnValuePin();
 };
 auto Gate=Math(TEXT("EqualEqual_ByteByte"),Get(TEXT("CurrentPhase")),nullptr,TEXT("1"));
 for(const TCHAR* N:{TEXT("bRoundDataValid"),TEXT("bInteractionStateReady"),TEXT("bIntegrationReady")})Gate=Math(TEXT("BooleanAND"),Gate,Get(N));
 for(const TCHAR* N:{TEXT("bUnitDragging"),TEXT("bItemDragging"),TEXT("bTransitionInProgress")})Gate=Math(TEXT("BooleanAND"),Gate,Math(TEXT("Not_PreBool"),Get(N)));
 Gate=Math(TEXT("BooleanAND"),Gate,Math(TEXT("GreaterEqual_IntInt"),Get(TEXT("DeployedAllyCount")),nullptr,TEXT("1")));
 Gate=Math(TEXT("BooleanAND"),Gate,Math(TEXT("LessEqual_IntInt"),Get(TEXT("DeployedAllyCount")),Get(TEXT("EffectiveDeploymentLimit"))));
 if(!Schema->TryCreateConnection(Gate,Output))return 8;
 Schema->TryCreateConnection(Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then),Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
 // Typed row lookup has separate found/missing paths; it never advances a round.
 auto Lookup=FBlueprintEditorUtils::CreateNewGraph(B,TEXT("GetRoundDefinition"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());
 FBlueprintEditorUtils::AddFunctionGraph<UFunction>(B,Lookup,true,nullptr);
 UK2Node_FunctionEntry* LE=nullptr;for(auto N:Lookup->Nodes)if(auto X=Cast<UK2Node_FunctionEntry>(N))LE=X;
 FEdGraphPinType NameType;NameType.PinCategory=UEdGraphSchema_K2::PC_Name;
 auto RowName=LE->CreateUserDefinedPin(TEXT("RowName"),NameType,EGPD_Output);
 auto Row=NewObject<UK2Node_GetDataTableRow>(Lookup);Lookup->AddNode(Row);Row->CreateNewGuid();Row->AllocateDefaultPins();
 Schema->TrySetDefaultObject(*Row->GetDataTablePin(),D);Row->PinDefaultValueChanged(Row->GetDataTablePin());
 Schema->TryCreateConnection(RowName,Row->GetRowNamePin());Schema->TryCreateConnection(LE->FindPinChecked(UEdGraphSchema_K2::PN_Then),Row->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
 for(bool Found:{true,false}){
  auto R=NewObject<UK2Node_FunctionResult>(Lookup);Lookup->AddNode(R);R->CreateNewGuid();R->AllocateDefaultPins();
  auto Success=R->CreateUserDefinedPin(TEXT("bFound"),BoolType,EGPD_Input);Schema->TrySetDefaultValue(*Success,Found?TEXT("true"):TEXT("false"));
  FEdGraphPinType StructType;StructType.PinCategory=UEdGraphSchema_K2::PC_Struct;StructType.PinSubCategoryObject=S;
  auto Definition=R->CreateUserDefinedPin(TEXT("Definition"),StructType,EGPD_Input);
  if(Found)Schema->TryCreateConnection(Row->GetResultPin(),Definition);
  Schema->TryCreateConnection(Found?Row->FindPinChecked(UEdGraphSchema_K2::PN_Then):Row->GetRowNotFoundPin(),R->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
  R->NodePosX=600;R->NodePosY=Found?0:300;
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);FCompilerResultsLog CompileLog;FKismetEditorUtilities::CompileBlueprint(B,EBlueprintCompileOptions::None,&CompileLog);
 if(B->Status==BS_Error)return 7;SaveFlow(E);SaveFlow(S);SaveFlow(D);SaveFlow(B);
 UE_LOG(LogTemp,Display,TEXT("FLOW_BUILD_OK: enum, struct, five rows, manager scaffold saved. No GameMode integration."));return 0;
}
#include "FlowExtension.inl"
#include "FlowComplete.inl"
#include "FlowFinalize.inl"
#include "FlowTests.inl"
#include "FlowConnect.inl"
#include "FlowPolish.inl"
