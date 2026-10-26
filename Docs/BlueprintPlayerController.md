# PlayerController를 BP 입력 그래프로 전환

## 적용 상태

C++ 코드만 변경했다. BP 에셋과 GameMode의 클래스 설정은 아직 수정하지 않았다.
기존 C++ 키 바인딩, 클릭 대상 분기, BeginPlay의 카메라/입력 모드 설정을 제거했다.
빌드 후 아래 BP를 연결해야 기존 조작과 카메라가 동작한다. 부모 C++의 입력 자동 실행은 없다.
기존 GameMode와 테스트 파일의 미커밋 변경은 이번 작업에서 수정하지 않았다.

## 역할 분담

| BP 이벤트 그래프 | C++ 공통 함수 |
| --- | --- |
| 키 입력과 실행 순서 | 선택 가능한 아군인지 검증 |
| 클릭 대상이 유닛인지 타일인지 분기 | 배치 단계·진영·최대 인원 검증 |
| 카메라 선택과 입력 모드 설정 | 타일 점유와 유닛 위치 갱신 |
| 시작·재시작 요청 호출 | GameMode로 요청 전달 |

Get Board Click Hit은 기존 Canvas HUD 버튼 영역을 제외하고 Visibility 채널로 정밀 충돌 검사를 한다.
BP는 반환된 Hit Actor를 보고 동작을 결정한다. C++ PlayerTick의 호버 조회와 선택 원 표시는 유지한다.
자동 전투와 HUD 확장은 이번 범위에 포함하지 않는다.

## 1. 빌드와 BP 클래스 지정

1. BP와 레벨을 저장하고 에디터를 닫은 뒤 Project_AEditor / Win64 / Development를 빌드한다.
2. 에디터에서 AutoBattlePlayerController를 부모로 하는 BP_AutoBattlePlayerController를 만든다.
3. BP_AutoBattleGameMode의 Class Defaults에서 Player Controller Class를 이 BP로 지정한다.
4. MainMap의 World Settings에서 GameMode Override가 BP_AutoBattleGameMode인지 확인한다.

## 2. BeginPlay — 입력 모드와 카메라

BP_AutoBattlePlayerController에서 Event BeginPlay를 추가한다. 부모 BeginPlay 호출 노드가 있으면 유지한다.

```text
Event BeginPlay
 → Set Input Mode Game And UI
 → Set Show Mouse Cursor(true)
 → 카메라 선택
```

Set Input Mode Game And UI의 Player Controller는 Self, In Widget to Focus는 비워 둔다.
Mouse Lock Mode는 Do Not Lock, Hide Cursor During Capture는 false로 설정한다.
Set Show Mouse Cursor의 Target도 Self다.

기존 카메라 선택 규칙은 BattleCamera 태그가 있는 CameraActor 우선, 없으면 첫 CameraActor다.
다음 그래프로 같은 규칙을 구성한다.

1. BattleCamera 변수를 CameraActor Object Reference로 만든다.
2. Get All Actors Of Class(CameraActor)의 Out Actors를 Length > 0으로 검사한다.
3. 비어 있지 않으면 Get(0)을 Cast To CameraActor하여 BattleCamera에 저장한다.
4. 같은 배열을 For Each Loop With Break로 순회한다.
5. Array Element → Actor Has Tag의 Tag에는 상속 변수 Get Battle Camera Tag를 연결한다.
6. true면 해당 Actor를 Cast To CameraActor → Set BattleCamera → Break에 연결한다.
7. 루프의 Completed → Is Valid(BattleCamera) → Set View Target with Blend를 호출한다.
8. Target은 Self, New View Target은 BattleCamera, Blend Time은 0이다.

카메라가 없는 경우 View Target을 설정하지 않는다. 레벨 카메라의 Actor Tags에 BattleCamera를 추가하면 선택이 명확해진다.

## 3. 우클릭·Enter·R

그래프 빈 곳에서 각 키의 입력 이벤트를 추가한다. Released는 사용하지 않는다.

```text
Right Mouse Button / Pressed → Clear Selection
Enter / Pressed              → Request Start Combat
R / Pressed                  → Request Restart
```

모든 함수의 Target은 Self다. Request 함수는 기존 GameMode BP의 진행 이벤트로 요청을 전달한다.
HUD의 전투 시작 버튼 연결은 그대로 사용한다.

## 4. 좌클릭 — 유닛 선택과 타일 배치

```text
Left Mouse Button / Pressed
 → Get Board Click Hit (Target = Self)
 → Branch (Condition = Return Value)
    True → Cast To BattleUnitBase (Object = Hit Actor)
       Success → Select Unit (Unit = As BattleUnitBase)
       Cast Failed → Cast To BoardTile (Object = 같은 Hit Actor)
          Success → Is Valid? (Input Object = Get Selected Unit)
             Is Valid → Place Selected Unit On Tile (Tile = As BoardTile)
             Is Not Valid → Cast To BattleUnitBase (Object = BoardTile의 Get Occupied Unit)
                Success → Select Unit (Unit = As BattleUnitBase)
```

- Get Board Click Hit의 Hit 출력 → Break Hit Result → Hit Actor를 두 Cast의 Object로 사용한다.
- Select Unit과 Place Selected Unit On Tile의 Target은 Self다.
- 타일 위 유닛 선택은 Occupied Unit 조회로도 처리하여 타일이 먼저 맞았을 때도 선택 가능하게 한다.
- Branch의 False와 마지막 Cast Failed는 비워 둔다.
- 적이나 전투 중 유닛을 선택할 수 없는 조건은 Select Unit에서 검사한다.
- 적 타일·점유 타일·배치 제한 검사와 배치 성공 후 선택 해제는 C++가 처리한다.
- 빈 공간이나 일반 바닥을 클릭하면 아무 작업도 하지 않는다. 기존의 클릭 실패 안내 문구는 생략한다.
- 기존 Canvas HUD 클릭을 월드 클릭으로 전달하지 않는 처리는 Get Board Click Hit에 남아 있다.

## 5. 확인과 발표

Compile·Save 후 새 Play 세션에서 확인한다. GameMode의 컨트롤러 클래스 변경은 진행 중인 세션에 적용되지 않는다.

- 레벨의 전투 카메라와 마우스 커서가 정상적으로 표시된다.
- 좌클릭으로 아군 선택 → 빈 아군 타일 클릭으로 배치된다.
- 이미 배치한 아군을 다른 타일로 이동하면 이전 타일 점유가 해제된다.
- 적 선택, 적 타일 배치, 점유 타일 배치, 최대 배치 수 초과는 거부된다.
- 우클릭으로 선택이 해제된다.
- 아군 배치 후 Enter와 기존 Widget 버튼으로 전투를 시작할 수 있다.
- 전투 중 배치가 차단되고, R로 재시작할 때 초기화가 한 번 실행된다.
- 기존 Canvas HUD 버튼과 Widget 버튼을 누를 때 월드 유닛 선택/배치가 함께 실행되지 않는다.

그래프 주석은 시작 설정 / 유닛 선택과 배치 / 전투 요청 세 구역이면 충분하다.
발표에서는 BP가 입력과 게임 진행 순서를 구성하고 C++가 공통 검증과 전투 계산을 담당한다고 설명한다.
빌드 성공은 BP 그래프 배선이나 Play 동작 검증을 대신하지 않는다.
