static int32 FinalizeFlow(){
 auto B=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager"));
 auto GM=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameMode/BP_AutoBattleGameMode.BP_AutoBattleGameMode"));if(!B||!GM)return 60;
 for(auto G:B->FunctionGraphs)if(G->GetFName()==TEXT("GetFlowSnapshot"))return 61;
 {
  FFlowBuilder F(B,TEXT("TryTransitionPhase"),true);auto Target=F.Entry->FindPinChecked(TEXT("Target"));
  UK2Node_IfThenElse* Gate=nullptr;for(auto N:F.G->Nodes)if(auto C=Cast<UK2Node_IfThenElse>(N)){Gate=C;break;}check(Gate);
  auto Old=Gate->GetConditionPin()->LinkedTo[0];Gate->GetConditionPin()->BreakAllPinLinks();
  auto Is=[&](int I){return F.Math(TEXT("EqualEqual_ByteByte"),Target,nullptr,FString::FromInt(I));};
  auto NotIs=[&](int I){return F.Not(Is(I));};
  auto Prep=F.And(F.Get(TEXT("bRoundDataValid")),F.And(F.Get(TEXT("bIntegrationReady")),F.And(F.Not(F.Get(TEXT("bAwaitingPreparation"))),F.Not(F.Get(TEXT("bAwaitingDeployment"))))));
  Prep=F.And(Prep,F.Math(TEXT("EqualEqual_IntInt"),F.Get(TEXT("DeploymentLimitRoundNumber")),F.Get(TEXT("CurrentRoundNumber"))));
  auto Can=F.Self(TEXT("CanStartCombat"));auto Requirements=F.And(F.Or(NotIs(1),Prep),F.Or(NotIs(2),Can->FindPinChecked(TEXT("bAllowed"))));
  Requirements=F.And(Requirements,F.Or(NotIs(3),F.ValidGuid(F.Get(TEXT("EncounterId")))));
  auto NextLimit=F.Math(TEXT("EqualEqual_IntInt"),F.Get(TEXT("DeploymentLimitRoundNumber")),F.Math(TEXT("Add_IntInt"),F.Get(TEXT("CurrentRoundNumber")),nullptr,TEXT("1")));
  auto Reward=F.And(F.Get(TEXT("bCombatResolved")),F.And(F.Not(F.Get(TEXT("bAwaitingDeployment"))),F.Or(F.Eq(TEXT("CurrentRoundNumber"),5),NextLimit)));
  Requirements=F.And(Requirements,F.Or(NotIs(4),Reward));
  auto Finish=F.And(F.Eq(TEXT("CurrentRoundNumber"),5),F.And(F.Not(F.Get(TEXT("bAwaitingReward"))),F.Not(F.Get(TEXT("bFinalPolicyRequested")))));
  Requirements=F.And(Requirements,F.Or(NotIs(5),Finish));F.Link(F.And(Old,Requirements),Gate->GetConditionPin());
 }
 // Expose read-only snapshots instead of public state setters.
 {
  FFlowBuilder F(B,TEXT("GetFlowSnapshot"));F.Entry->AddExtraFlags(FUNC_BlueprintPure|FUNC_Public);
  for(const TCHAR* Name:{TEXT("CurrentPhase"),TEXT("CurrentRoundNumber"),TEXT("PendingNextRoundNumber"),TEXT("EffectiveDeploymentLimit"),TEXT("DeploymentLimitRoundNumber"),TEXT("EncounterId"),TEXT("DeploymentRequestId"),TEXT("RewardRequestId"),TEXT("PreparationRequestId"),TEXT("bTimedOut"),TEXT("bIntegrationReady"),TEXT("bRoundDataValid")}){
   auto Value=F.Get(Name);auto Output=F.Exit->CreateUserDefinedPin(Name,Value->PinType,EGPD_Input);F.Link(Value,Output);
  }F.End(F.Entry->GetThenPin());
 }
 {
  FFlowBuilder F(B,TEXT("GetStartAvailability"));F.Entry->AddExtraFlags(FUNC_BlueprintPure|FUNC_Public);auto Can=F.Self(TEXT("CanStartCombat"));auto V=Can->FindPinChecked(TEXT("bAllowed"));F.Link(V,F.Exit->CreateUserDefinedPin(TEXT("bAllowed"),V->PinType,EGPD_Input));auto Message=F.Get(TEXT("StartBlockMessage"));F.Link(Message,F.Exit->CreateUserDefinedPin(TEXT("Message"),Message->PinType,EGPD_Input));F.End(F.Entry->GetThenPin());
 }
 // Emit an explicit configuration error on invalid table data.
 {
  FFlowBuilder F(B,TEXT("ValidateRoundData"),true);TArray<UK2Node_FunctionResult*> Exits;for(auto N:F.G->Nodes)if(auto R=Cast<UK2Node_FunctionResult>(N))Exits.Add(R);
  for(auto R:Exits){auto Pin=R->GetExecPin();auto Links=Pin->LinkedTo;Pin->BreakAllPinLinks();auto Check=F.Node<UK2Node_IfThenElse>();Check->AllocateDefaultPins();for(auto L:Links)F.Link(L,Check->GetExecPin());F.Link(F.Get(TEXT("bRoundDataValid")),Check->GetConditionPin());F.Link(Check->GetThenPin(),Pin);
   auto Error=F.Node<UK2Node_CallDelegate>();Error->DelegateReference.SetSelfMember(TEXT("OnConfigurationError"));Error->AllocateDefaultPins();F.S->TrySetDefaultValue(*Error->FindPinChecked(TEXT("Message")),TEXT("DT_Rounds에는 R1~R5와 출전 한도 2~6, 전투 시간 60초가 필요합니다."));F.Link(Check->GetElsePin(),Error->GetExecPin());F.Link(Error->GetThenPin(),Pin);
  }
 }
 // Round lookup uses the selected table, not a hidden fixed data source.
 for(auto G:B->FunctionGraphs)if(G->GetFName()==TEXT("GetRoundDefinition"))for(auto N:G->Nodes)if(auto Row=Cast<UK2Node_GetDataTableRow>(N)){
  auto Get=NewObject<UK2Node_VariableGet>(G);G->AddNode(Get);Get->CreateNewGuid();Get->VariableReference.SetSelfMember(TEXT("RoundsTable"));Get->AllocateDefaultPins();GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Get->GetValuePin(),Row->GetDataTablePin());break;
 }
 if(!FlowCompile(B))return 62;
 // Removing automatic preparation expiry is safe within the allowed GameMode asset.
 auto GMClass=GM->GeneratedClass;auto Defaults=GMClass->GetDefaultObject();auto Duration=FindFProperty<FFloatProperty>(GMClass,TEXT("PreparationDuration"));if(!Duration)return 63;Duration->SetPropertyValue_InContainer(Defaults,0.f);
 // The existing expiry -> start link is explicitly disconnected as a second guard.
 TArray<UEdGraph*> Graphs;GM->GetAllGraphs(Graphs);for(auto G:Graphs)for(auto N:G->Nodes)if(auto Event=Cast<UK2Node_Event>(N))if(Event->EventReference.GetMemberName()==TEXT("ReceivePreparationExpired"))if(auto P=Event->FindPin(UEdGraphSchema_K2::PN_Then))P->BreakAllPinLinks();
 // Preserve the battle-result event's actual enum argument in its existing switch.
 for(auto G:Graphs)for(auto N:G->Nodes)if(auto Event=Cast<UK2Node_Event>(N))if(Event->EventReference.GetMemberName()==TEXT("ReceiveBattleResultDetected")){
  auto Exec=Event->FindPin(UEdGraphSchema_K2::PN_Then);auto Result=Event->FindPin(TEXT("Result"));if(Exec&&Result)for(auto L:Exec->LinkedTo)if(auto Switch=Cast<UK2Node_SwitchEnum>(L->GetOwningNode()))GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Result,Switch->GetSelectionPin());
 }
 if(!FlowCompile(GM))return 64;Duration=FindFProperty<FFloatProperty>(GM->GeneratedClass,TEXT("PreparationDuration"));Duration->SetPropertyValue_InContainer(GM->GeneratedClass->GetDefaultObject(),0.f);
 SaveFlow(B);SaveFlow(GM);UE_LOG(LogTemp,Display,TEXT("FLOW_FINALIZE_OK: centralized transition guards, snapshot getters, configuration errors; GameMode manual preparation and result-pin repair."));return 0;
}

