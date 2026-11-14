// All nodes created below belong to BP_RoundManager; no runtime plugin dependency.
struct FFlowGraph {
 UBlueprint* BP; UEdGraph* G; UK2Node_FunctionEntry* Entry; const UEdGraphSchema_K2* S; int32 Y=0;
 FFlowGraph(UBlueprint* B,FName Name,bool Reuse=false):BP(B),S(GetDefault<UEdGraphSchema_K2>()){
  G=nullptr;if(Reuse)for(auto Existing:B->FunctionGraphs)if(Existing->GetFName()==Name)G=Existing;
  if(!G){G=FBlueprintEditorUtils::CreateNewGraph(B,Name,UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());FBlueprintEditorUtils::AddFunctionGraph<UFunction>(B,G,true,nullptr);}
  Entry=nullptr;for(auto N:G->Nodes)if(auto X=Cast<UK2Node_FunctionEntry>(N))Entry=X;
 }
 template<class T>T* Node(){auto N=NewObject<T>(G);G->AddNode(N);N->CreateNewGuid();N->NodePosY=Y;Y+=140;return N;}
 void Link(UEdGraphPin* A,UEdGraphPin* B){if(!A||!B||!S->TryCreateConnection(A,B))UE_LOG(LogTemp,Fatal,TEXT("Connection failed in %s"),*G->GetName());}
 UEdGraphPin* Input(FName Name,FName Kind){FEdGraphPinType T;T.PinCategory=Kind;return Entry->CreateUserDefinedPin(Name,T,EGPD_Output);}
 UK2Node_FunctionResult* Result(){auto N=Node<UK2Node_FunctionResult>();N->AllocateDefaultPins();return N;}
 UEdGraphPin* Get(FName Name){auto N=Node<UK2Node_VariableGet>();N->VariableReference.SetSelfMember(Name);N->AllocateDefaultPins();return N->GetValuePin();}
 UEdGraphPin* Set(FName Name,UEdGraphPin* Exec,UEdGraphPin* Value=nullptr,FString Default=TEXT("")){
  auto N=Node<UK2Node_VariableSet>();N->VariableReference.SetSelfMember(Name);N->AllocateDefaultPins();Link(Exec,N->GetExecPin());
  auto ValuePin=N->FindPinChecked(Name);if(Value)Link(Value,ValuePin);else S->TrySetDefaultValue(*ValuePin,Default);return N->GetThenPin();
 }
 UK2Node_CallFunction* Call(UClass* C,FName Name){auto N=Node<UK2Node_CallFunction>();auto F=C->FindFunctionByName(Name);check(F);N->SetFromFunction(F);N->AllocateDefaultPins();return N;}
 UEdGraphPin* Math(FName Name,UEdGraphPin* A,UEdGraphPin* B=nullptr,FString Default=TEXT("")){
  auto N=Call(UKismetMathLibrary::StaticClass(),Name);Link(A,N->FindPinChecked(TEXT("A")));
  if(auto P=N->FindPin(TEXT("B"))){if(B)Link(B,P);else S->TrySetDefaultValue(*P,Default);}return N->GetReturnValuePin();
 }
 UK2Node_IfThenElse* Branch(UEdGraphPin* Exec,UEdGraphPin* Condition){auto N=Node<UK2Node_IfThenElse>();N->AllocateDefaultPins();Link(Exec,N->GetExecPin());Link(Condition,N->GetConditionPin());return N;}
};
static int32 ExtendFlow(){
 auto B=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager"));
 auto D=LoadObject<UDataTable>(nullptr,TEXT("/Game/BluePrints/GameFlow/DT_Rounds.DT_Rounds"));
 if(!B||!D)return 21;
 for(auto G:B->FunctionGraphs)if(G->GetFName()==TEXT("ValidateRoundData")){UE_LOG(LogTemp,Error,TEXT("Extension already applied"));return 22;}
 {
  FFlowGraph F(B,TEXT("UpdateInteractionState"));
  auto Count=F.Input(TEXT("AllyCount"),UEdGraphSchema_K2::PC_Int);
  auto Unit=F.Input(TEXT("UnitDragging"),UEdGraphSchema_K2::PC_Boolean);
  auto Item=F.Input(TEXT("ItemDragging"),UEdGraphSchema_K2::PC_Boolean);
  auto Exec=F.Set(TEXT("DeployedAllyCount"),F.Entry->GetThenPin(),Count);
  Exec=F.Set(TEXT("bUnitDragging"),Exec,Unit);Exec=F.Set(TEXT("bItemDragging"),Exec,Item);
  Exec=F.Set(TEXT("bInteractionStateReady"),Exec,nullptr,TEXT("true"));
  auto Can=F.Node<UK2Node_CallFunction>();Can->FunctionReference.SetSelfMember(TEXT("CanStartCombat"));Can->AllocateDefaultPins();
  Exec=F.Set(TEXT("bCanStartCombat"),Exec,Can->FindPinChecked(TEXT("bAllowed")));
  F.Link(Exec,F.Result()->GetExecPin());
 }
 {
  FFlowGraph F(B,TEXT("ValidateRoundData"));
  auto Exec=F.Set(TEXT("bRoundDataValid"),F.Entry->GetThenPin(),nullptr,TEXT("false"));
  Exec=F.Set(TEXT("bCanStartCombat"),Exec,nullptr,TEXT("false"));
  auto Fail=F.Result();
  auto Names=F.Call(UDataTableFunctionLibrary::StaticClass(),TEXT("GetDataTableRowNames"));
  F.Link(Exec,Names->GetExecPin());F.Link(F.Get(TEXT("RoundsTable")),Names->FindPinChecked(TEXT("Table")));
  auto Length=F.Node<UK2Node_CallArrayFunction>();Length->SetFromFunction(UKismetArrayLibrary::StaticClass()->FindFunctionByName(TEXT("Array_Length")));Length->AllocateDefaultPins();
  F.Link(Names->FindPinChecked(TEXT("OutRowNames")),Length->FindPinChecked(TEXT("TargetArray")));
  auto CountOK=F.Math(TEXT("EqualEqual_IntInt"),Length->GetReturnValuePin(),nullptr,TEXT("5"));
  auto Check=F.Branch(Names->GetThenPin(),CountOK);F.Link(Check->GetElsePin(),Fail->GetExecPin());Exec=Check->GetThenPin();
  const TCHAR* Fields[]={TEXT("RoundNumber"),TEXT("MaxDeployedUnits"),TEXT("CombatDurationSeconds")};
  for(int32 I=1;I<=5;++I){
   auto Row=F.Node<UK2Node_GetDataTableRow>();Row->AllocateDefaultPins();
   F.S->TrySetDefaultObject(*Row->GetDataTablePin(),D);Row->PinDefaultValueChanged(Row->GetDataTablePin());
   F.Link(F.Get(TEXT("RoundsTable")),Row->GetDataTablePin());
   F.S->TrySetDefaultValue(*Row->GetRowNamePin(),FString::Printf(TEXT("R%d"),I));
   F.Link(Exec,Row->GetExecPin());F.Link(Row->GetRowNotFoundPin(),Fail->GetExecPin());
   auto Break=F.Node<UK2Node_BreakStruct>();Break->StructType=D->RowStruct;Break->AllocateDefaultPins();
   UEdGraphPin* StructInput=nullptr;for(auto P:Break->Pins)if(P->Direction==EGPD_Input&&P->PinType.PinCategory==UEdGraphSchema_K2::PC_Struct)StructInput=P;
   F.Link(Row->GetResultPin(),StructInput);
   UEdGraphPin* Valid=nullptr;
   for(int32 J=0;J<3;++J){
    auto Prop=FStructureEditorUtils::GetPropertyByFriendlyName(CastChecked<UUserDefinedStruct>(D->RowStruct),Fields[J]);check(Prop);
    auto Value=Break->FindPinChecked(Prop->GetFName());
    auto Equals=F.Math(J==2?TEXT("EqualEqual_DoubleDouble"):TEXT("EqualEqual_IntInt"),Value,nullptr,FString::FromInt(J==0?I:J==1?I+1:60));
    Valid=Valid?F.Math(TEXT("BooleanAND"),Valid,Equals):Equals;
   }
   auto OK=F.Branch(Row->GetThenPin(),Valid);F.Link(OK->GetElsePin(),Fail->GetExecPin());Exec=OK->GetThenPin();
  }
  Exec=F.Set(TEXT("bRoundDataValid"),Exec,nullptr,TEXT("true"));F.Link(Exec,F.Result()->GetExecPin());
 }
 for(auto& V:B->NewVariables)if(V.VarName==TEXT("CurrentPhase"))V.PropertyFlags|=CPF_BlueprintReadOnly;
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);FCompilerResultsLog Log;FKismetEditorUtilities::CompileBlueprint(B,EBlueprintCompileOptions::None,&Log);
 if(Log.NumErrors||B->Status==BS_Error)return 23;
 SaveFlow(B);UE_LOG(LogTemp,Display,TEXT("FLOW_EXTENSION_OK"));return 0;
}
static int32 VerifyFlow(){
 auto B=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager"));
 auto D=LoadObject<UDataTable>(nullptr,TEXT("/Game/BluePrints/GameFlow/DT_Rounds.DT_Rounds"));
 auto E=LoadObject<UUserDefinedEnum>(nullptr,TEXT("/Game/BluePrints/GameFlow/E_GamePhase.E_GamePhase"));
 if(!B||!D||!E||E->NumEnums()!=7||D->GetRowNames().Num()!=5)return 31;
 FCompilerResultsLog Log;FKismetEditorUtilities::CompileBlueprint(B,EBlueprintCompileOptions::None,&Log);if(Log.NumErrors)return 32;
 auto C=B->GeneratedClass;auto Obj=C->GetDefaultObject();
 auto Bool=[&](FName N,bool V){auto P=FindFProperty<FBoolProperty>(C,N);check(P);P->SetPropertyValue_InContainer(Obj,V);};
 auto Integer=[&](FName N,int32 V){auto P=FindFProperty<FIntProperty>(C,N);check(P);P->SetPropertyValue_InContainer(Obj,V);};
 auto Validate=C->FindFunctionByName(TEXT("ValidateRoundData"));if(!Validate)return 33;Obj->ProcessEvent(Validate,nullptr);
 auto Valid=FindFProperty<FBoolProperty>(C,TEXT("bRoundDataValid"));if(!Valid->GetPropertyValue_InContainer(Obj))return 34;
 auto Phase=FindFProperty<FByteProperty>(C,TEXT("CurrentPhase"));check(Phase);Phase->SetPropertyValue_InContainer(Obj,1);
 Bool(TEXT("bInteractionStateReady"),true);Bool(TEXT("bIntegrationReady"),true);Integer(TEXT("EffectiveDeploymentLimit"),2);
 auto Can=C->FindFunctionByName(TEXT("CanStartCombat"));check(Can);
 auto Check=[&](bool Expected){FStructOnScope Args(Can);Obj->ProcessEvent(Can,Args.GetStructMemory());auto P=FindFProperty<FBoolProperty>(Can,TEXT("bAllowed"));return P&&P->GetPropertyValue_InContainer(Args.GetStructMemory())==Expected;};
 Integer(TEXT("DeployedAllyCount"),0);if(!Check(false))return 35;
 Integer(TEXT("DeployedAllyCount"),1);if(!Check(true))return 36;
 Integer(TEXT("DeployedAllyCount"),3);if(!Check(false))return 37;
 Integer(TEXT("DeployedAllyCount"),1);
 for(FName N:{FName(TEXT("bUnitDragging")),FName(TEXT("bItemDragging")),FName(TEXT("bTransitionInProgress"))}){Bool(N,true);if(!Check(false))return 38;Bool(N,false);}
 Bool(TEXT("bIntegrationReady"),false);if(!Check(false))return 39;Bool(TEXT("bIntegrationReady"),true);
 for(uint8 I:{0,2,3,4,5}){Phase->SetPropertyValue_InContainer(Obj,I);if(!Check(false))return 40;}
 D->RemoveRow(TEXT("R5"));Obj->ProcessEvent(Validate,nullptr);if(Valid->GetPropertyValue_InContainer(Obj))return 41;
 UE_LOG(LogTemp,Display,TEXT("FLOW_VERIFY_OK: reload, compile, five rows, start gate, drag locks, phase locks, missing-row rejection. Nothing saved."));return 0;
}
