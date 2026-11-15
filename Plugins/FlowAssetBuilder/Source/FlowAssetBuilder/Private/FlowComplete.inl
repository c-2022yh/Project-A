// Editor-only construction of ordinary runtime Blueprint graphs.
struct FFlowBuilder : FFlowGraph {
 UK2Node_FunctionResult* Exit;
 FFlowBuilder(UBlueprint* B,FName Name,bool Reuse=false):FFlowGraph(B,Name,Reuse){Exit=nullptr;if(Reuse)for(auto N:G->Nodes)if(auto R=Cast<UK2Node_FunctionResult>(N)){Exit=R;break;}if(!Exit)Exit=Result();}
 UEdGraphPin* In(FName Name,FEdGraphPinType T){return Entry->CreateUserDefinedPin(Name,T,EGPD_Output);}
 UEdGraphPin* Guid(FName N){FEdGraphPinType T;T.PinCategory=UEdGraphSchema_K2::PC_Struct;T.PinSubCategoryObject=TBaseStructure<FGuid>::Get();return In(N,T);}
 void End(UEdGraphPin* E){Link(E,Exit->GetExecPin());}
 UEdGraphPin* Guard(UEdGraphPin* E,UEdGraphPin* C){auto N=Branch(E,C);Link(N->GetElsePin(),Exit->GetExecPin());return N->GetThenPin();}
 UEdGraphPin* Eq(FName N,int32 I){return Math(TEXT("EqualEqual_IntInt"),Get(N),nullptr,FString::FromInt(I));}
 UEdGraphPin* Phase(int32 I){return Math(TEXT("EqualEqual_ByteByte"),Get(TEXT("CurrentPhase")),nullptr,FString::FromInt(I));}
 UEdGraphPin* And(UEdGraphPin* A,UEdGraphPin* B){return Math(TEXT("BooleanAND"),A,B);}
 UEdGraphPin* Or(UEdGraphPin* A,UEdGraphPin* B){return Math(TEXT("BooleanOR"),A,B);}
 UEdGraphPin* Not(UEdGraphPin* A){return Math(TEXT("Not_PreBool"),A);}
 UEdGraphPin* GuidEqual(UEdGraphPin* A,FName N){auto C=Call(UKismetGuidLibrary::StaticClass(),TEXT("EqualEqual_GuidGuid"));Link(A,C->FindPinChecked(TEXT("A")));Link(Get(N),C->FindPinChecked(TEXT("B")));return C->GetReturnValuePin();}
 UEdGraphPin* ValidGuid(UEdGraphPin* A){auto C=Call(UKismetGuidLibrary::StaticClass(),TEXT("IsValid_Guid"));Link(A,C->FindPinChecked(TEXT("InGuid")));return C->GetReturnValuePin();}
 UEdGraphPin* Match(UEdGraphPin* A,FName N){return And(ValidGuid(A),GuidEqual(A,N));}
 UEdGraphPin* NewId(FName N,UEdGraphPin* E){auto C=Call(UKismetGuidLibrary::StaticClass(),TEXT("NewGuid"));return Set(N,E,C->GetReturnValuePin());}
 UEdGraphPin* Flag(FName N,UEdGraphPin* E,bool V){return Set(N,E,nullptr,V?TEXT("true"):TEXT("false"));}
 UK2Node_CallFunction* Self(FName N){auto C=Node<UK2Node_CallFunction>();C->FunctionReference.SetSelfMember(N);C->AllocateDefaultPins();return C;}
 UEdGraphPin* Invoke(FName N,UEdGraphPin* E){auto C=Self(N);Link(E,C->GetExecPin());return C->GetThenPin();}
 UEdGraphPin* Emit(FName N,UEdGraphPin* E,TMap<FName,UEdGraphPin*> Args={}){auto C=Node<UK2Node_CallDelegate>();C->DelegateReference.SetSelfMember(N);C->AllocateDefaultPins();Link(E,C->GetExecPin());for(auto& A:Args)Link(A.Value,C->FindPinChecked(A.Key));return C->GetThenPin();}
 UEdGraphPin* Transition(UEdGraphPin* E,int32 PhaseIndex,UUserDefinedEnum* Enum){auto C=Self(TEXT("TryTransitionPhase"));Link(E,C->GetExecPin());S->TrySetDefaultValue(*C->FindPinChecked(TEXT("Target")),Enum->GetNameStringByIndex(PhaseIndex));return Guard(C->GetThenPin(),C->FindPinChecked(TEXT("bSucceeded")));}
 UEdGraphPin* ClearTimer(UEdGraphPin* E){auto C=Call(UKismetSystemLibrary::StaticClass(),TEXT("K2_ClearTimer"));S->TrySetDefaultValue(*C->FindPinChecked(TEXT("FunctionName")),TEXT("HandleCombatTimeout"));Link(E,C->GetExecPin());return C->GetThenPin();}
 UEdGraphPin* StartTimer(UEdGraphPin* E){auto C=Call(UKismetSystemLibrary::StaticClass(),TEXT("K2_SetTimer"));S->TrySetDefaultValue(*C->FindPinChecked(TEXT("FunctionName")),TEXT("HandleCombatTimeout"));S->TrySetDefaultValue(*C->FindPinChecked(TEXT("Time")),TEXT("60.0"));Link(E,C->GetExecPin());return Set(TEXT("CombatTimerHandle"),C->GetThenPin(),C->GetReturnValuePin());}
 UEdGraphPin* RequestPreparation(UEdGraphPin* E){E=NewId(TEXT("PreparationRequestId"),E);E=Flag(TEXT("bAwaitingPreparation"),E,true);return Emit(TEXT("OnRoundPreparationRequested"),E,{{TEXT("NextRoundNumber"),Get(TEXT("PendingNextRoundNumber"))},{TEXT("RequestId"),Get(TEXT("PreparationRequestId"))}});}
};
static bool FlowCompile(UBlueprint* B){FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);FCompilerResultsLog L;FKismetEditorUtilities::CompileBlueprint(B,EBlueprintCompileOptions::None,&L);return L.NumErrors==0&&B->Status!=BS_Error;}
static int32 CompleteFlow(const FString& Params){
 auto B=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager"));
 auto D=LoadObject<UDataTable>(nullptr,TEXT("/Game/BluePrints/GameFlow/DT_Rounds.DT_Rounds"));
 auto E=LoadObject<UUserDefinedEnum>(nullptr,TEXT("/Game/BluePrints/GameFlow/E_GamePhase.E_GamePhase"));
 auto GM=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameMode/BP_AutoBattleGameMode.BP_AutoBattleGameMode"));
 if(!B||!D||!E||!GM)return 50;
 if(Params.Contains(TEXT("Inspect"))){FString Text;
  TArray<UEdGraph*> Graphs;GM->GetAllGraphs(Graphs);for(auto G:Graphs){Text+=TEXT("GRAPH ")+G->GetName()+TEXT("\n");for(auto N:G->Nodes){Text+=TEXT("NODE ")+N->GetClass()->GetName()+TEXT(" ")+N->GetNodeTitle(ENodeTitleType::FullTitle).ToString()+TEXT("\n");for(auto P:N->Pins){Text+=TEXT("  ")+P->PinName.ToString()+TEXT(" = ")+P->DefaultValue+TEXT("\n");for(auto L:P->LinkedTo)Text+=TEXT("    -> ")+L->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString()+TEXT(".")+L->PinName.ToString()+TEXT("\n");}}}
  FFileHelper::SaveStringToFile(Text,*(FPaths::ProjectSavedDir()+TEXT("CodexGameModeGraph.txt")));return 0;}
 for(auto G:B->FunctionGraphs)if(G->GetFName()==TEXT("TryTransitionPhase"))return 51;
 // Preserve the previously implemented queries and validation.
 for(auto& V:B->NewVariables)V.PropertyFlags&=~CPF_BlueprintReadOnly;
 auto Type=[](FName Cat,UObject* Obj=nullptr){FEdGraphPinType T;T.PinCategory=Cat;T.PinSubCategoryObject=Obj;return T;};
 auto Bool=Type(UEdGraphSchema_K2::PC_Boolean),Int=Type(UEdGraphSchema_K2::PC_Int),Guid=Type(UEdGraphSchema_K2::PC_Struct,TBaseStructure<FGuid>::Get()),Phase=Type(UEdGraphSchema_K2::PC_Byte,E),Text=Type(UEdGraphSchema_K2::PC_Text);
 auto Add=[&](FName N,FEdGraphPinType T){FBlueprintEditorUtils::AddMemberVariable(B,N,T);};
 for(const TCHAR* N:{TEXT("EncounterId"),TEXT("DeploymentRequestId"),TEXT("RewardRequestId"),TEXT("PreparationRequestId")})Add(N,Guid);
 for(const TCHAR* N:{TEXT("bAwaitingDeployment"),TEXT("bAwaitingPreparation"),TEXT("bAwaitingReward"),TEXT("bCombatResolved"),TEXT("bTimedOut"),TEXT("bFinalPolicyRequested"),TEXT("bInitialized"),TEXT("bLastTransitionSucceeded")})Add(N,Bool);
 Add(TEXT("PreviousPhase"),Phase);
 for(const TCHAR* N:{TEXT("RequestedDeploymentLimit"),TEXT("RequestedDeploymentRound")})Add(N,Int);
 Add(TEXT("CombatTimerHandle"),Type(UEdGraphSchema_K2::PC_Struct,FTimerHandle::StaticStruct()));
 auto Dispatcher=[&](FName N,TArray<TPair<FName,FEdGraphPinType>> Args){
  Add(N,Type(UEdGraphSchema_K2::PC_MCDelegate));auto G=FBlueprintEditorUtils::CreateNewGraph(B,N,UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());G->bEditable=false;auto Schema=GetDefault<UEdGraphSchema_K2>();Schema->CreateDefaultNodesForGraph(*G);Schema->CreateFunctionGraphTerminators(*G,(UClass*)nullptr);Schema->AddExtraFunctionFlags(G,FUNC_BlueprintCallable|FUNC_BlueprintEvent|FUNC_Public);B->DelegateSignatureGraphs.Add(G);
  for(auto Node:G->Nodes)if(auto Entry=Cast<UK2Node_FunctionEntry>(Node))for(auto& A:Args)Entry->CreateUserDefinedPin(A.Key,A.Value,EGPD_Output);
 };
 Dispatcher(TEXT("OnPhaseChanged"),{{TEXT("Previous"),Phase},{TEXT("Current"),Phase}});
 Dispatcher(TEXT("OnStartAvailabilityChanged"),{{TEXT("bAllowed"),Bool},{TEXT("Message"),Text}});
 Dispatcher(TEXT("OnConfigurationError"),{{TEXT("Message"),Text}});
 Dispatcher(TEXT("OnCombatStartRequested"),{{TEXT("EncounterId"),Guid},{TEXT("RoundNumber"),Int}});
 for(const TCHAR* N:{TEXT("OnCombatStopRequested"),TEXT("OnCombatResolutionRequired")})Dispatcher(N,{{TEXT("EncounterId"),Guid},{TEXT("bTimedOut"),Bool}});
 Dispatcher(TEXT("OnDeploymentLimitChangeRequested"),{{TEXT("Limit"),Int},{TEXT("LimitRoundNumber"),Int},{TEXT("RequestId"),Guid}});
 Dispatcher(TEXT("OnRewardRequested"),{{TEXT("CompletedRoundNumber"),Int},{TEXT("RewardRequestId"),Guid}});
 Dispatcher(TEXT("OnRoundPreparationRequested"),{{TEXT("NextRoundNumber"),Int},{TEXT("RequestId"),Guid}});
 Dispatcher(TEXT("OnFinalRoundPolicyRequired"),{{TEXT("EncounterId"),Guid}});
 if(!FlowCompile(B))return 52;
 // Create the private transition function first so other graphs can call it.
 {
  FFlowBuilder F(B,TEXT("TryTransitionPhase"));F.Entry->SetExtraFlags(FUNC_Private|FUNC_BlueprintCallable);auto Target=F.In(TEXT("Target"),Phase);auto Success=F.Exit->CreateUserDefinedPin(TEXT("bSucceeded"),Bool,EGPD_Input);
  auto EqTarget=[&](int I){return F.Math(TEXT("EqualEqual_ByteByte"),Target,nullptr,FString::FromInt(I));};
  auto Allowed=F.And(F.Phase(0),EqTarget(1));
  Allowed=F.Or(Allowed,F.And(F.Phase(1),EqTarget(2)));Allowed=F.Or(Allowed,F.And(F.Phase(2),EqTarget(3)));
  Allowed=F.Or(Allowed,F.And(F.Phase(3),F.Or(EqTarget(4),EqTarget(5))));Allowed=F.Or(Allowed,F.And(F.Phase(4),F.Or(EqTarget(1),EqTarget(5))));
  F.Link(F.Get(TEXT("bLastTransitionSucceeded")),Success);
  auto Exec=F.Flag(TEXT("bLastTransitionSucceeded"),F.Entry->GetThenPin(),false);Exec=F.Guard(Exec,F.And(Allowed,F.Not(F.Get(TEXT("bTransitionInProgress")))));
  Exec=F.Flag(TEXT("bTransitionInProgress"),Exec,true);
  // Freeze previous before changing phase; pure variable reads cannot cache this value.
  Exec=F.Set(TEXT("PreviousPhase"),Exec,F.Get(TEXT("CurrentPhase")));Exec=F.Set(TEXT("CurrentPhase"),Exec,Target);
  Exec=F.Flag(TEXT("bCanStartCombat"),Exec,false);
  Exec=F.Emit(TEXT("OnPhaseChanged"),Exec,{{TEXT("Previous"),F.Get(TEXT("PreviousPhase"))},{TEXT("Current"),F.Get(TEXT("CurrentPhase"))}});
  Exec=F.Flag(TEXT("bTransitionInProgress"),Exec,false);
  Exec=F.Flag(TEXT("bLastTransitionSucceeded"),Exec,true);F.End(Exec);
 }
 if(!FlowCompile(B))return 53;
 {
  FFlowBuilder F(B,TEXT("RefreshStartAvailability"));auto Can=F.Self(TEXT("CanStartCombat"));auto Exec=F.Set(TEXT("bCanStartCombat"),F.Entry->GetThenPin(),Can->FindPinChecked(TEXT("bAllowed")));
  auto Empty=F.Branch(Exec,F.Math(TEXT("LessEqual_IntInt"),F.Get(TEXT("DeployedAllyCount")),nullptr,TEXT("0")));
  auto A=F.Set(TEXT("StartBlockMessage"),Empty->GetThenPin(),nullptr,TEXT("기물을 배치하세요"));
  auto Allowed=F.Branch(Empty->GetElsePin(),F.Get(TEXT("bCanStartCombat")));
  auto Good=F.Set(TEXT("StartBlockMessage"),Allowed->GetThenPin(),nullptr,TEXT(""));
  auto Bad=F.Set(TEXT("StartBlockMessage"),Allowed->GetElsePin(),nullptr,TEXT("배치 한도·드래그·준비 및 시스템 연결 상태를 확인하세요"));
  auto Emit=F.Node<UK2Node_CallDelegate>();Emit->DelegateReference.SetSelfMember(TEXT("OnStartAvailabilityChanged"));Emit->AllocateDefaultPins();F.Link(A,Emit->GetExecPin());F.Link(Good,Emit->GetExecPin());F.Link(Bad,Emit->GetExecPin());F.Link(F.Get(TEXT("bCanStartCombat")),Emit->FindPinChecked(TEXT("bAllowed")));F.Link(F.Get(TEXT("StartBlockMessage")),Emit->FindPinChecked(TEXT("Message")));F.End(Emit->GetThenPin());
 }
 if(!FlowCompile(B))return 54;
 {
  FFlowBuilder F(B,TEXT("ConfigureIntegration"));UEdGraphPin* Ready=nullptr;
  for(const TCHAR* N:{TEXT("TimerOwnershipConfirmed"),TEXT("PhaseSyncConfirmed"),TEXT("DeploymentAdapterReady"),TEXT("CombatAdapterReady"),TEXT("PreparationAdapterReady"),TEXT("RewardAdapterReady")}){auto P=F.Input(N,UEdGraphSchema_K2::PC_Boolean);Ready=Ready?F.And(Ready,P):P;}
  auto Exec=F.Guard(F.Entry->GetThenPin(),F.Phase(0));Exec=F.Set(TEXT("bIntegrationReady"),Exec,Ready);F.End(F.Invoke(TEXT("RefreshStartAvailability"),Exec));
 }
 {
  FFlowBuilder F(B,TEXT("InitializeFlow"));auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Phase(0),F.And(F.Get(TEXT("bIntegrationReady")),F.Not(F.Get(TEXT("bInitialized"))))));Exec=F.Invoke(TEXT("ValidateRoundData"),Exec);Exec=F.Guard(Exec,F.Get(TEXT("bRoundDataValid")));
  Exec=F.Flag(TEXT("bInitialized"),Exec,true);Exec=F.Set(TEXT("PendingNextRoundNumber"),Exec,nullptr,TEXT("1"));Exec=F.Set(TEXT("RequestedDeploymentLimit"),Exec,nullptr,TEXT("2"));Exec=F.Set(TEXT("RequestedDeploymentRound"),Exec,nullptr,TEXT("1"));Exec=F.NewId(TEXT("DeploymentRequestId"),Exec);Exec=F.Flag(TEXT("bAwaitingDeployment"),Exec,true);
  F.End(F.Emit(TEXT("OnDeploymentLimitChangeRequested"),Exec,{{TEXT("Limit"),F.Get(TEXT("RequestedDeploymentLimit"))},{TEXT("LimitRoundNumber"),F.Get(TEXT("RequestedDeploymentRound"))},{TEXT("RequestId"),F.Get(TEXT("DeploymentRequestId"))}}));
 }
 // Declare reward entry before acknowledgement functions reference it.
 {
  FFlowBuilder F(B,TEXT("EnterReward"));F.Entry->SetExtraFlags(FUNC_Private|FUNC_BlueprintCallable);auto Exec=F.Guard(F.Entry->GetThenPin(),F.Phase(3));Exec=F.NewId(TEXT("RewardRequestId"),Exec);Exec=F.Flag(TEXT("bAwaitingReward"),Exec,true);Exec=F.Transition(Exec,4,E);
  F.End(F.Emit(TEXT("OnRewardRequested"),Exec,{{TEXT("CompletedRoundNumber"),F.Get(TEXT("CurrentRoundNumber"))},{TEXT("RewardRequestId"),F.Get(TEXT("RewardRequestId"))}}));
 }
 if(!FlowCompile(B))return 55;
 {
  FFlowBuilder F(B,TEXT("NotifyDeploymentLimitApplied"));auto Id=F.Guid(TEXT("RequestId"));auto OK=F.Input(TEXT("bSuccess"),UEdGraphSchema_K2::PC_Boolean);
  auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Or(F.Phase(0),F.Phase(3)),F.And(F.Get(TEXT("bAwaitingDeployment")),F.Match(Id,TEXT("DeploymentRequestId")))));
  // Failed applications remain pending so the adapter can retry this request.
  Exec=F.Guard(Exec,OK);Exec=F.Flag(TEXT("bAwaitingDeployment"),Exec,false);Exec=F.Set(TEXT("EffectiveDeploymentLimit"),Exec,F.Get(TEXT("RequestedDeploymentLimit")));Exec=F.Set(TEXT("DeploymentLimitRoundNumber"),Exec,F.Get(TEXT("RequestedDeploymentRound")));
  auto Init=F.Branch(Exec,F.Phase(0));F.End(F.RequestPreparation(Init->GetThenPin()));F.End(F.Invoke(TEXT("EnterReward"),Init->GetElsePin()));
 }
 {
  FFlowBuilder F(B,TEXT("NotifyRoundPreparationCompleted"));auto Id=F.Guid(TEXT("RequestId"));auto OK=F.Input(TEXT("bSuccess"),UEdGraphSchema_K2::PC_Boolean);
  auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Or(F.Phase(0),F.Phase(4)),F.And(F.Get(TEXT("bAwaitingPreparation")),F.Match(Id,TEXT("PreparationRequestId")))));Exec=F.Guard(Exec,OK);Exec=F.Invoke(TEXT("ValidateRoundData"),Exec);Exec=F.Guard(Exec,F.Get(TEXT("bRoundDataValid")));
  Exec=F.Flag(TEXT("bAwaitingPreparation"),Exec,false);Exec=F.Set(TEXT("CurrentRoundNumber"),Exec,F.Get(TEXT("PendingNextRoundNumber")));
  // Read the row for the newly prepared round from the configured table.
  auto Row=F.Node<UK2Node_GetDataTableRow>();Row->AllocateDefaultPins();F.S->TrySetDefaultObject(*Row->GetDataTablePin(),D);Row->PinDefaultValueChanged(Row->GetDataTablePin());F.Link(F.Get(TEXT("RoundsTable")),Row->GetDataTablePin());
  auto Num=F.Call(UKismetStringLibrary::StaticClass(),TEXT("Conv_IntToString"));F.Link(F.Get(TEXT("CurrentRoundNumber")),Num->FindPinChecked(TEXT("InInt")));
  auto Cat=F.Call(UKismetStringLibrary::StaticClass(),TEXT("Concat_StrStr"));F.S->TrySetDefaultValue(*Cat->FindPinChecked(TEXT("A")),TEXT("R"));F.Link(Num->GetReturnValuePin(),Cat->FindPinChecked(TEXT("B")));
  auto Name=F.Call(UKismetStringLibrary::StaticClass(),TEXT("Conv_StringToName"));F.Link(Cat->GetReturnValuePin(),Name->FindPinChecked(TEXT("InString")));F.Link(Name->GetReturnValuePin(),Row->GetRowNamePin());F.Link(Exec,Row->GetExecPin());F.Link(Row->GetRowNotFoundPin(),F.Exit->GetExecPin());
  Exec=F.Set(TEXT("CurrentRoundDefinition"),Row->GetThenPin(),Row->GetResultPin());Exec=F.Transition(Exec,1,E);F.End(F.Invoke(TEXT("RefreshStartAvailability"),Exec));
 }
 {
  FFlowBuilder F(B,TEXT("HandleCombatTimeout"));F.Entry->SetExtraFlags(FUNC_Private|FUNC_BlueprintCallable);auto Exec=F.Guard(F.Entry->GetThenPin(),F.Phase(2));Exec=F.ClearTimer(Exec);Exec=F.Flag(TEXT("bTimedOut"),Exec,true);Exec=F.Flag(TEXT("bCombatResolved"),Exec,false);Exec=F.Transition(Exec,3,E);
  Exec=F.Emit(TEXT("OnCombatStopRequested"),Exec,{{TEXT("EncounterId"),F.Get(TEXT("EncounterId"))},{TEXT("bTimedOut"),F.Get(TEXT("bTimedOut"))}});
  F.End(F.Emit(TEXT("OnCombatResolutionRequired"),Exec,{{TEXT("EncounterId"),F.Get(TEXT("EncounterId"))},{TEXT("bTimedOut"),F.Get(TEXT("bTimedOut"))}}));
 }
 {
  FFlowBuilder F(B,TEXT("RequestCombatStart"));auto Can=F.Self(TEXT("CanStartCombat"));auto Exec=F.Guard(F.Entry->GetThenPin(),Can->FindPinChecked(TEXT("bAllowed")));Exec=F.NewId(TEXT("EncounterId"),Exec);Exec=F.Flag(TEXT("bCombatResolved"),Exec,false);Exec=F.Flag(TEXT("bTimedOut"),Exec,false);Exec=F.Flag(TEXT("bFinalPolicyRequested"),Exec,false);Exec=F.Transition(Exec,2,E);Exec=F.StartTimer(Exec);
  F.End(F.Emit(TEXT("OnCombatStartRequested"),Exec,{{TEXT("EncounterId"),F.Get(TEXT("EncounterId"))},{TEXT("RoundNumber"),F.Get(TEXT("CurrentRoundNumber"))}}));
 }
 {
  FFlowBuilder F(B,TEXT("NotifyCombatFinished"));auto Id=F.Guid(TEXT("CompletedEncounterId"));auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Phase(2),F.Match(Id,TEXT("EncounterId"))));Exec=F.ClearTimer(Exec);Exec=F.Flag(TEXT("bTimedOut"),Exec,false);Exec=F.Flag(TEXT("bCombatResolved"),Exec,false);Exec=F.Transition(Exec,3,E);
  Exec=F.Emit(TEXT("OnCombatStopRequested"),Exec,{{TEXT("EncounterId"),F.Get(TEXT("EncounterId"))},{TEXT("bTimedOut"),F.Get(TEXT("bTimedOut"))}});F.End(F.Emit(TEXT("OnCombatResolutionRequired"),Exec,{{TEXT("EncounterId"),F.Get(TEXT("EncounterId"))},{TEXT("bTimedOut"),F.Get(TEXT("bTimedOut"))}}));
 }
 {
  FFlowBuilder F(B,TEXT("NotifyCombatResolutionCompleted"));auto Id=F.Guid(TEXT("CompletedEncounterId"));auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Phase(3),F.Match(Id,TEXT("EncounterId"))));F.End(F.Flag(TEXT("bCombatResolved"),Exec,true));
 }
 {
  FFlowBuilder F(B,TEXT("ConfirmProgressionToReward"));auto Id=F.Guid(TEXT("CompletedEncounterId"));auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Phase(3),F.And(F.Match(Id,TEXT("EncounterId")),F.And(F.Get(TEXT("bCombatResolved")),F.Not(F.Get(TEXT("bAwaitingDeployment")))))));
  auto Last=F.Branch(Exec,F.Eq(TEXT("CurrentRoundNumber"),5));auto Final=F.Guard(Last->GetThenPin(),F.Not(F.Get(TEXT("bFinalPolicyRequested"))));Final=F.Flag(TEXT("bFinalPolicyRequested"),Final,true);F.End(F.Emit(TEXT("OnFinalRoundPolicyRequired"),Final,{{TEXT("EncounterId"),F.Get(TEXT("EncounterId"))}}));
  Exec=Last->GetElsePin();Exec=F.Guard(Exec,F.And(F.Math(TEXT("GreaterEqual_IntInt"),F.Get(TEXT("CurrentRoundNumber")),nullptr,TEXT("1")),F.Math(TEXT("Less_IntInt"),F.Get(TEXT("CurrentRoundNumber")),nullptr,TEXT("5"))));
  Exec=F.Set(TEXT("PendingNextRoundNumber"),Exec,F.Math(TEXT("Add_IntInt"),F.Get(TEXT("CurrentRoundNumber")),nullptr,TEXT("1")));Exec=F.Set(TEXT("RequestedDeploymentRound"),Exec,F.Get(TEXT("PendingNextRoundNumber")));Exec=F.Set(TEXT("RequestedDeploymentLimit"),Exec,F.Math(TEXT("Add_IntInt"),F.Get(TEXT("PendingNextRoundNumber")),nullptr,TEXT("1")));Exec=F.NewId(TEXT("DeploymentRequestId"),Exec);Exec=F.Flag(TEXT("bAwaitingDeployment"),Exec,true);
  F.End(F.Emit(TEXT("OnDeploymentLimitChangeRequested"),Exec,{{TEXT("Limit"),F.Get(TEXT("RequestedDeploymentLimit"))},{TEXT("LimitRoundNumber"),F.Get(TEXT("RequestedDeploymentRound"))},{TEXT("RequestId"),F.Get(TEXT("DeploymentRequestId"))}}));
 }
 {
  FFlowBuilder F(B,TEXT("NotifyRewardSelectionCompleted"));auto Id=F.Guid(TEXT("CompletedRewardRequestId"));auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Phase(4),F.And(F.Get(TEXT("bAwaitingReward")),F.Match(Id,TEXT("RewardRequestId")))));Exec=F.Flag(TEXT("bAwaitingReward"),Exec,false);auto Last=F.Branch(Exec,F.Eq(TEXT("CurrentRoundNumber"),5));F.End(F.Transition(Last->GetThenPin(),5,E));F.End(F.RequestPreparation(Last->GetElsePin()));
 }
 {
  FFlowBuilder F(B,TEXT("ResolveFinalRoundPolicy"));auto Id=F.Guid(TEXT("CompletedEncounterId"));auto Reward=F.Input(TEXT("bGrantFinalReward"),UEdGraphSchema_K2::PC_Boolean);auto Exec=F.Guard(F.Entry->GetThenPin(),F.And(F.Phase(3),F.And(F.Eq(TEXT("CurrentRoundNumber"),5),F.And(F.Get(TEXT("bFinalPolicyRequested")),F.Match(Id,TEXT("EncounterId"))))));Exec=F.Flag(TEXT("bFinalPolicyRequested"),Exec,false);auto Choice=F.Branch(Exec,Reward);F.End(F.Invoke(TEXT("EnterReward"),Choice->GetThenPin()));F.End(F.Transition(Choice->GetElsePin(),5,E));
 }
 if(!FlowCompile(B))return 56;
 // Refresh availability after every delivered interaction snapshot.
 for(auto G:B->FunctionGraphs)if(G->GetFName()==TEXT("UpdateInteractionState")){
  TArray<UK2Node_FunctionResult*> Results;for(auto N:G->Nodes)if(auto R=Cast<UK2Node_FunctionResult>(N))Results.Add(R);
  for(auto R:Results){auto Pin=R->GetExecPin();auto Links=Pin->LinkedTo;Pin->BreakAllPinLinks();auto C=NewObject<UK2Node_CallFunction>(G);G->AddNode(C);C->CreateNewGuid();C->FunctionReference.SetSelfMember(TEXT("RefreshStartAvailability"));C->AllocateDefaultPins();for(auto L:Links)GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(L,C->GetExecPin());GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(C->GetThenPin(),Pin);}
 }
 // State may be queried but external graphs cannot write it directly.
 for(auto& V:B->NewVariables)if(V.VarType.PinCategory!=UEdGraphSchema_K2::PC_MCDelegate&&V.VarName!=TEXT("RoundsTable"))FBlueprintEditorUtils::SetBlueprintVariableMetaData(B,V.VarName,nullptr,TEXT("BlueprintPrivate"),TEXT("true"));
 if(!FlowCompile(B))return 57;
 SaveFlow(B);UE_LOG(LogTemp,Display,TEXT("FLOW_COMPLETE_OK: guarded phase machine, timed combat, correlated deployment/reward/preparation acknowledgements and final policy."));return 0;
}
