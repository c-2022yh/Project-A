# Widget BP로 준비 화면 만들기

## 현재 적용 범위

이 문서는 수동 에디터 작업 안내다. WBP 에셋과 GameMode 그래프는 자동으로 수정하지 않았다.
기존 적/아군 한 명 생성 경로는 유지한다. 로스터 생성 함수로 바꿀 필요가 없다.
현재 C++가 준비 시간, 배치 인원수, 시작/재시작 요청을 BP에 공개하고 있으므로 추가 C++ 수정이나 빌드가 필요하지 않다.

## 1. 위젯 구성

콘텐츠 브라우저에서 User Interface > Widget Blueprint를 만들고 부모는 User Widget, 이름은 WBP_BattleHUD로 지정한다.

Designer에 Canvas Panel을 루트로 추가하고 다음 요소를 배치한다.

- Text: 이름 TXT_PreparationTime, Is Variable 켜기.
- Button: 이름 BTN_StartCombat, 안에 Text를 넣어 '전투 시작' 표시.
- 시간은 오른쪽 위, 시작 버튼은 그 아래에 임시 배치한다. 기존 Canvas HUD와 겹치지 않게 한다.
- Canvas Panel의 Visibility는 Not Hit-Testable (Self Only)로 설정한다. 루트만 클릭을 통과시키고 버튼은 클릭 가능하게 한다.
- 장식용 텍스트의 Visibility는 Not Hit-Testable (Self & All Children)로 설정해도 된다.
- 위젯 전체를 Not Hit-Testable (Self & All Children)로 설정하면 버튼도 클릭할 수 없으므로 피한다.
- 버튼의 Is Focusable은 꺼서 Enter 입력이 버튼 포커스에 잡히는 것을 줄인다.

## 2. GameMode 참조 저장

WBP_BattleHUD의 Graph에 BattleGameMode 변수를 만든다. 타입은 AutoBattleGameMode Object Reference다.
BP_AutoBattleGameMode 타입 대신 C++ 부모 타입을 사용하면 위젯과 GameMode BP의 상호 참조를 줄일 수 있다.

Event Construct -> Get Game Mode -> Cast To AutoBattleGameMode -> Set BattleGameMode.
Cast의 Object에 Get Game Mode 반환값을 연결한다. 성공한 Cast 결과를 변수 값으로 저장한다.

## 3. 준비 시간 표시

TXT_PreparationTime의 Text 속성에서 Bind > Create Binding을 선택한다.
바인딩 함수는 Text를 반환한다. 저장한 BattleGameMode가 유효하지 않으면 빈 Text를 반환한다.
유효하면 Remaining Preparation Time을 읽고 Ceil to Int로 올림한 뒤 Format Text에 연결한다.

```text
Is Valid(BattleGameMode)
  True -> Remaining Preparation Time -> Ceil to Int
       -> Format Text('준비 {Seconds}초') -> Return Value
  False -> 빈 Text 반환
```

Ceil to Int는 0.1초가 남았는데 0초로 보이는 것을 피하기 위한 표시 처리다.
시간 감소나 전투 시작을 바인딩 함수에서 실행하지 않는다. 기존 C++ 타이머가 계산한다.

## 4. 전투 시작 버튼

Designer에서 BTN_StartCombat을 선택하고 OnClicked 이벤트를 만든다.

```text
OnClicked(BTN_StartCombat)
  -> Is Valid(BattleGameMode)
  -> Request Combat Start(Target = BattleGameMode)
```

Request Combat Start가 기존 GameMode BP의 Combat Start Requested 이벤트로 이어진다.
Start Combat Phase를 직접 호출하면 BP의 요청 흐름을 건너뛰므로 요청 함수를 사용한다.

버튼 Is Enabled에는 Bool 바인딩을 만들 수 있다. 유효한 GameMode를 먼저 검사하고 아래 조건을 모두 만족할 때만 true를 반환한다.

- Current Phase == Preparation
- Encounter Ready == true
- Preparation Active == true
- Get Deployed Player Count > 0

처음에는 시간 표시와 클릭 연결부터 확인하고 이 바인딩을 추가해도 된다.

## 5. GameMode BP에서 한 번 생성

BP_AutoBattleGameMode에 BattleHUD 변수를 만든다. 타입은 WBP_BattleHUD Object Reference다.
Event Game Phase Changed가 이미 있다면 같은 이벤트를 새로 만들지 말고 기존 처리에 합친다.

```text
Event Game Phase Changed(Phase)
  -> Switch on EGamePhase(Selection = Phase)
     Preparation -> Is Valid(BattleHUD)
                    Is Not Valid -> Create Widget(Class = WBP_BattleHUD,
                                                Owning Player = Get Player Controller 0)
                                 -> Set BattleHUD(Return Value)
                                 -> Add to Viewport(Target = BattleHUD)
                    Is Valid -> 추가 생성하지 않음
```

Start Preparation Phase가 상태 변경 이벤트를 보내므로 기존 유닛 생성 그래프는 수정하지 않아도 된다.
재시작도 같은 이벤트가 발생한다. 유효성 검사를 통해 위젯이 계속 쌓이는 것을 막는다.
이 이벤트에서 Start Preparation Phase를 다시 호출하면 재귀 호출될 수 있으므로 호출하지 않는다.

## 6. 확인과 다음 교체

Compile/Save 후 Play한다.

1. 새 준비 시간과 기존 준비 시간이 함께 감소하는지 확인한다.
2. 위젯이 떠 있는 상태에서 유닛 선택과 타일 배치가 되는지 확인한다.
3. 아군을 배치한 뒤 새 시작 버튼을 눌러 전투가 시작되는지 확인한다.
4. 재시작 후 위젯이 중복되지 않고 시간이 다시 표시되는지 확인한다.

현재 임시 AutoBattleHUD는 체력바와 선택 표시도 그린다. 지금 HUD Class를 변경하면 그 표시까지 사라진다.
새 UI 검증 후 기존 상단 표시/버튼만 끄거나 체력바를 별도 Widget으로 옮기는 작업을 결정한다.
이번 단계에서는 기존 HUD Class를 유지한다.

전투 단계별 문구, 배치 인원수, 승패 화면과 재시작 버튼은 위 최소 UI를 확인한 뒤 확장한다.
