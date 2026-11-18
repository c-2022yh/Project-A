# 기물 사망 페이드 머티리얼

`SetupUnitDeathFade.py`는 UE 5.7 Python용 에디터 스크립트다.
기사 `M_Knight`와 마법사 `M_Mage`에만 `UnitOpacity`(기본값 1) →
`DitherTemporalAA` → `Opacity Mask`를 연결하고 Blend Mode를 Masked로 설정한다.
기존 Base Color, 텍스처, 다른 입력은 유지한다. 이 머티리얼을 사용하는 몸체와 무기에 함께 적용된다.
이미 설정된 에셋은 연결을 검증하고 건너뛴다. 엔진 기본 머티리얼은 수정하지 않는다.

에디터의 에셋과 맵을 저장하고 종료한 뒤 PowerShell에서 프로젝트 루트 기준으로 실행:

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  "$PWD/Project_A.uproject" -EnablePlugins=PythonScriptPlugin -run=pythonscript `
  "-script=$PWD/Scripts/Editor/SetupUnitDeathFade.py" -unattended -nullrhi -nosound -nop4
```

결과는 `Saved/UnitDeathFadeMaterialSetup.json`에 기록한다.
스크립트/NullRHI 검사는 실제 화면의 디더 품질을 보증하지 않는다.

## 클래스 기반 스킬 이전

기사/마법사/궁수/도적의 스킬 BP 이전은 완료했다. 이전 DT 효과 필드와 일회성 이전 도구는 제거했다. 구조와 설정 위치는 [스킬 안내](../../Source/Project_A/Skills/README.md)를 참고한다.

## 런타임 흐름

- BattleUnitBase가 몸체/MainHand/OffHand의 모든 머티리얼 슬롯을 확인한다.
- `UnitOpacity`가 있는 머티리얼만 기물별 동적 인스턴스로 만든다.
- 사망 즉시 기존 판정대로 타일/예약을 반환하고 타깃에서 제외한다.
- 사망 몽타주는 재생 인스턴스에서 Auto Blend Out을 꺼 마지막 자세를 유지한다.
  원본 몽타주 에셋 설정은 변경하지 않는다.
- 몽타주 길이만큼 기다리고, `Death Pose Hold Seconds`(기본 0.5초) 대기 후
  `Death Fade Seconds`(기본 0.8초) 동안 UnitOpacity를 1 → 0으로 바꾼다.
- 전투 종료 후에도 Tick에서 연출을 계속하며, 0이 되면 Actor를 숨긴다. 파괴하지 않는다.
- ResetForRound/재초기화 시 연출 진행을 취소하고 표시/알파/애니메이션을 복구한다.
- 몽타주가 없거나 재생에 실패하면 모션 대기 시간은 0이다. 페이드 시간을 0으로 하면 대기 후 즉시 숨긴다.

추가 머티리얼을 쓰는 기물은 동일한 `UnitOpacity` 입력을 연결해야 한다.
현재 Ranger/Rogue 및 엔진 기본 WorldGrid 머티리얼은 수정하지 않았다.
다른 머티리얼을 쓰는 무기는 그래프 연결을 확인해야 한다.

## Play 확인

1. TestMap 러너에서 기사/마법사 행의 Death Montage가 지정됐는지 확인한다.
2. 죽는 즉시 다른 유닛이 타깃을 바꾸고 타일 점유가 해제되는지 확인한다.
3. 사망 모션 → 마지막 자세 → 몸체/무기 동시 페이드 → 숨김 순서를 확인한다.
4. 한 팀 전멸 후에도 마지막 사망자의 페이드가 끝나는지 확인한다.
5. 일반 라운드 담당은 전원 StopCombat 후 전원 ResetForRound를 호출한다.
   테스트 러너의 RestartTest는 기존 유닛을 삭제하고 새로 만드는 동작이다.

검사 결과/로그: `Saved/Automation/UnitDeathFade/index.json`,
`Saved/Logs/UnitDeathFade-Automation.log`.

2026-11-15 검증: Development Editor / Win64 빌드 성공, `ProjectA.` 자동 검사
15개 통과(테스트별 오류/경고 0). 현재 DT Knight/Mage의 몸체/무기 머티리얼 연결,
마지막 자세 보존, 전투 종료 후 페이드, 생존자 머티리얼 독립, 숨김/복구,
몽타주 미설정과 0초 설정을 검사했다. NullRHI 검사이므로 실제 화면/플레이어 빌드 확인은 남아 있다.
에디터 시작 로그의 기존 DamageId 비결정적 초기화 진단과 EditorPerf 메뉴 경고는 별도다.
