# BP에서 유닛을 직접 생성하기 위한 접점

## 현재 상태

기존 BP 그래프와 에셋은 수정하지 않았습니다. 기존 Board Ready → Spawn Encounter Units → Start Preparation Phase는 그대로 동작합니다.

이번 변경은 BP가 For Each Loop, Spawn Actor from Class, Set Team Type으로 실제 생성 순서를 구성할 수 있도록 등록 API를 준비한 단계입니다. Spawn Encounter Units의 C++ 전체 생성 함수는 비교/복귀용으로 유지하고 있습니다. 두 경로 모두 같은 등록/검증/대기 위치 계산을 사용합니다.

## 새 노드

| 노드 | 역할 |
| --- | --- |
| Begin Encounter Setup | 보드가 준비됐고 아직 유닛이 없는지 검사한 뒤 BP 생성 작업 시작 |
| Register Battle Unit | BP로 만든 유닛을 GameMode의 전투/조회/정리 목록에 등록 |
| Move Unit To Waiting Area | 등록된 아군을 지정 인덱스의 대기 위치로 이동. 출전 상태는 끔 |
| Complete Encounter Setup | 양쪽 진영과 적 배치·타일 연결·출전 제한을 검사한 뒤 유닛 준비 완료로 표시 |
| Cancel Encounter Setup | 준비 상태에서 등록한 유닛과 점유 정보를 정리. 다시 생성하거나 Board Ready를 자동 호출하지 않음 |

Register/Move/Begin/Complete는 bool 성공 여부를 반환합니다. 실패 원인은 Status Message에 들어갑니다. Cancel은 전투 중에는 아무것도 제거하지 않습니다.

Complete Encounter Setup은 준비 시간을 시작하지 않습니다. 성공 후 기존 Start Preparation Phase를 호출해야 합니다.

## 첫 테스트: 적 한 명의 위치 확인

기존 Board Ready 생성 실행선을 임시로 끊은 다음, 부모 호출 없이 새 경로만 테스트합니다. 기존 노드는 지우지 않고 남겨도 되지만 실행선이 연결되면 안 됩니다.

```text
Event Board Ready
  → Begin Encounter Setup → Branch(True)
  → Spawn Actor from Class (Class = BattleUnitBase)
  → Set Team Type (Enemy, Target = Spawn Return Value)
  → Register Battle Unit (Unit = Spawn Return Value) → Branch(True)
  → Place On Tile (Target = Spawn Return Value)
       Tile = Board Manager → Get Tile(Row=4, Column=1)
```

- Spawn Actor는 실제 기본 BP 노드를 사용합니다.
- Spawn Transform은 Make Transform을 사용하고 Location은 Board Manager의 Get Actor Location으로 지정합니다. Place On Tile이 최종 위치와 높이를 맞춥니다.
- Collision Handling Override는 Always Spawn, Ignore Collisions로 지정합니다.
- Class를 BattleUnitBase로 고정하면 Return Value가 BattleUnitBase 타입이므로 Team Type, Place On Tile을 바로 연결할 수 있습니다.
- Register 성공 여부와 Place On Tile 반환값을 확인합니다.
- 적 한 명만으로는 위치 확인까지 가능합니다. 양쪽 진영이 있어야 Complete Encounter Setup과 전투 시작이 가능합니다.

## 최소 전투 테스트: 아군 한 명 + 적 한 명

위 적 생성/등록/배치 다음에 이어서:

```text
Spawn Actor from Class (Class = BattleUnitBase)
  → Set Team Type (Player)
  → Register Battle Unit → Branch(True)
  → Move Unit To Waiting Area(Unit = 생성한 아군, Waiting Index=0, Waiting Count=1)
  → Branch(True)
  → Complete Encounter Setup → Branch(True)
  → Start Preparation Phase
```

아군은 대기 위치에 지급되므로 Play에서 선택하고 아군 타일에 배치해야 합니다. 기존 Enter/시작 버튼, 준비 시간 종료, 결과, 재시작 그래프는 그대로 사용합니다.

모든 성공 반환값을 Branch로 확인합니다. 실패하면 Print String(Get Status Message)으로 원인을 먼저 표시하고 Cancel Encounter Setup을 호출하여 등록된 유닛을 정리합니다. 등록에 실패한 새 Spawn Actor 결과는 아직 GameMode 목록에 없으므로 그 액터는 Destroy Actor로 직접 정리합니다.

Cancel 후 다시 Begin Encounter Setup을 호출하면 재시도할 수 있습니다. 기존 생성 함수를 동시에 호출하지 않습니다.

## 전체 목록으로 확장할 때

BP 함수 SpawnRosterUnits를 만들어 아래 흐름을 구성합니다.

1. Begin Encounter Setup 성공 확인.
2. Unit Roster를 순회해 Start On Board가 꺼진 아군 수를 Waiting Count로 계산.
3. Waiting Index = 0, Spawn Succeeded = true로 초기화.
4. Unit Roster를 For Each Loop With Break로 순회.
5. Break Unit Spawn Info로 Unit Class / Team / Start On Board / Row / Column 추출.
6. Spawn Actor from Class로 실제 생성.
7. 생성한 유닛의 Team Type 설정.
8. Register Battle Unit 성공 확인.
9. Start On Board가 켜졌으면 Board Manager.Get Tile(Row, Column) → 유닛.Place On Tile.
10. 꺼졌으면 Move Unit To Waiting Area(Waiting Index, Waiting Count), 성공 후 Waiting Index 증가.
11. 각 실패 시 Spawn Succeeded = false, 오류 표시, Cancel Encounter Setup, 루프 Break.
12. Completed에서 Spawn Succeeded가 true일 때만 Complete Encounter Setup 호출.
13. Complete 성공 때만 Start Preparation Phase 호출.

For Each Loop With Break의 Completed는 Break로 중단한 경우에도 실행될 수 있으므로 성공 플래그를 반드시 확인합니다.

현재 C++ 기본 지급 목록의 Unit Class는 비어 있을 수 있습니다. C++ 생성 함수에는 기본 큐브 클래스 대체 동작이 있지만 BP의 Spawn Actor는 None 클래스를 생성하지 못합니다. 첫 전체 목록 테스트에서는 GameMode BP의 Unit Roster 각 항목의 Unit Class를 BattleUnitBase로 명시해서 시작하면 쉽습니다. 이후 실제 유닛 BP로 교체합니다.

Team은 Spawn Actor의 기본 핀이 아니라 유닛이 가진 변수입니다. 현재는 생성 후 Set Team Type을 연결합니다. 추후 유닛 BP의 BeginPlay에서 진영별 초기화를 해야 한다면 진영을 Expose on Spawn으로 노출하거나 Deferred Spawn 방식을 함께 조정해야 합니다.

## C++와 BP가 각각 맡는 것

- BP: 목록 순회, 생성할 클래스 선택, Spawn Actor, 진영 설정, 시작 위치/대기 여부 분기, 성공/실패 흐름.
- C++: 전투 목록 등록, 준비/전투 중복 방지, 점유/진영 배치 검증, 대기 위치의 공통 좌표 계산, 생성 완료 검사, 정리.

Register만 해서는 유닛 위치가 자동으로 옮겨지지 않습니다. Place On Tile 또는 Move Unit To Waiting Area를 별도로 호출해야 합니다. Complete만 해서는 준비 타이머가 시작되지 않습니다. Start Preparation Phase를 별도로 호출합니다.

## 검증 범위

BP 에셋과 이벤트 그래프 자체는 수정하지 않았습니다. C++ 자동 테스트는 Spawn Actor에 해당하는 직접 생성으로 아군/적군을 만든 뒤 위 등록/배치/완료 API를 호출하여 기존 자동전투에 참여하는지 검사합니다. 실제 BP 노드 연결과 화면은 다음 작업에서 확인합니다.

2026-10-25 검증: 원본 Development Editor / Win64 빌드 성공. 임시 프로젝트에서 동일 DLL의 ProjectA.Battle.SingleEncounter 테스트 Success. 별도 생성한 적 1명/아군 1명의 등록·배치·대기·완료·실제 자동전투 승리, 중복/잘못된 등록 거부, 취소의 전투 중 보호, 기존 C++ 생성 경로의 재시작과 기존 전체 전투 흐름을 확인했습니다. BP 에셋 자체의 그래프 실행은 아직 확인하지 않았습니다.
