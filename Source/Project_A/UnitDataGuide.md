# DT_Units 항목과 정리 기준

`/Game/Data/DT_Units`는 구매/배치할 기물의 공통 설정이다.
특수한 전투 특징은 `Skill Classes`로 연결하고, 그 기능의 수치는 스킬 BP에서 설정한다.
적 몬스터 DT는 이번 작업에서 만들지 않았다.

## 행과 항목의 차이

- **행(Row)**: Knight 같은 유닛 종류 하나. Row Name이 내부 ID다.
- **항목/열(Property/Column)**: Cost, Base Stats 같은 모든 행이 공유하는 설정 형식.
- 행을 삭제하면 그 유닛 종류의 생성/테스트 참조가 끊긴다.
- 항목을 제거하려면 C++ 구조체와 참조 코드/BP를 함께 수정해야 한다. DT 셀을 비우는 것과 다르다.

현재 저장된 행은 아래 네 개다. 모두 Cost=1이며, 이번 작업에서는 어느 행도 삭제하지 않았다.

| Row Name | 표시 이름 | 역할 | 스킬 BP |
|---|---|---|---|
| Knight | 기사 | 근접 보호막 탱커 | BP_Skill_KnightShield |
| Mage | 마법사 | 원거리 평타, 화염구 범위 피해 | BP_Skill_MageFireball |
| Archer | 궁수 | 원거리 평타, 속사 버프 | BP_Skill_ArcherRapidFire |
| Rogue | 도적 | 근접 평타, 다음 평타 강화 | BP_Skill_RogueWeakPoint |

## 현재 DT: 최상위 6개 항목

| 에디터 항목 | 의미와 현재 역할 |
|---|---|
| Cost | 1~5코스트 분류. 초기화 시 범위를 검사한다. 현재 구매/판매 가격은 상점의 별도 Offer.Cost/BaseCost 경로이므로 이 값을 바꾸는 것만으로 상점 가격이 바뀌지는 않는다. |
| Base Stats | 1성 원본 능력치. 최종 능력치 계산의 출발점이다. |
| Skill Classes | 장착할 스킬 BP/C++ 클래스. 액티브 최대 1개와 패시브 여러 개. 스킬 수치는 각 스킬 BP에서 편집한다. |
| Basic Attack | 평타 방식·피해·판정 시점·투사체 설정. |
| Visuals | 몸체·무기·소켓·외형 보정. |
| Animations | 공통 AnimBP와 대기/이동/평타/사망/소환 모션. 스킬 시전 몽타주는 스킬 BP에 둔다. |

Row Name은 위 6개 항목과 별개인 행의 고유 ID다. 기사=Knight처럼 유닛 종류를 구분한다.
최상위 항목에 마우스를 올리면 한국어 툴팁으로 의미를 확인할 수 있다.

### 이번에 제거한 항목

- Display Name / Description / Icon / Role Tags: 전투 C++ 직접 참조가 없으며 BP 에셋 23개 조사에서도 연결된 필드 핀이 없었다.
- Hit Montage: C++ 자동 재생 호출이 없고 네 행 모두 None, AnimBP의 해당 핀도 미연결이었다.
- Star Health Multipliers / Star Attack Multipliers: 네 행 모두 [1, 1.8]이었다. 공통 코드 UnitProgression::TwoStarStatMultiplier로 통일했다.
- Skills: 이전 DT 효과 배열과 Legacy 어댑터를 완전히 제거했다. 실행 설정은 각 스킬 인스턴스의 Settings가 소유한다.
- Is Boss: 앞선 정리에서 제거했다. 기절 저항은 유닛 DT 밖의 CombatPolicy.StunDurationMultiplier로 설정한다.

삭제 전 이름·설명·태그 등의 값은 Scripts/Editor/Archives/DT_Units.removed-fields.json에 보관했다.
이 파일은 역사적 백업이며 런타임이 읽는 별도 데이터가 아니다.
현재 상점 표시 이름은 기존 FShopOffer.Label 경로를 유지한다. 새 UI가 필요해지면 그때 관리 위치를 정한다.

### 성급과 스킬 설정

- 모든 유닛의 2성 체력·공격력 배율은 공통으로 1.8이다. 1성은 1이며 3성은 현재 지원하지 않는다.
- 영구 성장과 장비 보너스는 이 배율 적용 후 더하므로 중복 승급되지 않는다.
- 스킬의 2성 기본값/계수는 스킬 BP에서 별도로 설정한다. 유닛 DT에 스킬 수치를 넣지 않는다.
- 런타임에서 DT Definition에 스킬 설정 사본을 다시 넣지 않는다. 투사체는 발사 때 필요한 설정과 피해값만 복사한다.

## Base Stats: 기본 능력치

| 항목 | 역할/단위 |
|---|---|
| Max Health | 최대 체력. 성급 체력 배율 적용. |
| Attack Damage | 공격력 AD. 평타와 AD 계수 스킬에서 사용. 성급 공격력 배율 적용. |
| Ability Power | 주문력 AP. 스킬의 AP 계수에서 사용. 성급 AD 배율과 별개. |
| Armor | 물리 피해 방어력. |
| Magic Resistance | 마법 피해 저항력. |
| Attack Speed | 초당 공격 횟수. 공격 간격과 평타 모션 길이에 반영. |
| Attack Range | 육각 타일 단위 평타 사거리. 스킬 사거리와 별개. |
| Movement Speed | 월드 길이/초. ‘초당 타일 수’가 아니다. |
| Start Mana | 전투 시작 마나. 액티브가 없는 유닛은 마나를 사용하지 않는다. |
| Max Mana | 액티브 자동 시전 기준 최대 마나. |
| Critical Chance | 치명타 확률. 0.25 = 25%. |
| Critical Multiplier | 치명타 총 피해 배율. 1.5 = 150%. |

현재 HP/마나, 보호막, 버프 후 최종 능력치는 유닛 인스턴스가 별도로 보관한다.
Base Stats를 실행 중 계속 변경해 버프/진화를 표현하지 않는다.

## Basic Attack: 평타

| 항목 | 역할/정리 판단 |
|---|---|
| Delivery | Melee면 즉시 타격, Projectile이면 비행 후 명중. |
| Damage Type | Physical/Magical/TrueDamage. 외형이 마법 탄이어도 피해 유형은 별도다. |
| Attack Damage Ratio | 기본 AD 계수. 1 = AD 100%. |
| Flat Damage | 평타에 더하는 고정 피해. |
| Can Crit | 평타 치명타 허용 여부. |
| Use Anim Notify | true면 공격 애니메이션의 CommitAttackHit(ActionId) 호출로 타격. false면 타이머 타격. |
| Animation Seconds At Speed One | 공속 1 기준 평타 행동 길이. Notify를 써도 공속에 따른 모션 길이/행동 시간 계산에 쓰므로 삭제 후보가 아니다. |
| Hit Time Ratio | 타이머 방식일 때 행동 길이 중 타격 비율. Notify 방식에서는 사용하지 않지만 테스트 러너의 타이머 공격에서 사용한다. |
| Projectile Speed | 평타 투사체 속도. 근접에서는 사용하지 않는다. |
| Projectile Max Lifetime | 평타 투사체 최대 수명. 근접에서는 사용하지 않는다. |
| Projectile Visual | 평타 투사체 Mesh/Material/Scale/Rotation. 스킬 탄 외형은 스킬 BP에 따로 있다. |

투사체 설정은 모든 평타에 사용되지는 않지만 근접/원거리 공통 형식이므로 유지한다.
이 부분은 유닛 고유 진화/흡혈 같은 특수 기능이 늘 때마다 확장하는 데이터가 아니다.

## Visuals: 몸체와 무기

| 항목 | 역할 |
|---|---|
| Skeletal Mesh | 유닛 몸체 모델. |
| Material Overrides | 몸체 머티리얼 슬롯 덮어쓰기. 비우면 메시 기본 재질. |
| Visual Transform | 모델의 크기·높이·정면 보정. 타일의 논리 위치/Actor 스케일과 분리. |
| Main Hand / Off Hand → Mesh | 손에 부착할 무기/방패 등 정적 메시. None이면 해당 손에 표시하지 않는다. |
| Main Hand / Off Hand → Socket Name | 몸체에 존재하는 부착 소켓. 잘못 지정하면 무기를 숨기고 검증 메시지를 남긴다. |
| Main Hand / Off Hand → Relative Transform | 소켓 기준 무기 위치·회전·크기 보정. |

## Animations: 기본 동작

| 항목 | 실제 역할/정리 판단 |
|---|---|
| Anim Class | 공통 ABP_Unit 같은 AnimBP 클래스. C++가 Body에 적용한다. |
| Idle Animation | 대기 모션. AnimBP가 없을 때 C++가 반복 재생하는 fallback도 있다. |
| Move Animation | 이동 모션 데이터. C++ 직접 재생은 없으므로 실제 AnimBP의 읽기 여부를 기준으로 판단한다. 이동 동작용이라 성급히 삭제하지 않는다. |
| Attack Montage | C++가 평타 시작 시 재생. **유지.** |
| Death Montage | C++ 사망 모션 및 마지막 자세/페이드 대기 길이. **유지.** |
| Spawn Montage | 소환 연출. 테스트 러너 등에서 PlaySpawnAnimation을 호출할 때 사용. **유지.** |

## 확인 결과와 사용처

- 스키마: UnitTypes.h의 FUnitDefinition과 중첩 구조체.
- 공통 성급 배율: UnitTypes.h의 UnitProgression.
- 전투 읽기 경로: BattleUnitBase.cpp의 InitializeUnit/RecalculateStats/ApplyVisuals/TryBasicAttack/AddModifier.
- 상점 분리 근거: Shop/ShopTypes.h, Shop/ShopPanel.cpp의 UnitLabel, Shop/ShopTransactionManager.cpp의 구매/가격 처리.
- BP 조사: Saved/UnitDataCleanup/BlueprintReferences.txt.
- 변경 전후 값: Saved/UnitDataCleanup/Before.json, Saved/UnitDataCleanup/After.json.
- 스킬 구성: [Skills/README.md](Skills/README.md).

BP 조사에서 ABP_Unit의 Idle Animation / Move Animation 핀은 실제 연결되어 있었다. 두 항목은 유지한다.
스키마 변경에 맞춰 관련 구조체 노드를 갱신하고 기존 애니메이션 연결이 유지되는지 확인한다.
실제 유닛 네 행의 능력치·평타·외형·모션·스킬 BP 기본값은 변경 전후 비교 대상으로 유지한다.
적 몬스터 DT나 보스 생성 기능은 이번에 새로 구현하지 않는다.

자동 검사 리포트: Saved/Automation/UnitDataCleanup/index.json.
빌드/코드/데이터 검사는 실제 Play 표시와 별개다. 이번 정리 후 실제 Play 확인은 별도다.

2026-11-17 최종 확인: Development Editor/Win64 빌드 성공, 자동 검사 25개 모두 성공.
기존 Board.MoveLifecycle 경고 1개가 포함된다. 신규 스킬/공통 성급/초기화 검사 오류는 없다.
DT의 6개 항목과 네 행, 스킬 BP 설정 보존 및 AnimBP 대기/이동 연결 보존을 확인했다.
