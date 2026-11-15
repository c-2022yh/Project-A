static void ArrangeFlowGraph(UEdGraph* G){
 TMap<UEdGraphNode*,int32> Depth;for(auto N:G->Nodes)Depth.Add(N,0);
 for(int32 I=0;I<G->Nodes.Num();++I){bool Changed=false;for(auto N:G->Nodes)for(auto P:N->Pins)if(P->Direction==EGPD_Output)for(auto L:P->LinkedTo){auto Next=L->GetOwningNode();if(Next!=N&&Depth.Contains(Next)&&Depth[Next]<Depth[N]+1){Depth[Next]=Depth[N]+1;Changed=true;}}if(!Changed)break;}
 TMap<int32,int32> Y;for(auto N:G->Nodes){int32 D=FMath::Min(Depth[N],G->Nodes.Num());int32 Inputs=0,Outputs=0;for(auto P:N->Pins)if(!P->bHidden){if(P->Direction==EGPD_Input)++Inputs;else ++Outputs;}N->NodePosX=D*640;N->NodePosY=Y.FindOrAdd(D);Y[D]+=FMath::Max(Inputs,Outputs)*26+150;}
}
static int32 PolishFlow(){
 auto B=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager"));auto GM=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameMode/BP_AutoBattleGameMode.BP_AutoBattleGameMode"));if(!B||!GM)return 90;
 // Existing native restart deletes registered units and has no agreed reset policy.
 TArray<UEdGraph*> Graphs;GM->GetAllGraphs(Graphs);for(auto G:Graphs)for(auto N:G->Nodes)if(auto Event=Cast<UK2Node_Event>(N)){
  auto Name=Event->EventReference.GetMemberName();if(Name==TEXT("ReceiveRestartRequested")){if(auto P=Event->FindPin(UEdGraphSchema_K2::PN_Then))P->BreakAllPinLinks();Event->NodeComment=TEXT("재시작 정책 미정: 유닛을 삭제하는 기존 RestartBattle 자동 호출을 차단합니다.");Event->bCommentBubbleVisible=true;}
  if(Name==TEXT("ReceivePreparationExpired")){Event->NodeComment=TEXT("준비 시간 무제한. 시간 경과로 전투를 시작하지 않습니다.");Event->bCommentBubbleVisible=true;}
  if(Name==TEXT("ReceiveCombatStartRequested")){Event->NodeComment=TEXT("모든 시작 요청은 RoundManager 검증을 거칩니다. 외부 연동 확인 전 시작 차단.");Event->bCommentBubbleVisible=true;}
 }
 // Explicitly remove this actor's timer at EndPlay as well as at combat completion.
 UEdGraph* EventGraph=B->UbergraphPages.IsEmpty()?nullptr:B->UbergraphPages[0];if(!EventGraph){EventGraph=FBlueprintEditorUtils::CreateNewGraph(B,TEXT("EventGraph"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());FBlueprintEditorUtils::AddUbergraphPage(B,EventGraph);}
 UK2Node_Event* End=nullptr;for(auto N:EventGraph->Nodes)if(auto Ev=Cast<UK2Node_Event>(N))if(Ev->EventReference.GetMemberName()==TEXT("ReceiveEndPlay"))End=Ev;
 if(!End){End=NewObject<UK2Node_Event>(EventGraph);EventGraph->AddNode(End);End->CreateNewGuid();End->EventReference.SetExternalMember(TEXT("ReceiveEndPlay"),AActor::StaticClass());End->bOverrideFunction=true;End->AllocateDefaultPins();}
 if(End->GetThenPin()->LinkedTo.IsEmpty()){auto Clear=NewObject<UK2Node_CallFunction>(EventGraph);EventGraph->AddNode(Clear);Clear->CreateNewGuid();Clear->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("K2_ClearTimer")));Clear->AllocateDefaultPins();auto Schema=GetDefault<UEdGraphSchema_K2>();Schema->TrySetDefaultValue(*Clear->FindPinChecked(TEXT("FunctionName")),TEXT("HandleCombatTimeout"));Schema->TryCreateConnection(End->GetThenPin(),Clear->GetExecPin());}
 for(auto G:B->FunctionGraphs)ArrangeFlowGraph(G);ArrangeFlowGraph(EventGraph);
 for(auto G:GM->FunctionGraphs)if(G->GetFName()==TEXT("EnsureRoundManager")||G->GetFName()==TEXT("GetRoundManager")||G->GetFName()==TEXT("RequestValidatedCombatStart"))ArrangeFlowGraph(G);
 if(!FlowCompile(B)||!FlowCompile(GM))return 91;SaveFlow(B);SaveFlow(GM);UE_LOG(LogTemp,Display,TEXT("FLOW_POLISH_OK: EndPlay timer cleanup, undefined restart blocked, readable generated graph layout."));return 0;
}
