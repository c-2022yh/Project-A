# Project_A 게임 진행 구현 결과

작업 대상: D:\Project-A-main\Project-A-main / Unreal Engine 5.7.4 / 2026-11-14

허용된 에셋 내부의 진행 로직과 GameMode 요청 경로를 구현했다. 실제 전투·배치·드래그·보상 시스템의 연동은 미완료이며, 미확인 상태에서는 시작을 차단한다. 테스트에서 사용한 응답은 테스트용 모의 응답이며 실제 시스템 연동을 의미하지 않는다.

## 생성 및 수정한 에셋

| 에셋 | 결과 |
|---|---|
| Content/BluePrints/GameFlow/E_GamePhase.uasset | Initializing, Preparation, Combat, ResolvingCombat, Reward, Finished |
| Content/BluePrints/GameFlow/ST_RoundDefinition.uasset | RoundNumber(int), MaxDeployedUnits(int), CombatDurationSeconds(double) |
| Content/BluePrints/GameFlow/DT_Rounds.uasset | R1~R5, 출전 한도 2→3→4→5→6, 각 전투 60초 |
| Content/BluePrints/GameFlow/BP_RoundManager.uasset | 상태 전환, 데이터 검증, 시작 조건, 타이머, 요청/응답 기반 진행 |
| Content/BluePrints/GameMode/BP_AutoBattleGameMode.uasset | 진행기 단일 생성·조회, 시작 요청 검증 경유, 준비 자동 시작 차단, 결과 인자 연결, 미정 재시작 차단 |

## 구현한 동작

- 공개 SetPhase 없이 private TryTransitionPhase에서 허용된 전환과 필요한 상태를 검사한다. 외부 블루프린트의 상태 직접 쓰기를 제한하고 GetFlowSnapshot으로 조회한다.
- ValidateRoundData는 정확한 행 개수, R1~R5 이름, 라운드 번호, 출전 한도, 60초를 검사한다. 실패 시 시작 차단과 OnConfigurationError를 제공한다.
- 배치 수 1 이상·한도 이하, Preparation, 라운드 데이터 유효, 배치/드래그 정보 수신 완료, 연동 확인 완료, 드래그/전환 중 아님을 모두 검사한다. 한도를 채울 필요는 없다.
- 0기 안내는 '기물을 배치하세요'. 다른 차단 조건에는 준비·한도·드래그·연동 상태 확인 문구를 제공한다.
- 준비와 보상에는 진행 타이머가 없다.
- 수동 시작 시 새 EncounterId를 만들고 60초 전투 타이머를 실행한다. 전투 종료·시간 초과·EndPlay에서 정리한다.
- 시간 초과는 승패를 결정하지 않는다. ResolvingCombat에서 전투 정지와 판정을 요청하고, 판정 완료 및 명시적인 진행 확인을 기다린다.
- R1~R4는 다음 출전 한도 적용 완료를 확인한 뒤 Reward에 들어간다. 보상 중 CurrentRoundNumber는 완료한 라운드이며 한도는 다음 라운드 기준이다.
- 실제 보상 선택·지급 완료 응답 후 다음 준비를 요청한다. 준비 완료 응답 후에만 라운드 번호를 올리고 Preparation으로 바꾼다.
- 요청 ID·EncounterId·현재 상태로 이전/중복 응답을 거른다. 적용/준비 실패 시 대기하며 자동으로 통과하지 않는다. 실패한 같은 요청은 재시도 가능하다.
- R5는 R6을 요청하지 않는다. 외부가 명시적으로 최종 보상 여부를 전달한 뒤 보상 완료 또는 즉시 Finished로 끝낸다. 승패 정책은 결정하지 않았다.

## 실제 프로젝트에서 달라지는 점

Board Ready 이벤트에서 EnsureRoundManager를 호출한다. 이미 유효한 인스턴스가 있으면 재사용하며 기존 유닛 준비 그래프는 유지한다. GetRoundManager로 참조를 얻는다.

기존 ReceiveCombatStartRequested는 RequestValidatedCombatStart를 통해 RoundManager.RequestCombatStart로 전달한다. 기존 StartCombatPhase 직행 연결은 끊었다. 외부 연동 확인과 초기화가 완료되지 않은 기본 상태에서는 버튼/Enter 요청이 들어와도 전투를 시작하지 않는다.

PreparationDuration 기본값은 0이며 Preparation Expired → RequestCombatStart 연결도 제거했다. 시간 경과에 따른 자동 시작을 막는다. 기존 Battle Result Detected 이벤트의 Result를 기존 enum switch의 Selection에 연결했다.

ReceiveRestartRequested → RestartBattle은 끊었다. 기존 RestartBattle이 등록 유닛을 삭제하고 새 진행기의 상태/요청을 초기화하지 않으므로, 재시작 정책이 정해지기 전에는 실행하지 않는다. 기존 C++ Tick과 함수 구현은 유지했다.

## 외부 시스템 연동 계약

먼저 GameMode.GetRoundManager로 진행기를 얻고 이벤트를 바인딩한다. 실제 기능을 구현하지 않은 채 준비 플래그만 true로 설정해서는 안 된다.

ConfigureIntegration에는 TimerOwnershipConfirmed, PhaseSyncConfirmed, DeploymentAdapterReady, CombatAdapterReady, PreparationAdapterReady, RewardAdapterReady가 모두 필요하다. 기본값은 모두 미확인이다. 실제 연동을 확인한 뒤 ConfigureIntegration → InitializeFlow를 호출한다.

| 진행기 이벤트 | 필요한 외부 작업과 응답 |
|---|---|
| OnDeploymentLimitChangeRequested(Limit, LimitRoundNumber, RequestId) | 실제 배치 제한 적용 후 NotifyDeploymentLimitApplied(RequestId, bSuccess) |
| OnRoundPreparationRequested(NextRoundNumber, RequestId) | 유닛 유지/회복/적 준비 및 기존 C++ 상태 동기화 완료 후 NotifyRoundPreparationCompleted(RequestId, bSuccess) |
| OnCombatStartRequested(EncounterId, RoundNumber) | 해당 전투만 시작. 기존 C++와 전투 타이머 소유권을 반드시 단일화 |
| OnCombatStopRequested(EncounterId, bTimedOut) | 해당 전투의 실제 AI/공격 정지 |
| OnCombatResolutionRequired(EncounterId, bTimedOut) | 결과 판정 후 NotifyCombatResolutionCompleted(CompletedEncounterId). 진행 정책에 따라 ConfirmProgressionToReward(CompletedEncounterId)를 별도 호출 |
| OnRewardRequested(CompletedRoundNumber, RewardRequestId) | 보상 선택 UI/지급 완료 후 NotifyRewardSelectionCompleted(CompletedRewardRequestId) |
| OnFinalRoundPolicyRequired(EncounterId) | 결정한 정책에 따라 ResolveFinalRoundPolicy(CompletedEncounterId, bGrantFinalReward) |
| OnStartAvailabilityChanged(bAllowed, Message) | WBP에서 버튼과 안내 반영 |
| OnPhaseChanged(Previous, Current) | 표시와 승인된 상태 동기화. 진행기를 다시 직접 전환하는 용도로 사용하지 않음 |
| OnConfigurationError(Message) | 구성 오류 표시/진단 |

배치/입력 담당은 UpdateInteractionState(AllyCount, UnitDragging, ItemDragging)를 최신 상태마다 전달해야 한다. 준비된 실제 전투가 정상 종료되면 NotifyCombatFinished(CompletedEncounterId)를 호출한다. CanStartCombat 및 GetStartAvailability로 시작 가능 여부를 조회한다. GetFlowSnapshot은 상태·라운드·한도·요청 ID를 제공한다.

## 남아 있는 통합 제약

기존 AAutoBattleGameMode는 native CurrentPhase/CurrentRound를 BlueprintReadOnly로 공개하고, Tick에서 별도 전투 시간과 시간 초과 패배를 판정한다. 다음 라운드/재시작은 유닛을 삭제할 수 있다. 따라서 현재 금지된 기존 C++ 또는 그 담당자의 정식 어댑터 없이 전체 전투를 안전하게 동기화할 수 있다고 보장할 수 없다.

실제 배치 한도 변경, 드래그 상태 전달, 보상 선택/지급, 유닛 유지·회복·적 갱신도 담당 시스템의 응답이 필요하다. 이 기능들을 성공한 것으로 꾸미거나 시간 경과로 대체하지 않았다.

패배 후 진행, 시간 초과 승패, 최종 승패, 최종 보상 여부, 유닛 유지·회복, 보상 중 조작, 재시작 정책은 자동 결정하지 않았다. 정책이 필요한 지점에서 기다린다.

현재 메인 맵에서 Play만 누르면 새 5라운드 전투가 완성되어 실행되는 상태가 아니다. 연동 전 시작이 차단되는 상태는 의도된 동작이다.

## 검증

Unreal Editor 5.7.4로 실제 생성·컴파일·저장·새 프로세스 재로드를 수행했다. .uasset 바이너리를 직접 편집하지 않았다.

독립 임시 월드에서 255개 검사를 통과했다. 5라운드 전체, 최종 보상 지급/생략 두 경로, 수동 시작, 0기·초과·드래그 차단, 60초 타이머와 콜백, 판정 확인 대기, 한도 선적용, 보상/준비 완료 대기, 실패·중복·이전 ID 무시, R6 방지, GameMode의 단일 진행기 생성과 연동 미완료 시작 차단을 검증했다. 준비와 보상에서 120초를 진행해도 자동 전환하지 않는 것을 확인했다.

별도 검증에서는 정상 라운드 데이터와 R5 누락 거부를 확인했다. 검증 중의 데이터/상태 변경은 메모리에만 적용하고 저장하지 않았다. 메인 맵 PIE, 실제 전투 AI/배치/위젯/보상 연동 테스트는 수행하지 않았다.

최종 로그: Saved/Logs/CodexFlowFullVerify.log, CodexFlowVerify.log, CodexFlowConnect.log, CodexFlowPolish.log.

## 보호 및 백업

WBP, PlayerController, 상점/보유 시스템, UnitBase/전투 AI, 메인 레벨/Level Blueprint, 기존 Source, 엔진 버전, 게임 빌드 규칙은 변경하지 않는다. 기존 Content는 작업 전 SHA256과 대조하며 변경 허용 대상은 BP_AutoBattleGameMode 한 개, 신규 생성 대상은 GameFlow 네 개다.

백업: Saved/CodexBackups/20261114-GameFlow/ 및 20261114-GameFlow-Full/. 원본 GameMode, 프로젝트 설정, 단계별 RoundManager와 변경 전 Content/Source 해시를 보관한다.

PythonScriptPlugin과 Editor 전용 Plugins/FlowAssetBuilder를 활성화했다. 플러그인은 제작과 테스트용이며 생성된 게임 실행 로직은 플러그인의 runtime 클래스/함수를 참조하지 않는다. 이 복사본의 .uproject를 옮길 때는 활성화된 플러그인 파일도 함께 유지해야 한다. 검증된 게임 에셋만 다른 프로젝트로 이관하는 경우에는 에디터의 Migrate와 종속성 검사를 사용한다.

플러그인 commandlet의 최초 생성/Extend/Complete/Finalize/Connect 단계는 단계별 생성 전용이며 중복 적용을 거부한다. Verify/FullVerify는 저장 없이 재검증한다. 소스와 테스트 코드는 Plugins/FlowAssetBuilder/Source/FlowAssetBuilder/Private에 보관한다.

Git 커밋·푸시는 하지 않았다.
