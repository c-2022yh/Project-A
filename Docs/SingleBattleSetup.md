# 한 판 시연 설정

## 1. 적용

작업을 저장하고 언리얼 에디터를 닫습니다. Visual Studio에서 Development Editor / Win64로 빌드한 다음 프로젝트를 다시 엽니다. 새 HUD와 헤더 변경이 포함되어 있습니다.

## 2. 가장 빠른 테스트

1. 기존 MainMap을 엽니다.
2. 레벨에 BP_BoardManager가 하나만 있는지 확인합니다.
3. BoardManager의 Tile Class에 BP_BoardTile을 지정합니다.
4. Rows = 6, Columns = 5, Player Rows = 3으로 설정합니다.
5. 기존 자동 간격과 머티리얼 설정은 원하는 값으로 둡니다.
6. Window > World Settings를 열고 GameMode Override를 AutoBattleGameMode로 지정합니다. Blueprint 버전이 아닌 C++ 클래스도 선택할 수 있습니다.
7. 기존 Main Camera 액터의 Actor Tags에 BattleCamera를 추가합니다. Camera Tags나 Component Tags가 아니라 액터의 Tags입니다. 카메라가 하나뿐이라면 태그가 없어도 그 카메라를 사용합니다.
8. 레벨을 저장하고 Play합니다.

기본 설정은 아군 대기 유닛 3명, 적 배치 유닛 3명입니다. 캐릭터 BP가 없어도 임시 큐브 유닛을 사용합니다. 상단 상태창과 체력바, 하단 시작/재시작 버튼도 C++ HUD가 표시합니다.

## 3. 조작과 확인 순서

1. 가까운 쪽 보드 바깥의 WAITING 유닛을 왼쪽 클릭합니다. 선택되면 SELECTED 표시와 노란 원이 나옵니다.
2. 빈 아군 타일을 왼쪽 클릭해서 배치합니다.
3. 같은 유닛을 다시 선택하고 다른 빈 아군 칸에 옮겨봅니다. 이전 칸에는 다른 유닛을 놓을 수 있어야 합니다.
4. 적군 칸과 점유된 칸에는 놓이지 않아야 합니다.
5. 오른쪽 클릭은 선택 취소입니다.
6. 아군을 배치하고 START COMBAT 또는 Enter를 누릅니다. 기본 준비 시간 30초가 지나도 시작합니다.
7. 전투 중 배치는 잠깁니다. 유닛은 가까운 적에게 이동해서 근접 공격하고 체력바가 감소합니다.
8. 참가 아군 전멸은 DEFEAT, 적 전멸은 VICTORY입니다. 대기 공간의 미배치 유닛은 참가하지 않습니다.
9. RESTART 또는 R로 새 아군과 적을 생성하고 준비 단계로 돌아갑니다.

준비 시간이 끝났는데 아군을 한 명도 놓지 않았다면 안내를 띄우고 준비 시간을 다시 줍니다. 기본 전투 제한 시간은 120초이고, 시간 초과는 실패입니다. 두 팀이 모두 전멸한 상태도 실패로 판정합니다.

## 4. 시간과 유닛 구성 수정

C++ GameMode 기본값을 에디터에서 조절하려면 콘텐츠 브라우저에서 Blueprint Class > All Classes > AutoBattleGameMode를 부모로 BP_AutoBattleGameMode를 만듭니다. World Settings의 GameMode Override를 이 BP로 바꾸고 BP의 Class Defaults에서 수정합니다.

| 설정 | 기본값 / 의미 |
| --- | --- |
| Max Round | 1. 한 판만 시연합니다. |
| Max Board Units | 3. 아군 최대 출전 수입니다. |
| Preparation Duration | 30초. 0이면 자동 시작을 끄고 버튼/Enter로 시작합니다. |
| Combat Time Limit | 120초 |
| Waiting Area Offset | 자동 대기 위치의 추가 이동량입니다. 대기 유닛이 화면 밖이면 Y를 양수로 조금 늘려 보드 쪽으로 이동합니다. |
| Unit Roster | 유닛 클래스, 진영, 시작 위치 목록 |

기본 Unit Roster는 다음과 같습니다. 좌표는 0부터 시작합니다.

| 배열 인덱스 | Team | Start On Board | Row | Column |
| --- | --- | --- | --- | --- |
| 0 | Player | 끔 | 사용 안 함 | 사용 안 함 |
| 1 | Enemy | 켬 | 4 | 1 |
| 2 | Player | 끔 | 사용 안 함 | 사용 안 함 |
| 3 | Enemy | 켬 | 4 | 2 |
| 4 | Player | 끔 | 사용 안 함 | 사용 안 함 |
| 5 | Enemy | 켬 | 4 | 3 |

Unit Class가 비어 있으면 기본 C++ 테스트 유닛이 생성됩니다. Enemy는 Start On Board를 켜고 적군 영역의 빈 타일을 지정해야 합니다. Player는 끄면 대기 공간에 지급되고, 켜면 지정 아군 타일에 배치됩니다. 여러 항목이 같은 타일을 사용하거나 진영/좌표가 잘못되면 상단에 Setup 안내가 나오고 시작하지 않습니다.

행 수나 Player Rows를 바꾸면 적 좌표도 새 적군 영역에 맞게 수정합니다. 현재 카메라 기준 작은 Row는 가까운 쪽, 큰 Row는 먼 쪽입니다. BoardManager의 위치/회전 기준은 기존 월드 XY 배열을 유지합니다.

Max Round를 늘린 경우 클리어 후 NEXT ROUND/Enter로 같은 구성의 다음 라운드를 시작합니다. 라운드별 다른 적 구성과 보상은 아직 구현 범위에 포함되지 않습니다.

## 5. 실제 캐릭터로 교체

1. BattleUnitBase를 부모로 유닛 BP를 만듭니다.
2. Character의 Mesh에 Skeletal Mesh와 Animation Blueprint를 연결합니다.
3. Use Placeholder Mesh를 끕니다. 기본 Cube 컴포넌트는 숨겨집니다.
4. Capsule 크기와 Mesh 위치를 맞춥니다. 타일 배치 높이는 캡슐 절반 높이를 기준으로 자동 계산합니다.
5. Max HP, Attack Damage, Armor, Attack Speed, Attack Range, Movement Speed를 설정합니다.
6. BP_AutoBattleGameMode > Unit Roster의 Unit Class에 해당 BP를 지정합니다.

Attack Speed는 초당 공격 횟수이고, Attack Range는 두 캡슐 표면 사이의 거리입니다. 방어력 피해 공식은 피해량 × 100 / (100 + 방어력)입니다.

현재 기본 이동은 평평하고 장애물이 없는 전장에서의 직접 이동과 간단한 유닛 간 분리입니다. 길찾기와 장애물 우회는 없습니다. 바닥/타일은 Pawn을 막는 충돌이 있어야 하고, 클릭 대상은 Visibility 채널을 Block해야 합니다. 대기 중에는 이동/낙하를 멈추지만 전투 중에는 실제 CharacterMovement를 사용하므로 연속된 바닥이 필요합니다.

공격 애니메이션이나 타격 VFX는 OnAttack 이벤트에 연결할 수 있습니다. 현재 피해는 공격 간격마다 즉시 적용되며, 애니메이션 노티파이 시점과 동기화하는 기능은 별도 작업입니다.

## 6. 정식 UI 연결

임시 HUD는 기능 확인용 영어 표시입니다. 정식 UI는 별도로 구성합니다.

- 상태/타이머: CurrentPhase, RemainingPreparationTime, ElapsedCombatTime, StatusMessage.
- 상태 변경 알림: OnPhaseChanged.
- 배치 수: GetDeployedPlayerCount / MaxBoardUnits.
- 버튼: StartCombatPhase, RestartBattle, AdvanceRound.
- 체력바: 유닛 CurrentHP / MaxHP 및 OnHealthChanged.
- 공격/사망 연출: OnAttack / OnUnitDied.
- 보드 조회: GetTile, GetPlayerTiles, GetEnemyTiles, OnBoardGenerated.

별도 Widget에서 시작/재시작 버튼을 만들면 GameMode 함수를 호출하면 됩니다. 임시 화면 표시를 제거하려면 GameMode BP의 HUD Class를 원하는 HUD로 교체합니다. Canvas를 쓰지 않는 HUD 클래스를 선택하고 Widget을 별도로 띄워도 됩니다.

## 7. 자주 생기는 문제

- 아무것도 지급되지 않고 임시 HUD도 없음: 현재 맵의 GameMode Override와 해당 GameMode의 PlayerController/HUD Class 확인.
- Setup 메시지가 나옴: 보드 개수, Tile Class, 아군/적군 영역, Unit Roster 좌표 확인.
- 카메라가 다른 것을 봄: 원하는 CameraActor에 BattleCamera 액터 태그 지정.
- 대기 유닛이 보이지 않음: Waiting Area Offset으로 위치를 조절하고 카메라 아래쪽 여백 확보.
- 클릭이 안 됨: 게임 뷰포트를 클릭해 포커스를 주고, 유닛 Capsule과 타일 Mesh의 Visibility 응답을 Block으로 설정.
- 유닛이 바닥으로 빠짐: 바닥의 Pawn 충돌 및 Static Mesh의 Collision 확인.
- 캐릭터가 너무 작거나 큼: 기본 Capsule은 반지름 110, 반높이 200이며 임시 큐브는 약 220 × 220 × 400입니다. 실제 캐릭터에 맞춰 BP에서 조절.

기존 타일 BP와 맵의 카메라/조명/머티리얼 설정은 코드 변경 과정에서 저장하거나 수정하지 않았습니다.

## 8. 검증 결과

2026-10-24: 원본 프로젝트 Development Editor / Win64 빌드 Result: Succeeded (Target is up to date). C++/UHT 컴파일 및 git diff --check 통과. 검증용 임시 프로젝트에서 ProjectA.Battle.SingleEncounter 자동 테스트 Success 확인.

검증 항목: 보드/진영 생성 후 지급, 적 사전 배치, 빈 출전 시작 차단, 배치/재배치 점유 연결과 높이, 적군/점유 칸 배치 차단, 음수 피해 거부, 실제 이동과 공격으로 승리 도달, 결과에서 전투 중지, 재시작 초기화, 대기 아군을 제외한 패배 판정, 준비 타이머 시작.

이 테스트는 화면을 렌더링하지 않는 임시 게임 월드를 사용했습니다. MainMap의 카메라 구도, 마우스 클릭, HUD 버튼, 사용자 캐릭터와 충돌 에셋의 실제 화면은 에디터 Play에서 확인해야 합니다.
