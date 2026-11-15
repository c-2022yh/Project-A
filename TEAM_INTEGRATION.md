# 팀 브랜치 통합 — 2026-11-15

통합 브랜치: `codex/integration-team-20261115`

## 합친 작업

| 담당 브랜치 | 통합한 커밋 | 내용 |
|---|---|---|
| Panyong_FirstUpdate | cfeba5a | 기존 유닛 데이터, 스킬, 애니메이션, 사망 페이드, 투사체 |
| Junyeong | e0e5c30 | 드래그, 보드/7칸 대기석 이동·교환, 조작 잠금, 진형 저장·복귀 |
| hongmin_test | 089aef2 | 상점, 보유 기물, 합성, 아이템, 보상, 거래 요청, 독립 테스트맵 |
| PJH_test | b5a6d16 | 5라운드 진행 BP, 라운드 데이터, GameMode 요청 경로, 에디터 제작 도구 |

기존 개인 브랜치와 main은 보존했다. 현재 변경은 로컬 통합 브랜치에만 있다.

## 충돌 해결 기준

- BoardManager/BoardTile은 한쪽 버전을 통째로 선택하지 않고 두 기능을 유지했다.
  전투 중 육각 경로·이동 예약은 기존 전투 코드, 준비 중 이동·교환·대기석은 배치 코드를 사용한다.
- 현재 유닛은 AActor이며 원점이 발 위치다. 캡슐 중심을 더하던 배치/드래그 높이를
  타일 표면 + 1cm 기준으로 맞췄다. 대기석 배치도 SetWaitingPlacement를 호출해
  위치뿐 아니라 Placement/준비 위치 상태를 함께 갱신한다.
- BP_BoardManager는 기존 보드 설정을 유지하고 BenchTileClass에 BP_BenchTile을 연결했다.
- 유닛 BP, DT_Units, AnimBP/몽타주, 아트 에셋은 기존 작업을 보존했다.
- 조작 테스트맵의 구형 PlaceholderMesh 미리보기는 현재 UnitBase와 호환되지 않는다.
  TeamIntegration commandlet의 Repair 모드로 해당 노드만 제거하고 생성 직후
  Knight 행으로 InitializeUnit을 호출하도록 이관한다. 기존 팀과 배치 좌표는 유지한다.

## 아직 연결해야 하는 부분

**브랜치 병합과 게임 전체 흐름 연결은 별도 작업이다.** 메인 맵에서 구매부터
5라운드 종료까지 완성된 상태라는 의미가 아니다.

1. 실제 BoardManager/BattleUnitBase를 구현 대상으로 사용하는 IUnitPlacement 어댑터.
   보유 FGuid와 실제 액터를 연결하고 구매/판매/합성/장착/교환을 원자적으로 처리해야 한다.
   기존 테스트 대역을 실제 연결로 간주하지 않는다.
2. RoundManager의 배치 한도·드래그 상태·전투 시작/정지·결과·준비·보상 요청을
   각 시스템의 실제 완료 응답과 연결해야 한다.
3. 기존 C++ GameMode와 새 BP 진행기의 상태/타이머 소유권을 통일해야 한다.
   BP 진행기의 미연동 시작 차단은 유지한다. 준비 완료 플래그만 켜서 우회하지 않는다.
4. 다음 라운드에서 아군 유지/회복, 적 재구성, 보상과 재시작 정책을 연결한다.
5. 메인 맵 PIE에서 구매 → 배치 → 전투 → 보상 → 다음 라운드를 검증한다.

## 확인 방법과 증거

- UE 5.7 Development Editor 빌드.
- `ProjectA.*` 자동화: 전투/경로/예약/상점 테스트 및
  `ProjectA.Integration.DeploymentAndCombat`으로 배치·교환·잠금·전투·복귀 확인.
- `Scripts/Editor/VerifyTeamIntegration.py`: 에디터 API로 대기석 BP 연결과 핵심 에셋 로딩 확인.
- `-run=CompileAllBlueprints -ProjectOnly`: 프로젝트 BP 전체 컴파일.
- `-run=FlowBuild -FullVerify`: 라운드 진행 BP 독립 검증. 실제 시스템 연결 검증과는 다르다.
- 결과 로그와 JSON은 `Saved/Logs/Team*.log`, `Saved/Automation/TeamIntegration/`,
  `Saved/TeamIntegrationAssetReport.json`에 보관한다.

자동화와 NullRHI 에셋 검사는 화면/애니메이션/실제 클릭을 확인하는 PIE 증거가 아니다.

확인 결과: Editor 빌드 성공, BP 전체 컴파일 오류/경고/로딩 실패 0개,
라운드 진행 독립 검증 255개 검사 통과. 최종 자동화 19개 통과(18 Success,
1 SuccessWithWarnings, 실패/미실행 0). 경고는 점유 중 보드 재생성을 거부하는 검사에서 발생했다.
최종 자동화 결과는 Saved/Automation/TeamIntegration/index.json을 확인한다.
자동화 실행 전 에디터 초기화 로그에는 상세 원인이 없는 `Condition failed` 네 건이 있다.
테스트 큐의 실패는 없지만 로그 전체가 오류 없이 실행됐다고 간주하지 않는다.
