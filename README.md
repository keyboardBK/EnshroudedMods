# SpeedMod 계산형 실험 — experiment/boots-formula

**새 실험 코드는 [Experiments/BootLab](Experiments/BootLab/README.md)에 있습니다.** 기존 SpeedMod 폴더와 아래 설치 EXE는 비교 기준인 0.2.1 랜덤 버전이며, 새 실험을 설치하지 않습니다.

이 브랜치는 독립 시험용입니다. [계산형](https://github.com/keyboardBK/EnshroudedMods/tree/experiment/boots-formula/Experiments/BootLab) · [외부 저장형](https://github.com/keyboardBK/EnshroudedMods/tree/experiment/boots-ledger/Experiments/BootLab) · [아이템 데이터형](https://github.com/keyboardBK/EnshroudedMods/tree/experiment/boots-metadata/Experiments/BootLab)

배포 ZIP은 해당 실험의 distribution 폴더에 있으며, 실제 월드·저장·멀티플레이 검증을 완료한 배포판이 아닙니다. 기존 변형 신발이 있는 실제 캐릭터에 바로 교체하지 마세요.

---

# 비교 기준: SpeedMod 0.2.1 랜덤 버전

신발의 **실제 아이템 레벨·등급**으로 추가 질주속도를 한 번 추첨합니다. 플레이어 레벨은 사용하지 않습니다.
기존 보유품, 제작품, 드롭품 모두 서버에서 처음 확인될 때 대상이며 외형 신발은 제외합니다.
기존 질주 옵션은 보존하고 보너스를 더합니다. 기본 아이템 툴팁에도 합산 수치가 들어갑니다.

## 쉬운 설치

**[Windows 설치 EXE 다운로드](https://github.com/keyboardBK/EnshroudedMods/raw/refs/heads/feature/random-boot-sprint/SpeedMod/distribution/SpeedMod-Setup-0.2.1.exe)** → 게임 종료 → 게임 폴더 확인 → **설치 / 업데이트**.

모드 데이터와 DLL이 포함돼 있고 EML·EMF는 공식 배포처에서 자동으로 받아 검증합니다. 처음 설치할 때 인터넷이 필요할 수 있습니다. Windows 10/11, 게임 클라이언트 빌드 1283531용 개발 버전입니다. 호스트와 참가자 모두 같은 설치 파일을 사용합니다.

[설치기 동작·검증·빌드 안내](docs/installer.md)

## 기본 범위

단위는 추가 질주속도 %이며 양끝을 포함하는 균등 정수 추첨입니다.

| 등급 | 아이템 레벨 1 | 아이템 레벨 45 |
|---|---:|---:|
| 일반 | 1–5 | 10–15 |
| 고급 | 5–9 | 13–36 |
| 희귀 | 8–13 | 15–58 |
| 영웅 | 12–16 | 18–79 |
| 전설 | 15–20 | 20–100 |

중간 레벨은 최소·최대값을 각각 선형 보간하고 반올림합니다. 45 초과는 45 범위를 사용합니다.
고급·희귀·영웅은 개발 기본값입니다. `emf/mods/speedmod_boots/config.ini`에서 범위를 수정한 후 게임/서버를 재시작합니다.
이미 추첨된 신발은 설정을 바꾸거나 강화해도 재추첨하지 않습니다.
예를 들어 기존 +6% 신발에서 +17%가 추첨되면 툴팁은 +23%를 표시합니다. 기존 게임 자체의 반올림 오차는 남습니다.

## 값 보존 방식과 설치

116종 기본 신발에 대해 0–100% 변형 11,716개를 EML 데이터로 등록합니다.
서버의 EMF 보조 모드가 최초 추첨 후 해당 변형의 영구 ItemId를 신발에 지정합니다.
기존 개별 장비 개체(PIDE), 레벨, 등급, 강화, 내구도, 염색과 보석은 유지하며 신발을 삭제·재생성하지 않습니다.
변형 정의는 원본 아이콘 조회용 objectId를 공유합니다. 아이콘 리소스를 수천 개 복제하지 않습니다. 추첨 결과는 저장되는 ItemId에 포함됩니다. 이후 거래·창고·재장착·재접속 때 이미 변형된 신발은 재추첨하지 않는 구조입니다.
이는 실제 서버 저장/거래 검증을 모두 마쳤다는 뜻은 아닙니다. 아래 검증 상태를 확인하세요.

**호스트와 모든 참가자에게 같은 EML 데이터가 필요합니다.** 호스트만 설치하는 방식이 아닙니다.
추첨 처리는 서버에서만 실행합니다. 현재 지원 대상은 게임 클라이언트에서 여는 호스트 세션입니다. 전용 서버 실행 파일은 부트스트랩이 지원하지 않습니다. 참가자 이동 예측은 아직 검증하지 않았습니다.
다른 서버로 신발을 가져가는 것은 지원 범위 밖입니다.

- `SpeedMod/build/speedmod_bootstrap.dll` → 게임 실행 파일 옆
- `SpeedMod/eml` → 게임의 `mods/SpeedMod`
- 빌드한 `speedmod_boots.dll`, `SpeedMod/runtime/mod.json`, `SpeedMod/runtime/config.ini` → `emf/mods/speedmod_boots`
- EML과 EMF가 함께 로드되어야 합니다. 기존 `dinput8.dll`을 덮어쓰지 말고 [EMF 체인 로딩 문서](https://github.com/matenm/emf-enshrouded/blob/main/docs/modding/other_mods.md)를 따르세요.
- 게임 빌드 1283531, EMF 0.3.5 / API 16 이상을 대상으로 합니다. 다른 게임 빌드에서는 런타임이 작업을 거부합니다.
- 0.1의 임시 효과 DLL을 0.2 DLL로 교체해야 중복 보너스가 생기지 않습니다.

0.2.1에는 실행 초기의 리소스 메모리 보정이 필요합니다. 기존 `dinput8-chain.ini`에서 EML 앞에 부트스트랩을 추가합니다. 다른 모드의 기존 연결 항목은 유지하세요. 현재 작업 환경의 예시는 다음과 같습니다.

```ini
[Chain]
speedmod_bootstrap.dll
speedmod_eml.dll
```

부트스트랩은 게임 실행 파일의 정확한 SHA-256과 명령어를 검사한 뒤 **실행 중 메모리에서만** GF_Resource 한도를 490 MiB에서 768 MiB로 확장합니다. 디스크의 실행 파일은 바꾸지 않습니다. 지원하지 않는 실행 파일에서는 게임 시작을 중단합니다. EML 데이터만 유지하고 부트스트랩을 빼면 전체 변형을 읽는 중 충돌할 수 있습니다.

## 제거 전 복구

설치 EXE의 **SpeedMod만 제거** 버튼을 사용하세요. EML·EMF와 다른 모드는 유지하고, SpeedMod 데이터 패치·런타임·부트스트랩만 제거합니다. 남은 EML 모드는 다시 패치하며 제거 실패 시 기존 구성을 복구합니다. 먼저 아래 신발 복구 절차를 완료해야 합니다.

**새 ItemId가 저장된 상태에서 EML 데이터를 제거하면 해당 신발이 유실될 수 있습니다.**
먼저 모드를 유지한 채 호스트 EMF 콘솔에서 `speedmod_boots.restore`를 실행하세요.
현재 로드된 인벤토리의 신발을 원래 ItemId로 되돌리고, 프로그램을 다시 시작할 때까지 신규 추첨을 멈춥니다.
`speedmod_boots.status`와 `emf/emf_core.log`의 `Restored` 기록을 확인하고 저장하세요.
오프라인 캐릭터, 다른 월드, 로드되지 않은 창고까지 일괄 복구하는 명령은 아닙니다.
모든 보관 위치와 캐릭터가 복구·저장됐음을 확인하기 전에는 EML 데이터를 유지해야 합니다.
게임 업데이트 뒤에도 변형 데이터가 다시 패치되기 전에 세이브를 열지 마세요.

## 0.2.1 시작 오류 수정

0.2.0의 데이터 검사는 통과했지만 실제 시작 메뉴 검증이 누락됐습니다. 전체 데이터로 실행하면 GF_Resource 영역을 넘는 복사에서 충돌했고, 오류 보고용 보조 프로세스까지 EML을 실행하면서 `Failed to create AppState`가 표시됐습니다. 메모리 문제를 해소한 뒤에는 복제 아이콘이 기본 8,192개 UI 테이블을 초과하는 문제도 확인됐습니다.

0.2.1은 초기 메모리 보정, 원본 아이콘 공유, 불필요한 원본 데이터 재작성 축소를 적용합니다. UI/GPU 테이블 용량을 강제로 늘리는 실험 코드는 최종 구성에서 제거했습니다. 0~100% 변형 11,716개와 저장 ID 규칙은 유지합니다.

현재 브랜치는 **개발 중인 랜덤 버전**입니다. 메뉴 진입과 실제 신발 적용·저장 검증을 구분합니다.

실제 메뉴 반복 실행과 원인 분석 기록: [0.2.1 시작 오류 수정](docs/startup-fix-0.2.1.md).

## 검증 상태

- MSVC x64 정적 CRT 빌드, `/W4 /WX` 통과.
- 테스트: 레벨별 범위·양끝 RNG, 실제 등급 우선, 동일 PIDE 유지, 아이템 ID 보존, 복구, 세션 변경·이동 중 요청 취소, 쓰기 실패 시 미변경, 참가자 측 추첨 금지, 불완전 데이터 시 작업 거부.
- 실제 EML 직렬화/역직렬화: 116종 × 101개 변형의 ItemId·질주 설정·툴팁 검증. 기존 신발 정의 불변 확인.
- 현재 제작법에서 기본 신발 ItemId를 직접 요구하는 재료는 없음.
- **실제 게임 내 속도 측정, 툴팁 화면, 거래·창고, 저장 후 재시작, 멀티플레이어는 검증이 남아 있습니다.** 테스트 코드의 모의 전송을 실제 멀티플레이어 검증으로 보지 않습니다.

## 빌드

Visual Studio 2022 C++ Build Tools가 있는 Windows에서 `SpeedMod/build.cmd`를 실행합니다.
DLL·설정·테스트 실행 파일은 `SpeedMod/build`에 생성됩니다.
`third_party/emf`는 공개 [EMF SDK](https://github.com/matenm/emf-enshrouded)와 MIT 라이선스이며 게임 파일·세이브는 저장소에 포함하지 않습니다.

[요구사항과 게임 내 확인 항목](docs/requirements.md)
