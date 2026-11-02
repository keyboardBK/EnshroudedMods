# EnshroudedMods

Enshrouded용 모드 소스입니다. 현재는 신발 질주속도 모드 SpeedMod를 개발 중입니다.

## SpeedMod 0.1.0 알파

실제 착용 신발의 등급에 따라 일반 +5%, 고급 +10%, 희귀 +15%, 영웅 +20%, 전설 +30%의 추가 질주속도를 주도록 구현했습니다.
기존 보유품과 제작품도 대상입니다. 기존 옵션을 덮어쓰지 않고 게임의 기본 장비 효과를 별도로 적용합니다.

- `SpeedMod/eml`: 등급별 임시 효과 5개를 만드는 EML 데이터 모드입니다. 게임의 `mods/SpeedMod`에 설치합니다.
- `SpeedMod/runtime`: 실제 착용 슬롯과 아이템 등급을 읽는 EMF 보조 모드입니다. DLL과 mod.json을 `emf/mods/speedmod_boots`에 넣습니다.
- `SpeedMod/tests`: 가짜 EMF API로 실행하는 컨트롤러 테스트입니다. 실제 게임 이동 물리 테스트와는 다릅니다.
- `third_party/emf`: 빌드에 사용하는 공개 EMF SDK와 MIT 라이선스입니다.

현재 대상은 게임 빌드 1283531입니다. 다른 빌드에서는 런타임 보조 모드가 중단됩니다.
EML과 EMF의 로더 연결이 필요합니다. 같은 이름의 dinput8.dll을 덮어쓰지 말고 EMF의 체인 로딩 문서를 따릅니다.

## 검증 상태

DLL 빌드, 등급 판별·교체·해제·중복 방지·세션 전환 등의 컨트롤러 테스트와 게임 시작 메뉴 로딩까지 확인했습니다.
게임에 반영된 데이터를 원본과 비교해 기존 아이템 4,268개, Perk 365개, 기존 템플릿 12,269개가 같고 임시 효과 5개만 추가됨을 확인했습니다.
사용자 실행 로그에는 일반 신발의 효과 개체 생성 기록이 있습니다. 실제 질주속도 증가와 회수는 아직 측정하지 않았습니다.
멀티플레이의 참가자 이동 예측·서버 보정과 호스트만 설치하는 구성도 미검증입니다.

**현재 버전은 보너스를 아이템 툴팁에 표시하지 않습니다. 표시가 없다는 사실만으로 미적용이라고 판단할 수는 없습니다.**

## 다음 버전 요구사항

[개발 요구사항](docs/requirements.md)에 레벨·등급별 랜덤 범위, 개별 아이템의 값 유지, 툴팁 표시 기준을 기록했습니다.
현재 알파에는 해당 랜덤 기능이 아직 구현되지 않았습니다.

## 빌드

Visual Studio 2022 C++ Build Tools가 있는 Windows에서 `SpeedMod/build.cmd`를 실행합니다.
DLL과 컨트롤러 테스트 결과는 `SpeedMod/build`에 생성됩니다. 게임 파일과 세이브는 저장소에 포함하지 않습니다.

## 사용한 도구

- [EML](https://github.com/Brabb3l/kfc-parser)
- [EMF](https://github.com/matenm/emf-enshrouded)
- [EMF 체인 로딩](https://github.com/matenm/emf-enshrouded/blob/main/docs/modding/other_mods.md)
