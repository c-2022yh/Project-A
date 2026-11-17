# 클래스 기반 유닛 스킬

2026-11-17: 공통 유닛의 이동·평타·피격·사망은 유지하고 스킬을 유닛별 UObject 인스턴스로 분리했다.

## 실제 구성

```text
BattleUnitBase
  └─ SkillInstances (유닛마다 새로 생성)
       ├─ UnitActiveSkill  : 최대 1개
       └─ UnitPassiveSkill : 여러 개
```

이 폴더의 `UnitSkill.h/.cpp`에 공통 → 액티브/패시브 → 개별 구현이 있다.

| DT_Units 행 | 장착된 BP (`/Game/BluePrints/Units/Skills/`) | C++ 부모 |
|---|---|---|
| Knight | BP_Skill_KnightShield | KnightShieldSkill |
| Mage | BP_Skill_MageFireball | MageFireballSkill |
| Archer | BP_Skill_ArcherRapidFire | ArcherRapidFireSkill |
| Rogue | BP_Skill_RogueWeakPoint | RogueWeakPointSkill |

기존 네 행의 스킬 수치·성급 보정·몽타주·투사체 외형은 각 BP의 Class Defaults → Settings로 복사했다.
DT의 체력·공격력·평타·외형 등은 유지하며 스킬은 `Skill Classes` 배열에 클래스만 연결한다.
실제 행의 기존 `Skills` 효과 배열은 비웠다.

## 책임과 실행 흐름

- 유닛: 마나 충족 확인/소모, 시전 상태, ActionId와 Notify 중복 방지, 피해/보호막/버프 공통 처리.
- 스킬: 대상 선택(`SelectTarget`), 사용 조건(`CanUse`), 실행(`Execute`), 명중 효과(`ApplyImpact`).
- 스킬 BP 기본값: Settings의 ID·타깃·시전 시간·몽타주·투사체·Effects 수치.
- 스킬 인스턴스: OwnerUnit·Counter·Cooldown. 공유 BP 기본 객체는 수정하지 않는다.

Settings는 기존 `FUnitSkillDefinition` 형식을 재사용한다. 각 스킬 BP가 자신의 설정을 소유하며
DT에서 수치를 편집하지 않는다. 고유 동작은 C++ 가상 함수 또는 BP override로 확장할 수 있다.
기존 네 C++ 스킬은 보호막/마법 피해/공속 버프/다음 평타 강화를 각각 구현한다.

액티브는 평타/이동이 끝난 Idle 상태에서 시작한다. 대상이 없으면 시작하지 않고 마나를 유지한다.
시작할 때 마나를 소모하고 `Cast Seconds` 후 효과를 발동한다.
`Use Anim Notify=true`인 경우 `CommitSkillEffect(ActionId)`로 효과를 발동해야 한다.
현재 네 스킬의 타이머/몽타주 설정은 기존 값을 유지했다.
`OnSkillStarted`, 평타 Notify, 공통 ABP/투사체 BP의 기존 연결은 그대로 사용한다.

## 기절·사망·투사체·라운드

- 기절 중에는 새 시전을 시작하지 않는다. 일반 스킬의 진행 중 시전은 기절로 취소하지 않는다.
- 채널링 등은 Settings → Interruptible By Stun을 켠다. Refund Mana When Interrupted는 발동 전 취소에만 적용된다.
- 시전 중 대상이 사라지면 다른 대상으로 옮기지 않는다. 기본적으로 마나는 환불하지 않는다.
- 사망 시 진행 중 시전은 취소한다. 스킬 객체의 타이머/latent 작업을 정리하고 `OnOwnerDied`를 알린다.
- 이미 발사된 탄은 시전자 사망 후에도 공통 유닛 Tick에서 비행한다. Actor를 즉시 파괴하지 않는 기존 사망 구조를 사용한다.
- 공격 측 피해량·계수·외형은 발사 순간 복사한다. 방어력·피해 감소·보호막은 명중 시 대상의 현재 값으로 계산한다.
- 대상 사망/라운드 변경/수명 만료 시 탄을 제거한다. StopCombat 시 남은 탄을 모두 제거한다.
- StopCombat/StartCombat에서 Counter·Cooldown·스킬의 타이머/latent 작업을 정리하고 `OnRoundReset`을 호출한다.
  이 이벤트는 반복 호출에도 안전하게 작성한다. 영구 성장 수치는 유닛이 따로 보관하여 유지한다.
- 시전 생명주기 BP 이벤트: OnCastStarted / OnCastCancelled / OnCastFinished.

## 패시브 추가

`HitGrowthPassiveSkill`은 3회 평타 명중마다 공격력이 성장하는 재사용 예시다. 현재 DT에는 자동 장착하지 않았다.

1. UnitPassiveSkill 또는 HitGrowthPassiveSkill을 부모로 BP를 만든다.
2. Settings → Skill Id를 고유하게 정하고 Trigger를 설정한다.
3. DT의 해당 유닛 → Skill Classes 배열에 추가한다. 액티브는 기존 항목 그대로 둔다.

전달 이벤트: CombatStart / AttackStarted / BasicAttackHit / DamageDealt / DamageTaken / Kill.
`ShouldActivate`는 이벤트 종류·쿨타임·카운트·순환 여부를 확인한다. BP에서 재정의할 때 기본 필터가 필요하면 부모 구현을 호출한다.
Context에 공격자/대상/피해 출처/스킬 ID/연쇄 정보, Result에 실제 체력 피해·보호막 피해·처치 결과가 전달된다.
기본적으로 Triggered 출처의 피해는 패시브를 다시 발동시키지 않는다.
명시적 연계는 Allow Triggered Events로 허용하되 동일 스킬 재진입과 최대 깊이 제한은 유지한다.
사망 자체의 정리/고유 반응은 공통 생명주기 이벤트 OnOwnerDied에서 확장할 수 있다.

## 이전 호환과 도구

이전 테스트/다른 DT를 깨뜨리지 않도록 `Skills` 직렬화 필드와 Legacy 어댑터를 남겼다.
SkillClasses가 하나라도 있으면 기존 배열은 실행하지 않는다. 런타임 Definition.Skills는
시전/투사체용 설정 사본이며 DT를 변경하는 경로가 아니다.
새 작업은 SkillClasses를 사용한다. 모두 이전된 뒤 Legacy 어댑터 제거를 별도로 검토할 수 있다.

`Scripts/Editor/MigrateUnitSkills.py`는 네 행의 기존 설정을 스킬 BP로 복사하고 스킬 필드만 바꾸는 에디터 도구다.
에셋은 이미 이전했으므로 다시 실행할 필요는 없다. 재실행 시 이전된 BP 수치를 초기화하지 않는다.
전용 C++ MigrationLibrary는 WITH_EDITOR에서만 변경을 허용한다.
엔진 DT JSON 내보내기에서 발생한 assertion을 피하기 위해 행에 직접 접근한다.

## 검증과 다음 확인

- Development Editor / Win64 빌드 및 `ProjectA.` 자동 검사.
- 활성/패시브 동시 장착, 인스턴스/카운트 분리, 잘못된 클래스 거부, 패시브 재귀 필터.
- 네 실제 BP의 로드/초기화, 1성·2성 스킬 결과의 이전 실행 경로와 비교.
- 시전자 사망 후 화염구 명중, 발사 스냅샷, 기존 이동/평타/사망/전투 러너 회귀 검사.
- 기록: `Saved/SkillMigration/MigrationReport.json`, `Saved/Automation/UnitSkillMigration/index.json`,
  `Saved/Logs/UnitSkillMigration-Automation.log`.

NullRHI 자동 검사는 실제 화면의 애니메이션·화염구·보호막 표시를 보증하지 않는다.
다음 에디터 확인은 기존 TestMap에서 기사 보호막부터 시작한다. 레벨/유닛 BP 그래프는 이번 이전에서 수정하지 않았다.

2026-11-17 결과: Development Editor 빌드 성공, 자동 검사 25개 모두 성공.
신규 스킬 검사 5개는 오류/경고 0. 기존 Board.MoveLifecycle 검사의 예상 로그 경고 1개가 포함된다.
에디터 시작 시 기존 Condition failed 진단 4건과 EditorPerf 메뉴 경고는 테스트 결과와 별도다.
