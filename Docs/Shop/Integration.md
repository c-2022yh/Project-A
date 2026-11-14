# 성장 시스템 연결

`AShopManager`는 골드, 상품, 리롤, 잠금, 보상 후보와 현재 페이즈를 관리한다. `ARosterManager`는 보유 기물의 ID와 성급을 관리한다. `AItemManager`는 아이템 정의, 보관, 장착, 해제, 드롭과 합성 계승을 관리한다. `UBenchManager`는 7칸 대기석의 빈자리와 점유 조회를 제공한다. `AShopTransactionManager`는 여러 상태를 바꾸는 요청을 검사하고, 보드·전투 변경이 성공한 뒤 상점·보유 기물·아이템 변경을 확정한다. 세 매니저는 서로를 참조하지 않는다. `UShopPanel`은 조회와 조작 요청만 수행한다.

## 보드·전투 담당 연결

`IUnitPlacement`를 구현한 객체를 RosterManager의 `Placement`에 전달한다. 기물 ID는 성장 시스템의 `FGuid`이며, 기물 종류는 전투 담당과 합의한 `FName UnitId`를 사용한다. 전투 기물 클래스와 기본 능력치는 전투 파트에서 매핑한다.

- `GetUnitPosition`, `GetOccupant`는 보드와 7칸 대기석의 점유를 반환한다. 전투 중에서도 아군의 배치 기준 위치가 조회되어야 한다.
- `FindRewardTile`은 전달받은 다음 라운드 출전 한도 내에서 빈 아군 타일을 반환한다. 출전이 불가능하면 성장 쪽에서 대기석을 확인한다.
- `ApplyUnitChanges`는 추가·제거·업데이트·이동 목록을 한 요청으로 받는다. 좌표, 점유, 팀, 출전 한도, 페이즈를 확인한 뒤 보드 변경과 전투 액터 생성·제거·능력치 반영을 함께 적용한다. 교환은 이동 두 건을 보낸다. 합성은 재료 제거와 보존 기물 갱신을 같은 요청으로 보낸다. 실패를 반환할 때 액터와 점유를 원래 상태로 유지한다.
- 능력치 요청에는 성급 배율과 아이템의 합산 보너스가 들어 있다. 기본 능력치를 기준으로 적용할 항목을 전투 파트에서 확정한다. 같은 갱신을 재적용해도 배율이 누적되지 않아야 한다.
- 보드와 대기석에 실제 유닛 액터를 배치하고, `GetHoveredUnit`에서 커서 아래 액터의 보유 ID를 반환한다. 이 ID를 프로필 표시와 판매·장착 대상 선택에 사용한다. 능력치는 `GetUnitProfile`로 조회한다. 대기석은 `RosterManager.GetBench()`로 조회한다. 이 컴포넌트는 `BP_RosterManager`에 기본으로 포함된다. 대기석으로 이동하거나 교환할 때는 `ShopTransactionManager.RequestMoveToBench`를 호출하고, 보드 타일이 목적지면 `ShopTransactionManager.RequestMove`를 호출한다. 두 요청 모두 아이템 자동 해제와 점유 갱신을 함께 처리한다.

## 진행 담당 연결

1. `BP_ShopManager`, `BP_RosterManager`, `BP_ItemManager`를 생성한다. `ShopTransactionManager` 액터의 `Shop`, `Roster`, `Items`에 각각 전달한다. 시작 기물이 필요하면 `ShopTransactionManager.GrantStartingUnit`을 호출한다.
2. `ShopTransactionManager.EnterPreparation(Round, Limit)`을 호출한다. 첫 준비 단계가 확정될 때 시작 골드가 적용된다. R1~R5에 2, 3, 4, 5, 6을 공급한다. 전투 요청과 준비 타이머는 진행 담당이 연결한다. 준비에는 시간 제한을 두지 않고 전투 시작 버튼을 사용한다.
3. 상점 페이즈는 `ShopManager.BeginCombat`과 `ShopManager.BeginSettlement`로 전환한다. 적 처치마다 같은 적에 대해 동일한 ID로 `ShopTransactionManager.GrantKillReward`를 호출한다. 별도 라운드 골드는 `ShopManager.GrantRoundGold`로 지급한다.
4. 보상 라운드는 `ShopTransactionManager.BeginRewardSelection(NextLimit)`으로 후보 4개를 연다. 지급이 확정되면 `ShopTransactionManager.OnRewardReceived`가 발생한다. 진행 담당은 이 이벤트에서 다음 준비로 진입할 수 있다. 공간 부족이나 보드 요청 실패 시 같은 후보에서 다시 선택한다. 보상이 없는 라운드는 정산에서 바로 다음 준비로 전환한다. 최종 결과는 `ShopManager.FinishMatch`를 호출한다. 다음 적을 구성할 때 아군과 보유 상태를 유지한다.

준비 단계에서 구매 즉시 2성 합성이 가능하면 대기석이 가득 차도 구매할 수 있다. 전투 중에는 합성을 보류하므로 빈 대기석이 필요하다.

## 성장 데이터와 UI

- `BP_ShopManager`의 Catalog에 식별자·이름·가격을 설정한다. `DA_ShopOdds` 에셋에서 라운드별 코스트 가중치를 설정한다. 라운드 행이 비어 있으면 카탈로그의 유효 코스트를 균등하게 선택하고 해당 코스트 안에서 기물을 균등 추첨한다.
- `BP_ItemManager`가 참조하는 `DT_Items`에 완제품 아이템을 입력하고 ShopManager의 Reward Pool에 기물·아이템 후보를 등록한다. 보상 선택을 열려면 양수 가중치의 유효 후보가 4개 이상 필요하다.
- 시작·처치 골드와 추가 골드·아이템 확률 기본값은 0이다. 초기 판매 배율은 1성·2성 모두 기본 가격의 1배이며 변경할 수 있다. 합성은 보드 우선이며 같은 위치 유형에서 먼저 보유한 재료를 보존한다. 성급 능력치 요청 기본값은 1.8배, 외형 1.2배다.
- `ShopPanel`은 UI 서브시스템이다. 월드에서 활성 `ShopTransactionManager`를 찾아 매니저들의 읽기 API로 화면을 그린다. 구매·판매·보상·장착 요청은 `ShopTransactionManager`에 보내고, 리롤·잠금은 `ShopManager`에 보낸다. 매니저에서 UI를 생성하거나 참조하지 않는다. 아이템창은 왼쪽에 있다. 보드 또는 대기석의 월드 기물을 클릭하면 판매·장착 조작 대상을 선택한다. 아이템 버튼을 눌러 장착하고 장착 항목을 눌러 해제한다. 대기석 이동·회수·교환은 보드 입력에서 요청한다. 기존 메인 레벨에는 담당 BP와 진행·보드 연결이 필요하다.

`ProjectA.Shop.Transactions`는 보드 연결을 테스트 대역으로 대체해 거래와 성장 상태를 검증한다.

## 상점 테스트맵

C++ Development Editor 빌드 후 콘텐츠 브라우저에서 `/Game/ShopTests/ShopTest`를 열고 Play를 누른다. 이 맵은 20골드, 1골드 상품 5개, 월드 대기석 7칸으로 시작한다. 구입한 기물은 대기석의 큐브로 표시된다.

1. 카드 하나를 클릭한다. 골드가 19로 줄고 해당 슬롯은 빈칸이 된다.
2. 바로 Reroll을 누른다. 골드가 17이 되고 상품 5개가 다시 채워진다.
3. 원래 상태로 다시 시작하려면 플레이를 중지하고 다시 시작한다.
