static int32 ConnectFlow(){
 auto B=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager"));
 auto GM=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameMode/BP_AutoBattleGameMode.BP_AutoBattleGameMode"));if(!B||!GM)return 80;
 for(auto G:GM->FunctionGraphs)if(G->GetFName()==TEXT("EnsureRoundManager"))return 81;
 FEdGraphPinType T;T.PinCategory=UEdGraphSchema_K2::PC_Object;T.PinSubCategoryObject=B->GeneratedClass;
 if(!FBlueprintEditorUtils::AddMemberVariable(GM,TEXT("RoundManagerReference"),T))return 82;if(!FlowCompile(GM))return 83;
 {
  FFlowBuilder F(GM,TEXT("EnsureRoundManager"));auto Valid=F.Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"));F.Link(F.Get(TEXT("RoundManagerReference")),Valid->FindPinChecked(TEXT("Object")));auto Check=F.Branch(F.Entry->GetThenPin(),Valid->GetReturnValuePin());F.End(Check->GetThenPin());
  auto Spawn=F.Node<UK2Node_SpawnActorFromClass>();Spawn->AllocateDefaultPins();F.S->TrySetDefaultObject(*Spawn->FindPinChecked(TEXT("Class")),B->GeneratedClass);Spawn->ReconstructNode();
  auto Transform=F.Call(UKismetMathLibrary::StaticClass(),TEXT("MakeTransform"));F.Link(Transform->GetReturnValuePin(),Spawn->FindPinChecked(TEXT("SpawnTransform")));
  F.Link(Check->GetElsePin(),Spawn->GetExecPin());auto Exec=F.Set(TEXT("RoundManagerReference"),Spawn->GetThenPin(),Spawn->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue));F.End(Exec);
 }
 {
  FFlowBuilder F(GM,TEXT("GetRoundManager"));F.Entry->AddExtraFlags(FUNC_BlueprintPure|FUNC_Public);auto Value=F.Get(TEXT("RoundManagerReference"));F.Link(Value,F.Exit->CreateUserDefinedPin(TEXT("RoundManager"),Value->PinType,EGPD_Input));F.End(F.Entry->GetThenPin());
 }
 if(!FlowCompile(GM))return 84;
 {
  FFlowBuilder F(GM,TEXT("RequestValidatedCombatStart"));auto Exec=F.Invoke(TEXT("EnsureRoundManager"),F.Entry->GetThenPin());auto Valid=F.Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"));F.Link(F.Get(TEXT("RoundManagerReference")),Valid->FindPinChecked(TEXT("Object")));Exec=F.Guard(Exec,Valid->GetReturnValuePin());
  auto Refresh=F.Call(B->GeneratedClass,TEXT("RefreshStartAvailability"));F.Link(F.Get(TEXT("RoundManagerReference")),Refresh->FindPinChecked(UEdGraphSchema_K2::PN_Self));F.Link(Exec,Refresh->GetExecPin());
  auto Request=F.Call(B->GeneratedClass,TEXT("RequestCombatStart"));F.Link(F.Get(TEXT("RoundManagerReference")),Request->FindPinChecked(UEdGraphSchema_K2::PN_Self));F.Link(Refresh->GetThenPin(),Request->GetExecPin());F.End(Request->GetThenPin());
 }
 if(!FlowCompile(GM))return 85;
 TArray<UEdGraph*> Graphs;GM->GetAllGraphs(Graphs);
 for(auto G:Graphs){TArray<UK2Node_Event*> Events;for(auto N:G->Nodes)if(auto Event=Cast<UK2Node_Event>(N))Events.Add(Event);
  for(auto Event:Events){auto Name=Event->EventReference.GetMemberName();auto Exec=Event->FindPin(UEdGraphSchema_K2::PN_Then);if(!Exec)continue;
   if(Name==TEXT("ReceiveBoardReady")){
    auto Links=Exec->LinkedTo;Exec->BreakAllPinLinks();auto Ensure=NewObject<UK2Node_CallFunction>(G);G->AddNode(Ensure);Ensure->CreateNewGuid();Ensure->FunctionReference.SetSelfMember(TEXT("EnsureRoundManager"));Ensure->AllocateDefaultPins();auto Schema=GetDefault<UEdGraphSchema_K2>();Schema->TryCreateConnection(Exec,Ensure->GetExecPin());for(auto L:Links)Schema->TryCreateConnection(Ensure->GetThenPin(),L);
   }
   if(Name==TEXT("ReceiveCombatStartRequested")){
    Exec->BreakAllPinLinks();auto Request=NewObject<UK2Node_CallFunction>(G);G->AddNode(Request);Request->CreateNewGuid();Request->FunctionReference.SetSelfMember(TEXT("RequestValidatedCombatStart"));Request->AllocateDefaultPins();GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Exec,Request->GetExecPin());
   }
  }
 }
 FBlueprintEditorUtils::SetBlueprintVariableMetaData(GM,TEXT("RoundManagerReference"),nullptr,TEXT("BlueprintPrivate"),TEXT("true"));
 if(!FlowCompile(GM))return 86;SaveFlow(GM);UE_LOG(LogTemp,Display,TEXT("FLOW_CONNECT_OK: one manager on Board Ready, public getter, all native start requests go through validation; unconfirmed adapters block combat."));return 0;
}
