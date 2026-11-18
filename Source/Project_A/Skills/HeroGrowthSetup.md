# 견습 용사 성장 패시브: 처치 관여와 기록 보존

2026-11-18 코드 작업. 처치 관여 집계, 2성 합성 기록 상속, 준비 단계 진화와 무기 머티리얼 적용을 구현했다.
진화한 기물의 강화 평타 계수는 HeroStrike에서 설정한다.
DT/BP/레벨 에셋은 수정하지 않았다.

## 클래스와 편집 항목

- C++ 부모: `HeroGrowthPassiveSkill` (`UHeroGrowthPassiveSkill`).
- 직접 만들 BP 권장 이름: `BP_Skill_HeroGrowth`.
- DT의 Skill Classes에 기존 `BP_Skill_HeroStrike` 액티브와 함께 장착할 수 있다.
- 이 패시브만 장착한 유닛은 마나를 사용하지 않는다.

| 항목 | 위치/기본값 | 의미 |
|---|---|---|
| Required Participations | Class Defaults → Hero Growth, 기본 0 | 진화에 필요한 누적 횟수. 0이면 자동 진화를 사용하지 않는다. |
| Evolved Weapon Material | Hero Growth → Visuals, 기본 None | 진화 후 주무기에 적용할 머티리얼. |
| Weapon Material Slot | Hero Growth → Visuals, 기본 0 | 주무기 메시에서 바꿀 머티리얼 슬롯 인덱스. |
| Total Participations | 런타임 읽기 전용, 기본 0 | 기물별 누적 처치 관여 횟수. 일시적인 Counter와 별개다. |
| Evolved | 런타임 읽기 전용, 기본 false | 준비 단계에서 확정하며 합성 및 액터 재생성 시 상속한다. |
| Is Evolution Ready | Blueprint Pure 조회 함수 | 아직 진화하지 않았고 양수로 지정한 필요 횟수에 도달했는지 반환한다. |

공유 스킬 BP 기본 객체를 변경하지 않고 유닛마다 생성한 스킬 객체가 자신의 기록을 갖는다.
전투 중에는 횟수만 쌓인다. 준비 단계 진입 또는 같은 액터의 라운드 초기화에서 진화를 확정한다.
별도 라운드 BP는 기록을 복원한 뒤 준비 단계에서 `PrepareSkillsForRound`를 호출할 수 있다.
준비 호출을 거치지 않은 액터도 전투 시작 직전에 한 번 판정한다.
이미 진화한 기록을 복원하면 주무기 머티리얼도 다시 적용된다.

## 어떤 경우에 +1인가

1. 유닛이 살아 있는 적에게 실제 체력 피해 또는 보호막 피해를 준다.
2. 같은 전투에서 그 적이 실제 사망하면, 자신/아군/환경 중 누가 마무리했든 +1이다.
3. 같은 적을 여러 번 공격하거나 같은 피해 요청/사망 알림이 반복돼도 해당 생명의 사망당 한 번만 인정한다.

- 평타/액티브/추가 피해/지속 피해를 모두 인정한다. 피해 요청의 Attacker가 기여자여야 한다.
- 보호막만 깎아도 기여다. 실제 체력+보호막 피해가 0이면 기여로 등록하지 않는다.
- 아군/자기 자신에게 준 피해, 다른 월드의 공격자는 기여로 등록하지 않는다.
- 기여한 유닛이 먼저 사망해도 같은 전투가 계속되고 아군이 마무리하면 인정한다.
- 기여자가 StopCombat으로 전투를 떠났거나 다른 전투 세대로 넘어갔다면 과거 기여로 나중의 처치를 받지 않는다.
- 단순 Actor Destroy/가짜 OnUnitDied.Broadcast는 실제 처치로 집계하지 않는다.

## 공통 전투 코드와 스킬의 분담

- `BattleUnitBase::ReceiveDamage`: 실제 피해 확정 후, 마지막 일격까지 포함하여 기여자를 기록한다.
- 피해받은 유닛의 `DamageContributors`: 기여자 약한 참조 + 기여자의 전투 세대를 저장한다.
- `Die → NotifyKillParticipants`: 기록을 먼저 비우고 기여자 스킬에 native 알림을 준다. 그 뒤 기존 사망 이벤트를 보낸다.
- `UnitSkillBase::OnKillParticipation`: 기록 갱신 전용 C++ 확장 지점. 피해/전투 종료/외부 BP 실행을 여기서 추가하지 않는다.
- `HeroGrowthPassiveSkill`은 누적 횟수, 진화 확정, 무기 머티리얼을 담당한다.

일반 Kill 트리거와 처치 관여 알림으로 이중 집계하지 않도록 이 스킬은 ShouldActivate에서 일반 트리거 실행을 사용하지 않는다.
현재 진화 패시브를 장착하지 않은 기물은 기본 native 알림이 아무 동작도 하지 않는다.

## 무엇이 유지되고 초기화되는가

| 처리 | 결과 |
|---|---|
| StopCombat → ResetForRound → StartCombat | 같은 유닛/스킬 객체의 Total Participations 유지. 전투별 적/기여 기록, 일시 Counter/Cooldown은 초기화. |
| 유닛 사망 후 같은 Actor 복구 | 누적 횟수 유지. |
| 같은 적 Actor가 다음 라운드에 복구 | 이전 생명의 기여 기록은 제거. 새로 피해를 준 뒤 죽으면 새 관여 1회 가능. |
| InitializeUnit/RefreshUnitFromData로 명시적 재초기화 | 새 스킬 객체를 만들므로 누적 횟수 0. 종류 교체/새 기물로 취급하는 현재 계약을 따른다. |
| 1성 세 기물 → 2성 | 세 기물 중 최대 Total Participations 상속. Evolved는 하나라도 true면 유지. 기존 위치/ID/장비 선택 정책 유지. |
| Actor 파괴 후 재생성 | 아래 인터페이스 연결로 명시적 보관/복원 가능. 모든 Destroy 경로를 자동 추적하지는 않는다. |
| 게임 재시작/디스크 저장 | 제공하지 않는다. 런타임 보유 기록이며 SaveGame 구현은 별도다. |

`TestRunner.RestartTest`와 기존 `AutoBattleGameMode.ClearEncounter`는 유닛을 파괴/재생성한다.
이 경로를 동일 기물의 단순 라운드 초기화로 간주하지 않는다.
이 경로를 바꾸거나 BP 에셋을 수정하지 않았다. 실제 통합 계층이 아래 기록 이관을 연결해야 한다.

## 합성과 액터 기록 이관 계약

DT에 성장 횟수를 넣지 않는다. `FRosterUnit.SkillProgress`가 기물 GUID별 실행 기록을 보관한다.
`FUnitSkillProgress`의 SkillId/SkillClass는 처리할 스킬을 식별하고, Counters/Flags의 의미와 병합 규칙은 해당 스킬 클래스가 정의한다.
HeroGrowth는 `Participations`의 최대값과 `Evolved`의 OR를 사용한다. 쿨타임/일시 Counter/시전 대상은 이관하지 않는다.
기존 기록보다 작은 스냅샷을 복원해도 횟수나 진화 이력이 되돌아가지 않는다.

기존 `IUnitPlacement` 구현에 연결할 지점:

1. `GetBattleUnit(Id)`: 로스터 GUID에 해당하는 현재 `BattleUnitBase`를 반환한다. 없는 경우 null.
2. `ApplyUnitChanges`: 각 Add/Update/Move의 `Change.Unit.SkillProgress`를 대상 액터에 적용한다.
   먼저 전체 배치의 `CanRestoreSkillProgress`와 기존 위치/성급 조건을 확인한 뒤 성급 및 `RestoreSkillProgress`를 함께 반영한다.
   새 액터는 InitializeUnit을 먼저 호출한 뒤 복원한다. 복원은 전투 중 허용하지 않는다.
   실패하면 액터/위치/로스터/장비/골드 어느 것도 변경하지 않는 기존 배치 계약을 따른다.
3. 같은 GUID의 액터를 파괴/재생성할 때는 전투 정리 후, 파괴 전에 `Roster.CacheUnitSkillProgress(Id)` 성공을 확인한다.
   새 액터 초기화 → `Roster.GetUnit(Id)` → 반환된 SkillProgress 복원 순서로 연결한다.

`Roster.GetUnit`은 저장된 기록과 살아 있는 액터의 최신 기록을 병합해서 반환한다.
`GetUnits`는 마지막 커밋/캐시의 목록이므로 실시간 스킬 기록 조회는 GetUnit을 사용한다.
합성 계획은 모든 재료의 GetUnit 결과를 사용하므로 최고 기록의 기물이 소모돼도 기록을 상속한다.
검증 실패 시 구매나 준비 단계 전환도 거절하며, 기록만 먼저 적용하지 않는다.

현재 실제 BP 통합 구현은 수정하지 않았다. 인터페이스를 연결하지 않은 구현은 기존 저장 기록만 사용하므로
실제 전투에서 새로 쌓인 기록의 자동 수집/액터 반영까지 완료된 상태는 아니다.
테스트용 `UTestPlacement`에만 실제 전투 액터 조회/복원을 연결해서 C++ 계약을 검사한다.

## 검사와 다음 작업

자동 검사: `ProjectA.Unit.HeroGrowth.*`.
아군 마무리/자신의 마지막 일격/중복 요청, 보호막/0 피해/잘못된 팀,
라운드 보존/전투 세대 구분, 외부 사망 콜백의 즉시 정지, 사망한 기여자,
여러 기물의 독립 기록, 기존 액티브와 동시 사용, 준비 판정/새 초기화 계약을 검사한다.

기록: `Saved/Automation/HeroGrowth/index.json`, `Saved/Logs/HeroGrowth-Automation.log`.
이 검사는 렌더링 없는 코드 검사다. 에셋 연결 후 실제 Play 표시는 별도로 확인한다.

추가 검사: `ProjectA.Unit.SkillProgress.*`, `ProjectA.Shop.SkillProgress.*`.
새 액터 복원/잘못된 기록의 일괄 거절, 구매/지연 합성의 최댓값 상속,
소모되는 기물의 진화 기록 상속, 거절 시 원본/골드 보존, 이동/캐시/재생성을 검사한다.

에디터에서 진화 필요 횟수와 주무기 머티리얼을 지정한다.
빠른 Play 확인에는 임시로 필요 횟수를 1로 두고 처치 후 다음 준비 단계와 첫 강화 평타를 확인한다.

2026-11-18 합성/기록 이관 추가 후 확인: Development Editor/Win64 빌드 성공, 전체 자동 검사 40개 성공.
새 기록 이관/합성 검사 3개는 오류/경고 0이다. 전체 결과에는 기존 Board.MoveLifecycle 경고 1개가 포함된다.
최신 기록: `Saved/Automation/SkillProgress/index.json`, `Saved/Logs/SkillProgress-Automation.log`.
DT/공통 AnimBP 파일 해시는 작업 전과 같으며 실제 Play는 이번 단계에서 실행하지 않았다.
