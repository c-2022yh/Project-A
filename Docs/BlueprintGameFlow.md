# GameMode 이벤트 그래프 연결

## 이번 변경의 범위

C++는 보드 준비 확인, 유닛 생성/정리, 타이머 계산, 전투 시작 검증, 생존자 집계를 담당합니다. BP_AutoBattleGameMode의 이벤트 그래프는 이 함수들을 언제 호출할지 결정합니다.

현재 BP 에셋의 그래프는 자동 수정하지 않았습니다. 아래 순서대로 BP에서 연결합니다. 이벤트를 아직 재정의하지 않았다면 기존 C++ 기본 동작을 사용하므로 빈 BP에서도 기존 게임은 실행됩니다. 이벤트를 재정의한 순간부터 해당 단계는 BP가 담당합니다.

## 적용

1. BP와 레벨을 저장하고 에디터를 닫은 상태에서 Development Editor / Win64 빌드.
2. 프로젝트를 열고 BP_AutoBattleGameMode를 엽니다.
3. World Settings의 GameMode Override가 BP_AutoBattleGameMode인지 확인합니다.
4. Event Graph의 빈 공간에서 이벤트 이름을 검색합니다. 검색이 안 되면 My Blueprint의 Overrides 목록도 확인합니다. 내부 이름은 ReceiveBoardReady처럼 Receive로 시작합니다.

Event BeginPlay는 새로 만들 필요가 없습니다. 기존 BeginPlay를 직접 재정의했다면 부모 BeginPlay 호출을 유지해야 C++의 보드 준비 확인이 실행됩니다.

## 1. Board Ready — 먼저 이것 하나만 연결

다음 실행 핀을 흰 선으로 연결합니다.

```text
Event Board Ready
    → Spawn Encounter Units
    → Branch
        True → Start Preparation Phase
        False → Print String (Status Message)
```

- Spawn Encounter Units의 Return Value(초록색 bool 핀)를 Branch의 Condition에 연결합니다.
- False의 Print String에는 Get Status Message를 연결합니다.
- 함수의 Target은 Self입니다. Get Game Mode와 Cast를 추가할 필요가 없습니다.
- 부모 이벤트 호출과 수동 생성 함수를 동시에 실행하지 않습니다. 자동으로 Parent 노드가 만들어졌다면 위 그래프에서는 제거합니다.

Compile/Save 후 Play합니다. 아군 3명, 적 3명, 준비 시간이 보이면 이 그래프가 보드 준비 이후의 지급/준비를 담당하고 있습니다. 이 단계만 먼저 테스트해도 나머지 이벤트는 C++ 기본 동작을 유지합니다.

Spawn Encounter Units는 생성만 합니다. 준비 단계와 배치 허용은 Start Preparation Phase에서 시작합니다. Start Preparation Phase는 유닛을 재생성하지 않습니다.

## 2. Combat Start Requested — 버튼과 Enter의 요청 처리

```text
Event Combat Start Requested
    → Switch on EGamePhase (Selection = Get Current Phase)
        Preparation → Start Combat Phase
        RoundClear  → Advance Round
```

나머지 출력 핀은 연결하지 않습니다. 한 판 설정(Max Round = 1)에서는 Preparation 경로만 사용합니다.

- Request Combat Start는 요청 입구입니다.
- Start Combat Phase는 실제 전투 시작 검증/실행 함수입니다.
- 이 이벤트 안에서 다시 Request Combat Start를 부르면 같은 이벤트로 돌아오므로 사용하지 않습니다.
- 출전 아군이 없으면 C++가 시작을 거부하고 준비 시간을 다시 줍니다.

## 3. Preparation Expired — 준비 시간 종료의 연결

```text
Event Preparation Expired
    → Request Combat Start
```

타이머는 C++에서 계산합니다. BP에 Event Tick이나 별도 Delay/Timer를 추가하지 않습니다. Preparation Duration = 0이면 시간 종료 이벤트는 없고 버튼/Enter만 사용합니다.

종료 이벤트는 한 번만 전달됩니다. 의도적으로 지연/연출을 넣어도 매 프레임 중복 발생하지 않습니다. 다시 준비 시간을 주고 싶으면 Restart Preparation Countdown을 호출합니다. 아직 아군이 없어 시작할 수 없는 경우에는 Start Combat Phase가 자동으로 이를 처리합니다.

## 4. Battle Result Detected — 결과에 따라 마무리

```text
Event Battle Result Detected (Result)
    → Switch on EGamePhase (Selection = Result)
        Victory    → Handle Victory
        GameOver   → Handle Game Over
        RoundClear → Handle Round Clear
```

Preparation/Combat 출력은 사용하지 않습니다. Switch의 Selection에는 Current Phase가 아니라 이벤트의 Result 핀을 연결해야 합니다.

C++는 결과를 한 번만 판정해서 전달하고 유닛을 먼저 정지시킵니다. BP가 위 Handle 함수를 호출하면 최종 상태가 반영됩니다. Delay로 승리 연출을 기다려도 정지된 유닛이 계속 싸우지 않습니다. 출력 핀을 연결하지 않으면 결과 확정을 기다리며 멈추는 것이 정상입니다.

## 5. Restart Requested — 재시작 연결

```text
Event Restart Requested
    → Restart Battle
```

- Request Battle Restart는 버튼/키보드가 호출하는 요청 함수입니다.
- Restart Battle은 실제 초기화 함수입니다.
- 이 이벤트 안에서 Request Battle Restart를 다시 호출하지 않습니다.
- Restart Battle이 끝나면 Board Ready 이벤트가 다시 발생하므로 1번 그래프가 지급과 준비를 다시 진행합니다.

새로고침/다음 라운드에서도 기존 보드 위치와 머티리얼은 유지하고, 유닛만 새로 생성합니다.

## 6. Game Phase Changed — Widget 연결 지점

게임 흐름의 다섯 이벤트가 동작하는 것을 확인한 후 연결합니다.

```text
Event Game Phase Changed (Phase)
    → Switch on EGamePhase
        Preparation → 준비 화면 표시
        Combat      → 준비 화면 숨김 / 전투 화면 표시
        Victory     → 승리 화면 표시
        GameOver    → 실패 화면 표시
        RoundClear  → 라운드 클리어 화면 표시
```

이 이벤트는 화면 표시용입니다. 여기서 다시 같은 단계의 Start/Handle 함수를 호출하지 않습니다. C++가 이미 상태를 반영한 뒤 이벤트를 전달합니다.

Widget을 만들 때의 기본 연결:

1. WBP_BattleUI 생성.
2. GameMode BP에 BattleUI 변수(UserWidget 또는 WBP_BattleUI Object Reference)를 만듭니다.
3. Preparation 경로에서 Is Valid(BattleUI)를 검사합니다.
4. False이면 Create Widget(WBP_BattleUI, Owning Player = Get Player Controller 0) → BattleUI에 저장 → Add to Viewport.
5. True이면 기존 Widget을 재사용합니다. 재시작 때 계속 Create Widget을 호출해서 화면이 쌓이지 않도록 합니다.
6. Widget의 시작 버튼 OnClicked → Get Game Mode → Cast to AutoBattleGameMode → Request Combat Start.
7. 재시작 버튼 OnClicked → 같은 Cast → Request Battle Restart.
8. 타이머/상태 텍스트는 Remaining Preparation Time, Current Phase, Status Message를 읽습니다. Widget의 표시 갱신을 위한 Tick/바인딩은 가능하지만, 그 안에서 단계 전환이나 다른 타이머를 돌리지 않습니다.

정식 Widget UI를 완성한 후 GameMode BP의 HUD Class를 기본 HUD로 변경하면 기존 AutoBattleHUD 표시를 끌 수 있습니다. 체력바도 임시 HUD가 표시하므로 정식 체력바 연결 전에는 임시 HUD를 유지해도 됩니다. 이번 작업에서는 Widget 에셋을 생성하지 않았습니다.

## 이벤트 추가 방식과 중복 실행 방지

다섯 흐름 이벤트는 BlueprintNativeEvent입니다. BP에서 재정의하지 않은 이벤트만 C++ 기본 구현을 사용합니다. 따라서 기본 동작과 BP 그래프가 자동으로 둘 다 실행되지는 않습니다.

다만 BP에서 Add Call to Parent Function을 추가하고 같은 일을 수동으로 또 연결하면 중복될 수 있습니다. 위 예시 그래프는 부모 호출 없이 작성합니다.

Start Preparation Phase는 이제 준비 단계 시작만 담당합니다. 이전처럼 전체 초기화 용도로 사용하지 않습니다. 전체 재시작은 Request Battle Restart를 사용합니다.

## 확인할 순서

1. Board Ready 그래프만 만들고 지급/준비가 되는지 확인.
2. Combat Start Requested를 연결하고 Enter로 전투 시작 확인.
3. Preparation Expired를 연결하고 시간 종료로 시작 확인.
4. Battle Result Detected를 연결하고 승패 표시 확인.
5. Restart Requested를 연결하고 재지급/재배치 확인.
6. 마지막으로 Game Phase Changed에 Widget 표시 연결.

보드 위치, 자동 간격, 진영/점유 판정, 유닛 전투 계산, 타일 클릭 판정은 이번 전환에서 유지됩니다.

## 검증 기록

2026-10-25: Development Editor / Win64 원본 빌드 성공. git diff --check 통과. 같은 DLL을 사용한 임시 프로젝트에서 ProjectA.Battle.SingleEncounter 자동 테스트 Success 확인. 기존 지급/배치/전투/승패/재시작/타이머와 요청 함수의 기본 구현, BP 재정의용 이벤트 노출, 준비 단계 시작 시 유닛을 재생성하지 않는 동작을 검증했습니다.

원본 프로젝트의 무화면 테스트 실행은 엔진 창 처리(GetRestoredDimensions) 오류로 테스트 시작 전에 중단되어, 에디터 설정이 없는 임시 프로젝트로 검증했습니다. BP_AutoBattleGameMode의 사용자 그래프 및 Widget은 아직 작성하지 않았고, 실제 그래프 연결과 화면은 에디터에서 확인해야 합니다.
