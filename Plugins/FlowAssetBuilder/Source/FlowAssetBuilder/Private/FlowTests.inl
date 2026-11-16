struct FFlowTest {
 AActor* Actor; UClass* C; int32 Failures=0; int32 Checks=0;
 FFlowTest(AActor* A):Actor(A),C(A->GetClass()){}
 void Check(bool OK,const TCHAR* Message){++Checks;if(!OK){++Failures;UE_LOG(LogTemp,Error,TEXT("FLOW_TEST_FAIL: %s"),Message);}}
 FProperty* Prop(FName N){auto P=C->FindPropertyByName(N);check(P);return P;}
 int32 Int(FName N){return CastFieldChecked<FIntProperty>(Prop(N))->GetPropertyValue_InContainer(Actor);}
 bool Bool(FName N){return CastFieldChecked<FBoolProperty>(Prop(N))->GetPropertyValue_InContainer(Actor);}
 int32 Phase(){return CastFieldChecked<FByteProperty>(Prop(TEXT("CurrentPhase")))->GetPropertyValue_InContainer(Actor);}
 FGuid Id(FName N){return *Prop(N)->ContainerPtrToValuePtr<FGuid>(Actor);}
 void Call(FName N,TMap<FName,bool> Bools={},TMap<FName,int32> Ints={},TMap<FName,FGuid> Guids={}){
  auto F=C->FindFunctionByName(N);check(F);FStructOnScope Args(F);
  for(auto& A:Bools)FindFProperty<FBoolProperty>(F,A.Key)->SetPropertyValue_InContainer(Args.GetStructMemory(),A.Value);
  for(auto& A:Ints)FindFProperty<FIntProperty>(F,A.Key)->SetPropertyValue_InContainer(Args.GetStructMemory(),A.Value);
  for(auto& A:Guids)*FindFProperty<FStructProperty>(F,A.Key)->ContainerPtrToValuePtr<FGuid>(Args.GetStructMemory())=A.Value;
  FEditorScriptExecutionGuard Guard;Actor->ProcessEvent(F,Args.GetStructMemory());
 }
 bool Can(){auto F=C->FindFunctionByName(TEXT("CanStartCombat"));FStructOnScope Args(F);FEditorScriptExecutionGuard Guard;Actor->ProcessEvent(F,Args.GetStructMemory());return FindFProperty<FBoolProperty>(F,TEXT("bAllowed"))->GetPropertyValue_InContainer(Args.GetStructMemory());}
 void Interaction(int32 Count,bool Unit=false,bool Item=false){Call(TEXT("UpdateInteractionState"),{{TEXT("UnitDragging"),Unit},{TEXT("ItemDragging"),Item}},{{TEXT("AllyCount"),Count}});}
 void AckLimit(bool Success=true,FGuid Override=FGuid()){
  Call(TEXT("NotifyDeploymentLimitApplied"),{{TEXT("bSuccess"),Success}},{},{{TEXT("RequestId"),Override.IsValid()?Override:Id(TEXT("DeploymentRequestId"))}});
 }
 void AckPrep(bool Success=true){Call(TEXT("NotifyRoundPreparationCompleted"),{{TEXT("bSuccess"),Success}},{},{{TEXT("RequestId"),Id(TEXT("PreparationRequestId"))}});}
 void Resolve(){Call(TEXT("NotifyCombatResolutionCompleted"),{},{},{{TEXT("CompletedEncounterId"),Id(TEXT("EncounterId"))}});}
 void Progress(){Call(TEXT("ConfirmProgressionToReward"),{},{},{{TEXT("CompletedEncounterId"),Id(TEXT("EncounterId"))}});}
};
static int32 VerifyCompleteFlow(){
 FEditorScriptExecutionGuard TestScriptGuard;
 auto B=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameFlow/BP_RoundManager.BP_RoundManager"));
 auto GM=LoadObject<UBlueprint>(nullptr,TEXT("/Game/BluePrints/GameMode/BP_AutoBattleGameMode.BP_AutoBattleGameMode"));
 if(!B||!GM||!FlowCompile(B)||!FlowCompile(GM))return 70;
 auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
 auto W=UWorld::CreateWorld(EWorldType::Game,false,TEXT("CodexRoundFlowTest"),nullptr,true,ERHIFeatureLevel::Num,&Init);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 int32 Failures=0,Checks=0;
 for(bool FinalReward:{false,true}){
  auto Actor=W->SpawnActor<AActor>(B->GeneratedClass);if(!Actor)return 71;FFlowTest T(Actor);
  T.Call(TEXT("InitializeFlow"));T.Check(!T.Bool(TEXT("bInitialized")),TEXT("Initialization must wait for confirmed integration"));
  T.Call(TEXT("ConfigureIntegration"),{{TEXT("TimerOwnershipConfirmed"),true},{TEXT("PhaseSyncConfirmed"),true},{TEXT("DeploymentAdapterReady"),true},{TEXT("CombatAdapterReady"),true},{TEXT("PreparationAdapterReady"),true},{TEXT("RewardAdapterReady"),true}});
  T.Call(TEXT("InitializeFlow"));T.Check(T.Bool(TEXT("bAwaitingDeployment"))&&T.Int(TEXT("EffectiveDeploymentLimit"))==0,TEXT("Initial deployment must wait for application"));
  auto FirstId=T.Id(TEXT("DeploymentRequestId"));T.Call(TEXT("InitializeFlow"));T.Check(T.Id(TEXT("DeploymentRequestId"))==FirstId,TEXT("Repeated initialization must preserve pending request"));
  T.AckLimit(true,FGuid::NewGuid());T.Check(T.Bool(TEXT("bAwaitingDeployment")),TEXT("Stale deployment acknowledgement ignored"));
  T.AckLimit(false);T.Check(T.Bool(TEXT("bAwaitingDeployment"))&&T.Phase()==0,TEXT("Failed deployment cannot start preparation"));
  T.AckLimit();T.Check(T.Int(TEXT("EffectiveDeploymentLimit"))==2&&T.Bool(TEXT("bAwaitingPreparation")),TEXT("R1 limit applied before preparation"));
  T.AckPrep(false);T.Check(T.Phase()==0,TEXT("Failed preparation stays initializing"));T.AckPrep();
  FGuid OldReward;
  for(int32 Round=1;Round<=5;++Round){
   T.Check(T.Phase()==1&&T.Int(TEXT("CurrentRoundNumber"))==Round,TEXT("Prepared round and phase match"));
   T.Check(T.Int(TEXT("EffectiveDeploymentLimit"))==Round+1,TEXT("Deployment limit equals round + 1"));
   ++GFrameCounter;W->GetTimerManager().Tick(120.f);T.Check(T.Phase()==1,TEXT("Preparation never auto starts"));
   T.Interaction(0);T.Call(TEXT("RequestCombatStart"));T.Check(T.Phase()==1&&!T.Can(),TEXT("Zero units rejected"));
   T.Interaction(Round+2);T.Call(TEXT("RequestCombatStart"));T.Check(T.Phase()==1,TEXT("Over-limit deployment rejected"));
   T.Interaction(1,true,false);T.Call(TEXT("RequestCombatStart"));T.Check(T.Phase()==1,TEXT("Unit drag rejected"));
   T.Interaction(1,false,true);T.Call(TEXT("RequestCombatStart"));T.Check(T.Phase()==1,TEXT("Item drag rejected"));
   T.Interaction(1);T.Check(T.Can(),TEXT("One unit allowed without filling limit"));T.Call(TEXT("RequestCombatStart"));
   T.Check(T.Phase()==2,TEXT("Manual request enters Combat"));auto Encounter=T.Id(TEXT("EncounterId"));
   auto Handle=*T.Prop(TEXT("CombatTimerHandle"))->ContainerPtrToValuePtr<FTimerHandle>(Actor);
   T.Check(W->GetTimerManager().IsTimerActive(Handle),TEXT("Combat has an active timer"));T.Check(FMath::IsNearlyEqual(W->GetTimerManager().GetTimerRemaining(Handle),60.f,0.1f),TEXT("Combat timer is 60 seconds"));
   T.Call(TEXT("RequestCombatStart"));T.Check(T.Id(TEXT("EncounterId"))==Encounter,TEXT("Double start ignored"));
   T.Call(TEXT("NotifyCombatFinished"),{},{},{{TEXT("CompletedEncounterId"),FGuid::NewGuid()}});T.Check(T.Phase()==2,TEXT("Stale combat result ignored"));
   if(Round==1){++GFrameCounter;W->GetTimerManager().Tick(61.f);T.Check(T.Phase()==3&&T.Bool(TEXT("bTimedOut")),TEXT("Timeout waits for resolution without choosing winner"));}
   else {T.Call(TEXT("NotifyCombatFinished"),{},{},{{TEXT("CompletedEncounterId"),Encounter}});T.Check(T.Phase()==3&&!T.Bool(TEXT("bTimedOut")),TEXT("Normal finish enters resolution"));}
   T.Check(!W->GetTimerManager().IsTimerActive(Handle),TEXT("Combat timer cleared on finish"));
   T.Progress();T.Check(!T.Bool(TEXT("bAwaitingDeployment"))&&!T.Bool(TEXT("bAwaitingReward")),TEXT("Progress waits for resolution confirmation"));
   T.Resolve();T.Progress();
   if(Round==5){
    T.Check(T.Phase()==3&&T.Bool(TEXT("bFinalPolicyRequested"))&&!T.Bool(TEXT("bAwaitingDeployment")),TEXT("R5 waits for final policy and never requests R6"));
    T.Call(TEXT("ResolveFinalRoundPolicy"),{{TEXT("bGrantFinalReward"),FinalReward}},{},{{TEXT("CompletedEncounterId"),Encounter}});
    if(FinalReward){T.Check(T.Phase()==4,TEXT("Explicit final reward policy enters Reward"));auto Id=T.Id(TEXT("RewardRequestId"));T.Call(TEXT("NotifyRewardSelectionCompleted"),{},{},{{TEXT("CompletedRewardRequestId"),Id}});}
    T.Check(T.Phase()==5&&!T.Bool(TEXT("bAwaitingPreparation")),TEXT("Final round finishes without next preparation"));break;
   }
   T.Check(T.Phase()==3&&T.Int(TEXT("CurrentRoundNumber"))==Round,TEXT("Deployment acknowledgement required before reward"));
   auto Pending=T.Id(TEXT("DeploymentRequestId"));T.Progress();T.Check(T.Id(TEXT("DeploymentRequestId"))==Pending,TEXT("Repeated progression does not duplicate deployment request"));
   T.AckLimit(false);T.Check(T.Phase()==3,TEXT("Failed next limit blocks Reward"));T.AckLimit();
   T.Check(T.Phase()==4&&T.Int(TEXT("EffectiveDeploymentLimit"))==Round+2&&T.Int(TEXT("CurrentRoundNumber"))==Round,TEXT("Next limit applied before Reward; completed round retained"));
   ++GFrameCounter;W->GetTimerManager().Tick(120.f);T.Check(T.Phase()==4,TEXT("Reward never auto advances"));
   auto Reward=T.Id(TEXT("RewardRequestId"));T.Call(TEXT("NotifyRewardSelectionCompleted"),{},{},{{TEXT("CompletedRewardRequestId"),OldReward.IsValid()?OldReward:FGuid::NewGuid()}});T.Check(T.Bool(TEXT("bAwaitingReward")),TEXT("Stale reward completion ignored"));
   T.Call(TEXT("NotifyRewardSelectionCompleted"),{},{},{{TEXT("CompletedRewardRequestId"),Reward}});auto PrepId=T.Id(TEXT("PreparationRequestId"));
   T.Check(T.Phase()==4&&T.Bool(TEXT("bAwaitingPreparation")),TEXT("Reward completion waits for actual preparation"));
   T.Call(TEXT("NotifyRewardSelectionCompleted"),{},{},{{TEXT("CompletedRewardRequestId"),Reward}});T.Check(T.Id(TEXT("PreparationRequestId"))==PrepId,TEXT("Duplicate reward completion ignored"));
   T.AckPrep(false);T.Check(T.Phase()==4,TEXT("Failed next preparation stays Reward"));T.AckPrep();OldReward=Reward;
  }
  Failures+=T.Failures;Checks+=T.Checks;Actor->Destroy();
 }
 if(GM->GeneratedClass->FindFunctionByName(TEXT("EnsureRoundManager"))){
  auto GMActor=W->SpawnActor<AActor>(GM->GeneratedClass);FFlowTest G(GMActor);G.Call(TEXT("EnsureRoundManager"));
  auto Ref=FindFProperty<FObjectPropertyBase>(GM->GeneratedClass,TEXT("RoundManagerReference"));auto Manager=Cast<AActor>(Ref->GetObjectPropertyValue_InContainer(GMActor));G.Check(Manager!=nullptr,TEXT("GameMode spawns the manager"));
  G.Call(TEXT("EnsureRoundManager"));G.Check(Ref->GetObjectPropertyValue_InContainer(GMActor)==Manager,TEXT("Manager creation is idempotent"));
  G.Call(TEXT("RequestValidatedCombatStart"));FFlowTest M(Manager);G.Check(M.Phase()==0&&!M.Can(),TEXT("GameMode start remains blocked without confirmed adapters"));
  auto NativePhase=FindFProperty<FEnumProperty>(GM->GeneratedClass,TEXT("CurrentPhase"));G.Check(NativePhase&&NativePhase->GetUnderlyingProperty()->GetSignedIntPropertyValue(NativePhase->ContainerPtrToValuePtr<void>(GMActor))==0,TEXT("Blocked start does not start native combat"));
  Checks+=G.Checks;Failures+=G.Failures;Manager->Destroy();GMActor->Destroy();
 }
 auto Duration=FindFProperty<FFloatProperty>(GM->GeneratedClass,TEXT("PreparationDuration"));if(!Duration||Duration->GetPropertyValue_InContainer(GM->GeneratedClass->GetDefaultObject())!=0.f){++Failures;UE_LOG(LogTemp,Error,TEXT("GameMode preparation duration is not manual"));}
 W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 UE_LOG(LogTemp,Display,TEXT("FLOW_FULL_VERIFY: %d checks, %d failures. No assets saved."),Checks,Failures);return Failures?72:0;
}


