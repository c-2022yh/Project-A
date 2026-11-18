# 견습 용사 액티브: 강화 평타와 흡혈

2026-11-18 구현. 이번 작업은 코드와 자동 검사만 제공한다. 스킬 BP/DT/레벨 에셋은 직접 연결한다.
성장 패시브가 진화한 기물은 진화용 계수와 적중 연출을 사용한다.

## 클래스와 설정 위치

- C++ 부모: `HeroStrikeSkill` (`UHeroStrikeSkill`).
- 권장 BP 이름: `BP_Skill_HeroStrike`.
- 스킬 BP의 **Class Defaults → Hero Strike**에서 아래 값을 편집한다.
- 범용 Settings는 이 스킬 BP의 Details에서 숨겼다. 내부 공통 시전 설정은 초기화 때 전용 항목에서 만든다.

| 항목 | 기본값 | 의미 |
|---|---|---|
| Attack Damage Ratio | 2.0 | 평타를 포함한 총 AD 200%. 기본 평타에 AD 200%를 추가하지 않는다. |
| Evolved Attack Damage Ratio | 3.0 | 진화 후 강화 평타의 총 AD 300%. |
| Evolved Impact Sound | None | 진화한 강화 평타가 적중하면 대상 위치에서 재생할 소리. |
| Health Damage Heal Ratio | 0.2 | 해당 강화 평타가 적 체력에 실제로 준 피해의 20% 회복. |
| Cast Seconds | 0.2초 | 마나 소모 후 다음 평타 강화를 부여하는 시전 시간. |
| Cast Montage | None | 별도 시전 모션 없이 기본 평타 모션으로 타격한다. 필요할 때만 지정한다. |
| Use Anim Notify | false | 기본은 시전 시간 뒤 자동 부여. true로 바꾸면 시전 모션에서 CommitSkillEffect(ActionId) 호출이 필요하다. |

기본 평타는 기존 평타 몽타주와 `CommitAttackHit(ActionId)` 연결을 사용한다.
시전 Notify와 평타 타격 Notify는 다른 호출이다.
수치만 바꿀 때는 C++ 재빌드 없이 스킬 BP를 저장한 뒤 Play를 다시 시작한다.
이미 생성된 유닛은 자신의 스킬 객체를 사용하므로 실행 중 BP 기본값 변경을 자동 반영하지 않는다.

## 직접 연결할 때

1. Blueprint Class → All Classes에서 HeroStrikeSkill을 부모로 `BP_Skill_HeroStrike`를 만든다.
2. Class Defaults의 Hero Strike 항목을 확인하고 저장한다.
3. 견습 용사의 DT 행 → Skill Classes에 이 BP를 장착한다. 다른 액티브와 동시에 넣지 않는다.

유닛 BP를 별도로 만들 필요는 없다. 기존 `BP_UnitBase`에서 해당 DT 행을 읽으면 된다.
기존 근접 유닛 행을 복제하여 시작할 경우 기존 액티브를 이 스킬로 교체한다.

노션 견습 상태 초안에 맞춘 능력치 참고:

| 항목 | 1성 원본값 |
|---|---|
| Cost | 1 |
| Max Health / Attack Damage | 500 / 40 |
| Ability Power | 100 |
| Armor / Magic Resistance | 20 / 20 |
| Attack Speed / Attack Range | 0.75 / 1 |
| Start Mana / Max Mana | 0 / 50 |
| Critical Chance / Critical Multiplier | 0.25 / 1.5 |

2성 체력/공격력은 공통 규칙으로 900/72가 된다. 강화 평타 계수는 성급과 별도로 진화 여부에 따라 선택한다.
섬광을 추가하려면 액티브 BP의 Event Graph에서 `On Evolved Impact`를 추가하고 Victim 위치에 이펙트를 재생한다.
흡혈률은 진화 전후에 같은 `Health Damage Heal Ratio`를 쓴다.
이동속도·모델·무기·모션은 노션에서 미정이며, 이번 코드 작업으로 값을 지정하거나 에셋을 바꾸지 않았다.

## 명중·취소·회복 규칙

- 마나가 차면 공통 시전 경로에서 마나를 소모하고 강화를 장착한다.
- 강화가 남거나 해당 강화 투사체가 비행 중이면 이 액티브를 다시 사용하지 않는다. 거절 시 마나를 소모하지 않는다.
- 강화는 시간 제한 없이 다음 실제 명중까지 유지한다. 대상 소멸·기절·평타 취소·유효 타격 실패 시 보존한다.
- 보호막에 완전히 막힌 타격도 실제 명중이므로 강화는 소모하지만 체력 피해가 0이면 회복하지 않는다.
- 물리 방어·피해 감소·보호막 처리 후 `HealthDamage`만 회복 기준으로 사용한다. 초과 처치 피해는 포함하지 않는다.
- 강화 타격 전체에 기존 평타 치명타/피해 증감이 한 번 적용된다. 기본 공격 판정과 마나 획득 규칙은 유지한다.
- 실제 명중 후 회복이 끝나면 이후 일반 평타에는 자체 흡혈이 없다.
- 소유자 사망·전투 종료·라운드 초기화 때 남은 강화를 제거한다. 죽은 소유자를 회복/부활시키지 않는다.
- 원거리 평타에 붙여도 발사 시점에는 회복하지 않는다. 한 탄만 강화를 사용하며, 탄의 대상 소멸/발사 실패 시 다음 공격에 재사용한다.

## 코드 분담

- HeroStrikeSkill: 전용 수치, 재사용 조건, 강화 장착, 해당 타격의 흡혈 계산.
- BattleUnitBase: ArmNextBasicAttack/HasArmedBasicAttack, 타격별 강화 예약·소모, 피해 결과 전달.
- UnitSkillBase: BuildSettings와 OnEmpoweredAttackHit 확장 지점. 기존 네 스킬은 기본 구현을 그대로 사용한다.
- 기존 도적의 시간제 강화는 기존 동작을 유지한다.

## 검증과 Play 확인

자동 검사 이름: `ProjectA.Unit.HeroStrike.*`.
1·2성/치명타, 실제 체력 피해 흡혈, 보호막/초과 처치/회복 상한,
기절·대상 사망·재사용 방지, 중복 Notify, 사망·라운드 초기화, 투사체 예약/실패 복구,
전용 수치 조절과 시전 타이머를 검사한다.

기록: `Saved/Automation/HeroStrike/index.json`, `Saved/Logs/HeroStrike-Automation.log`.
자동 검사는 렌더링 없는 코드 검사다. 직접 BP/DT를 연결한 뒤 실제 화면에서 마나 소모 → 강화 평타 → 회복 순서를 확인해야 한다.

2026-11-18: Development Editor/Win64 빌드 성공, 전체 자동 검사 31개 성공.
HeroStrike 신규 검사 6개는 오류/경고 0이며 기존 Board.MoveLifecycle 경고 1개는 별도다.
DT/공통 AnimBP 파일 해시가 작업 전과 같음을 확인했다. BP/DT를 새로 만들거나 수정하지 않았다.
