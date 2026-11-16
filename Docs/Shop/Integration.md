# 상점·성장 시스템 구조 및 연동

## 1. 시스템 개요

상점·성장 시스템은 플레이어의 기물 구매, 판매, 합성, 아이템 관리, 보상 획득을 담당한다.

각 Manager는 자신이 소유한 데이터와 관련된 규칙을 관리하며, 여러 Manager의 상태를 함께 변경해야 하는 작업은 `ShopTransactionManager`가 조율한다.

### 클래스 구성

| 클래스 | 주요 책임 |
|---|---|
| `AShopManager` | 골드, 상품 목록, 구매 가격, 리롤, 잠금, 보상 후보 |
| `ARosterManager` | 보유 기물, 기물 ID, 성급, 합성 규칙 |
| `UBenchManager` | 대기석 7칸의 논리적 점유 상태 및 대기석 내부 이동 |
| `AItemManager` | 아이템 정의, 미장착 아이템 보관함, 드롭, 장비 변경 규칙 |
| `AShopTransactionManager` | 여러 Manager에 걸친 작업의 조율 및 일관성 보장 |
| `UShopPanel` | 상점·아이템 UI 표시 및 사용자 요청 전달 |

시스템의 기본 원칙은 다음과 같다.

- 각 Manager는 자신의 상태와 규칙을 관리한다.
- 다른 Manager의 정보를 직접 변경하지 않고 해당 Manager의 기능을 사용한다.
- TransactionManager는 여러 Manager의 작업을 하나의 거래로 처리한다.
- 게임 진행 상태는 GameMode가 관리한다.
- 구매와 보드 배치는 별도의 기능으로 구분한다.
- 구매한 기물은 대기석으로 들어간다.

---

## 2. ShopManager

`AShopManager`는 상점의 경제 상태와 상품 목록을 관리한다.

### 소유 데이터

- `Gold`: 현재 골드
- `Catalog`: 판매 가능한 기물 정의
- `Offers`: 현재 상점에 표시된 상품
- `ShopOdds`: 라운드별 상품 등장 확률
- `bShopLocked`: 상점 잠금 여부
- `Rewards`: 현재 보상 선택지
- `RewardPool`: 보상 후보 목록
- 판매 환급 비율 및 처치 골드 설정

### 주요 조회 API

```cpp
int32 GetGold() const;
bool IsShopLocked() const;

int32 GetShopRevision() const;
TArray<FShopOffer> GetOffers() const;

int32 GetRewardRevision() const;
TArray<FRewardOffer> GetRewards() const;

FShopOffer GetOfferDefinition(FName UnitId) const;
```

### 담당 기능

**상품 관리**
- 카탈로그에서 상품 추첨
- 라운드별 확률 적용
- 상점 리롤
- 상점 잠금 및 유지

**골드 관리**
- 구매 비용 차감
- 기물 판매 금액 지급
- 라운드 골드 지급
- 처치 골드 지급

**보상 관리**
- 보상 후보 추첨
- 보상 선택지 관리
- 보상 선택 완료 처리

상점은 게임 전체의 Phase와 Round를 별도로 소유하지 않는다. 필요한 진행 정보는 GameMode를 통해 얻는다.

---

## 3. RosterManager

`ARosterManager`는 플레이어가 소유한 기물과 성장 상태를 관리한다.

### 기물 데이터

```cpp
struct FRosterUnit
{
    FGuid Id;
    FName UnitId;
    int32 Stars;
    int32 BaseCost;
};
```

| 필드 | 설명 |
|---|---|
| `Id` | 기물 인스턴스의 고유 ID |
| `UnitId` | 기물 종류 |
| `Stars` | 현재 성급 |
| `BaseCost` | 구매 가격을 기준으로 한 판매 계산용 값 |

같은 종류의 기물을 여러 개 보유할 수 있으므로 `UnitId`와 별도로 `FGuid`를 사용한다.

### 주요 조회 API

```cpp
TArray<FRosterUnit> GetUnits() const;

bool GetUnit(FGuid Id, FRosterUnit& Unit) const;

FUnitPosition GetPosition(FGuid Id) const;

FGuid FindOccupant(FUnitPosition Position) const;

bool GetProfile(FGuid Id, FRosterUnitProfile& Profile) const;

bool GetUnitEquipment(
    FGuid Id,
    TArray<FName>& Equipment
) const;

UBenchManager* GetBench() const;
```

### 합성 규칙

동일한 종류의 1성 기물 3개가 모이면 2성 기물 하나로 합성한다.

합성에 필요한 재료 선정과 결과 계산은 RosterManager가 담당한다.

주요 내부 함수는 다음과 같다.

```cpp
PlanAdmission(...)
PlanMerge(...)
PlanPendingMerges(...)
```

합성 과정에서 기존 기물 중 하나를 유지하고 나머지 재료를 제거한다.

보드에 배치된 기물이 재료에 포함된 경우에는 보드 기물을 우선 보존한다.

준비 단계에는 구매와 동시에 합성을 진행할 수 있다. 전투 중 구매한 기물의 합성은 다음 준비 단계에서 처리한다.

### 배치 시스템과의 연결

RosterManager는 `IUnitPlacement` 인터페이스를 통해 기물의 실제 위치와 액터 상태를 조회한다.

- 대기석 위치는 `BenchManager`에서 조회한다.
- 보드 위치는 `IUnitPlacement`에서 조회한다.
- 보유 기물의 등록·삭제·성급 변경은 RosterManager가 관리한다.
- 실제 전투 액터와 보드 위치 변경은 배치 시스템에서 처리한다.

---

## 4. BenchManager

`UBenchManager`는 `ARosterManager`가 소유하는 ActorComponent다.

대기석은 총 7칸으로 구성되며, 각 슬롯의 점유 상태는 기물의 `FGuid`로 관리한다.

### 소유 데이터

```cpp
TArray<FGuid> Slots;
```

배열의 각 원소는 대기석 슬롯 하나를 나타낸다.

유효하지 않은 GUID는 빈 슬롯을 의미한다.

### 주요 API

```cpp
int32 GetSlotCount() const;

FGuid GetOccupant(int32 Slot) const;

FUnitPosition FindFreeSlot() const;

FUnitPosition FindPosition(FGuid UnitId) const;

ETransactionResult RequestMove(
    FGuid UnitId,
    int32 Slot
);
```

### 담당 기능

- 대기석 점유 상태 조회
- 빈 대기석 탐색
- 기물의 대기석 위치 조회
- 대기석 내부 이동 및 교환
- 기물 추가·삭제에 따른 점유 갱신

대기석 내부 이동은 `RequestMove()`를 통해 처리한다.

보드와 대기석 사이의 이동은 배치 시스템이 담당하며, BenchManager의 점유 변경과 실제 유닛 액터의 이동이 함께 반영되도록 구성한다.

### 구매 시 사용

구매 요청이 발생하면 TransactionManager는 `FindFreeSlot()`을 통해 입고 위치를 얻는다.

일반 구매에서는 빈 대기석이 필요하다.

단, 구매 즉시 합성이 완성되는 경우에는 새로운 기물이 대기석을 점유하지 않고 합성 재료로 소비될 수 있다.

---

## 5. ItemManager

`AItemManager`는 아이템 정의와 미장착 아이템 보관함을 관리한다.

### 소유 데이터

```cpp
TArray<FName> Inventory;
```

각 원소는 아이템 정의를 식별하는 ID다.

### 주요 조회 API

```cpp
TArray<FName> GetInventory() const;

bool GetItem(
    FName Id,
    FItemDefinition& Item
) const;
```

### 아이템 정의

`FItemDefinition`은 데이터 테이블을 통해 제공된다.

```cpp
struct FItemDefinition
{
    FText Label;
    FText Description;
    FItemBonuses Bonuses;
    bool bCanDropOnKill;
};
```

아이템의 능력치 효과는 `FItemBonuses`에 정의한다.

```cpp
struct FItemBonuses
{
    float HP;
    float AttackDamage;
    float AbilityPower;
    float Armor;
    float AttackSpeed;
};
```

### 담당 기능

- 아이템 정의 조회
- 미장착 아이템 보관
- 아이템 지급
- 처치 시 아이템 드롭
- 장착 및 해제 시 보관함 변경 계획 수립
- 합성 과정에서 계승할 장비 개수 제한

### 장착 아이템의 소유권

아이템 상태는 다음과 같이 구분한다.

**미장착 아이템**
- ItemManager의 Inventory에서 관리한다.

**장착 아이템**
- 실제 전투 Unit이 소유한다.
- Unit이 장착된 아이템과 성급 등의 효과를 반영하여 최종 스탯을 계산한다.

아이템의 정의와 효과 데이터는 ItemManager가 제공한다.

기물 판매 시 장착 아이템은 보관함으로 반환한다.

합성 시 재료가 보유한 장비를 계승하며, 최대 장착 개수를 초과하는 아이템은 보관함으로 반환한다.

---

## 6. ShopTransactionManager

`AShopTransactionManager`는 여러 Manager의 상태를 함께 변경해야 하는 요청을 처리한다.

각 Manager가 제공하는 검증과 변경 기능을 조합하여 거래를 실행한다.

### 참조 객체

```cpp
AShopManager* Shop;
ARosterManager* Roster;
AItemManager* Items;
```

BenchManager는 `Roster->GetBench()`를 통해 접근한다.

### 공개 API

```cpp
bool EnterPreparation();

ETransactionResult GrantStartingUnit(
    FName UnitId
);

bool OpenRewards();

bool GrantKillReward(
    FGuid DefeatedEnemyId
);

ETransactionResult RequestBuy(
    int32 Slot,
    int32 OfferRevision
);

ETransactionResult RequestSell(
    FGuid UnitId
);

ETransactionResult RequestReward(
    int32 Slot,
    int32 SelectionRevision
);

ETransactionResult RequestReroll();

ETransactionResult RequestShopLock(
    bool bLocked
);

bool GrantRoundGold(
    int32 Amount
);

ETransactionResult RequestEquip(
    FGuid UnitId,
    FName ItemId
);

ETransactionResult RequestUnequip(
    FGuid UnitId,
    int32 Slot
);

bool IsConfigured() const;

FShopMatchState GetMatchState() const;
```

### 트랜잭션 처리 구조

여러 데이터가 변경되는 작업은 다음 순서로 처리한다.

1. 작업에 필요한 현재 상태를 조회한다.
2. 관련 Manager가 변경 가능 여부와 변경 내용을 판단한다.
3. 변경 내용을 하나의 작업 단위로 구성한다.
4. 실제 유닛과 배치 상태에 변경을 적용한다.
5. 성공하면 Roster와 Inventory 등의 상태 변경을 확정한다.
6. 거래 결과를 반환하고 변경 이벤트를 발생시킨다.

변경 적용에 실패한 경우에는 거래 전체를 실패 처리한다.

상품 비용이나 합성 재료를 TransactionManager가 직접 결정하지 않고, 해당 데이터를 관리하는 Manager의 기능을 사용한다.

---

## 7. 주요 기능별 처리 흐름

### 7.1 유닛 구매

```text
ShopPanel
    │ RequestBuy()
    ▼
ShopTransactionManager
    │
    ├─ ShopManager
    │   └─ 상품 및 골드 검증
    │
    ├─ BenchManager
    │   └─ 빈 슬롯 조회
    │
    ├─ RosterManager
    │   └─ 기물 추가 / 합성 계획
    │
    └─ BenchManager.ApplyChanges()
        └─ IUnitPlacement.ApplyUnitChanges()
    │
    ▼
기물 및 대기석 상태 확정
    │
    ▼
ShopManager에서 골드 차감 및 상품 소진
```

구매한 유닛은 대기석으로 들어간다.

구매 즉시 합성이 이루어지는 경우에는 RosterManager의 합성 계획에 따라 재료와 결과 기물을 반영한다.

구매 기능에서 직접 보드 배치를 수행하지 않는다.

### 7.2 기물 판매

```text
ShopPanel
    │ RequestSell(UnitId)
    ▼
ShopTransactionManager
    │
    ├─ RosterManager
    │   └─ 기물 및 위치 조회
    │
    ├─ Unit / Placement
    │   └─ 장착 아이템 조회
    │
    ├─ ItemManager
    │   └─ 반환할 아이템 계산
    │
    ├─ 배치 시스템
    │   └─ 기물 제거 반영
    │
    └─ ShopManager
        └─ 판매 골드 지급
```

판매 완료 시 기물은 보유 목록과 배치 위치에서 제거되고, 장착 아이템은 보관함으로 반환된다.

### 7.3 기물 합성

```text
RosterManager
    │
    ├─ 동일 종류의 1성 기물 탐색
    ├─ 합성 재료 결정
    ├─ 보존할 기물 결정
    └─ 2성 기물 변경 계획 생성
    │
    ▼
ShopTransactionManager
    │
    ├─ 장비 계승 내용 구성
    ├─ 장비 개수 제한 반영
    └─ 변경 내용 일괄 적용
    │
    ▼
2성 기물 유지 / 재료 제거
```

합성 규칙은 RosterManager가 담당하고, 장비 계승을 포함한 여러 시스템의 상태 변경은 TransactionManager가 조율한다.

### 7.4 아이템 장착 및 해제

```text
ShopPanel
    │ RequestEquip / RequestUnequip
    ▼
ShopTransactionManager
    │
    ├─ RosterManager
    │   └─ 대상 기물과 장비 조회
    │
    ├─ ItemManager
    │   └─ 보관함 및 장착 변경 검증
    │
    └─ IUnitPlacement
        └─ 실제 Unit 장비 변경 적용
    │
    ▼
아이템 보관함 확정
```

현재 장착 및 해제 요청은 보드에 배치된 기물을 대상으로 처리한다.

장착된 아이템에 따른 스탯 변화는 전투 Unit에서 계산한다.

### 7.5 보상 획득

```text
GameMode
    │ 보상 선택 단계
    ▼
ShopTransactionManager.OpenRewards()
    │
    ▼
ShopManager
    └─ 보상 후보 4개 생성
    │
    ▼
ShopPanel
    └─ 보상 선택
    │
    ▼
ShopTransactionManager.RequestReward()
    │
    ├─ 유닛 보상 → RosterManager / BenchManager
    │
    └─ 아이템 보상 → ItemManager
    │
    ▼
OnRewardReceived
```

유닛 보상은 대기석에 입고한다.

아이템 보상은 미장착 아이템 보관함에 추가한다.

---

## 8. 게임 진행 시스템 연동

게임 진행 상태는 GameMode가 관리한다.

상점·성장 시스템은 `IShopMatchProgress` 인터페이스를 통해 진행 상태를 조회한다.

### 진행 상태 조회

```cpp
FShopMatchState GetShopMatchState() const;
```

```cpp
struct FShopMatchState
{
    EShopPhase Phase;
    int32 Round;
    int32 DeploymentLimit;
};
```

### Phase 종류

| Phase | 의미 |
|---|---|
| `Inactive` | 게임 시작 전 |
| `Preparation` | 준비 단계 |
| `Combat` | 전투 단계 |
| `Settlement` | 정산 단계 |
| `RewardSelection` | 보상 선택 단계 |
| `Finished` | 게임 종료 |

`FShopMatchState`는 상점 시스템에서 사용하는 진행 상태의 표현이며, 실제 진행 상태의 소유자는 GameMode다.

### 단계별 호출

**게임 시작**
- ShopManager, RosterManager, ItemManager, TransactionManager 구성
- 초기 기물 지급 시 `GrantStartingUnit()` 호출

**준비 단계**
- GameMode에서 준비 상태 설정
- `EnterPreparation()` 호출
- 상품 생성 및 갱신
- 대기 중인 합성 처리

**전투 단계**
- GameMode에서 전투 진행
- 적 처치 시 `GrantKillReward()` 호출

**정산 단계**
- `GrantRoundGold()` 호출

**보상 선택 단계**
- `OpenRewards()` 호출
- 유저가 보상을 선택하면 `RequestReward()` 호출
- `OnRewardReceived` 이벤트로 보상 획득 완료 알림

**다음 라운드**
- GameMode에서 라운드 및 출전 인원 제한 갱신
- 준비 상태로 전환
- `EnterPreparation()` 호출

### 거래 가능 단계

`FShopMatchState`에서 다음 함수를 제공한다.

```cpp
bool CanTrade() const;
bool CanManageBoard() const;
```

- 거래는 준비 및 전투 단계에서 가능하다.
- 보드 관련 관리는 준비 및 보상 선택 단계에서 가능하다.

TransactionManager는 현재 게임 상태에 따라 요청을 처리한다.

---

## 9. 보드·전투 시스템 연동

RosterManager는 `IUnitPlacement` 인터페이스를 통해 실제 유닛 액터와 보드 배치 시스템에 접근한다.

### 인터페이스

```cpp
FUnitPosition GetUnitPosition(FGuid Id) const;

FGuid GetOccupant(FUnitPosition Position) const;

bool GetUnitEquipment(
    FGuid Id,
    TArray<FName>& Equipment
) const;

bool GetUnitProfile(
    FGuid Id,
    FRosterUnitProfile& Profile
) const;

FGuid GetHoveredUnit() const;

bool ApplyUnitChanges(
    const TArray<FRosterUnitChange>& Changes,
    EShopPhase Phase,
    int32 DeploymentLimit
);
```

### 주요 데이터

**FUnitPosition**

```cpp
struct FUnitPosition
{
    EUnitLocation Location;
    int32 Row;
    int32 Column;
};
```

- `Bench`: Column을 대기석 슬롯 인덱스로 사용한다.
- `Board`: Row와 Column을 보드 좌표로 사용한다.
- `None`: 배치되지 않은 상태다.

**FRosterUnitChange**

기물 추가, 제거, 변경, 이동을 표현한다.

```cpp
enum class ERosterUnitEdit
{
    Add,
    Remove,
    Update,
    Move
};
```

변경 데이터에는 기물 정보, 목적지, 장비 변경 내용, 성급에 따른 능력치·외형 배율이 포함된다.

### 배치 시스템의 역할

- 실제 기물 액터 생성 및 제거
- 보드와 대기석 사이의 유닛 이동
- 보드 배치 가능 여부 검증
- 출전 인원 제한 적용
- 유닛 교환
- 장비 및 성급 변경을 실제 Unit에 반영
- 유닛 프로필 제공

대기석의 논리적 점유 상태는 BenchManager가 관리하며, 실제 액터와 타일의 점유 상태는 배치 시스템이 관리한다.

두 상태는 기물의 GUID를 기준으로 연결한다.

### 전투 Unit의 역할

실제 전투 Unit은 자신의 전투 관련 상태를 관리한다.

- 기본 스탯
- 최종 스탯
- 성급에 따른 효과
- 장착 아이템
- 아이템 및 성장 효과에 따른 스탯 재계산
- 체력과 전투 행동

ItemManager는 아이템의 정의 및 미장착 보관함을 관리하고, 전투 Unit은 장착 상태와 효과 적용을 담당한다.

---

## 10. ShopPanel

`UShopPanel`은 `UWorldSubsystem`으로 구현된 상점 UI다.

상점 시스템의 조회 API를 통해 화면을 구성하고, 사용자 입력을 적절한 요청 함수로 전달한다.

### 표시 정보

- 현재 골드
- 상점 상품 5개
- 리롤 및 잠금 상태
- 보유 아이템
- 선택한 기물의 정보
- 장착 아이템
- 보상 선택지

### 호출 관계

```text
ShopPanel
    │
    ├─ ShopManager
    │   └─ 상품, 골드, 보상 정보 조회
    │
    ├─ RosterManager
    │   └─ 기물 및 장비 정보 조회
    │
    ├─ ItemManager
    │   └─ 아이템 정보 조회
    │
    └─ ShopTransactionManager
        └─ 구매 / 판매 / 리롤 / 장착 / 보상 요청
```

UI는 거래 결과를 표시하지만, 기물이나 아이템의 상태를 직접 변경하지 않는다.

---

## 11. 초기 설정

### 상점

`BP_ShopManager`에서 다음 데이터를 설정한다.

- `Catalog`: 판매 기물 ID, 이름, 비용
- `ShopOdds`: 라운드별 등장 확률
- `StartingGold`: 시작 골드
- `KillGold`: 처치 골드
- `BonusKillGold`: 추가 처치 골드
- `BonusKillGoldChance`: 추가 골드 확률
- `OneStarRefundRate`: 1성 판매 환급 비율
- `TwoStarRefundRate`: 2성 판매 환급 비율
- `RewardPool`: 보상 후보

### 아이템

`BP_ItemManager`에서 다음 데이터를 설정한다.

- `ItemDefinitions`: 아이템 데이터 테이블
- `KillItemChance`: 처치 시 아이템 드롭 확률

### 기물

`BP_RosterManager`에서 다음 데이터를 설정한다.

- `Placement`: 보드·전투 연동 객체
- `TwoStarStatMultiplier`: 2성 스탯 배율
- `TwoStarVisualScale`: 2성 외형 배율

### TransactionManager

`BP_ShopTransactionManager`에 다음 객체를 연결한다.

```text
Shop   → BP_ShopManager
Roster → BP_RosterManager
Items  → BP_ItemManager
```

BenchManager는 RosterManager에 포함되어 있으므로 별도로 생성하지 않는다.

---

## 12. 테스트

상점·성장 시스템의 자동 테스트는 다음 이름으로 등록되어 있다.

```text
ProjectA.Shop.Transactions
```

테스트에서는 GameMode와 `IUnitPlacement`의 테스트 구현을 통해 거래를 검증한다.

주요 검증 항목은 다음과 같다.

- 구매 성공 및 골드 차감
- 구매한 기물의 대기석 입고
- 빈자리 부족 시 구매 거절
- 구매 실패 시 기존 상태 유지
- 전투 중 구매와 합성 보류
- 준비 단계에서 기물 합성
- 합성에 따른 장비 계승
- 판매와 아이템 반환
- 장착 및 해제
- 리롤 및 잠금
- 처치 골드 및 아이템 드롭
- 보상 선택
- 대기석 내부 교환

### 상점 테스트맵

```text
/Game/ShopTests/ShopTest
```

테스트맵에서는 상점 구매와 대기석의 기물 표시, 이동, 아이템 UI 등을 확인할 수 있다.

---

## 13. 전체 구조 요약

```text
                 GameMode
                    │
             게임 진행 상태
                    │
                    ▼
             ShopTransactionManager
                    │
         ┌──────────┼──────────┐
         │          │          │
         ▼          ▼          ▼
    ShopManager RosterManager ItemManager
                    │
                    ▼
               BenchManager
                    │
                    ▼
              IUnitPlacement
                    │
           ┌────────┴────────┐
           ▼                 ▼
      BoardManager        BattleUnit
```

이 구조에서 각 객체의 핵심 책임은 다음과 같다.

- **GameMode:** 게임 진행을 결정한다.
- **ShopManager:** 경제와 상점 상품을 관리한다.
- **RosterManager:** 보유 기물과 합성을 관리한다.
- **BenchManager:** 대기석의 논리적 점유 상태를 관리한다.
- **ItemManager:** 아이템 정의와 미장착 보관함을 관리한다.
- **BattleUnit:** 장착 아이템과 전투 스탯을 관리한다.
- **BoardManager:** 실제 보드 및 유닛 배치를 관리한다.
- **ShopTransactionManager:** 여러 시스템에 걸친 거래를 조율한다.
- **ShopPanel:** 상태를 표시하고 사용자 요청을 전달한다.

**상점은 기물을 구매하고, 대기석은 구매한 기물을 수용하며, 보드는 기물 배치를 관리한다. TransactionManager는 이 과정에서 여러 객체의 상태 변경이 하나의 거래로 처리되도록 조율한다.**