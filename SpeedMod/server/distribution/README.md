# SpeedMod Server 0.3.0

[`SpeedMod-Server-0.3.0.zip`](https://github.com/keyboardBK/EnshroudedMods/raw/refs/heads/feature/random-boot-sprint/SpeedMod/server/distribution/SpeedMod-Server-0.3.0.zip)을 내려받아 압축을 풀고, 게임을 종료한 뒤 `install.cmd`를 실행합니다.
Steam 라이브러리에서 게임을 찾으며, 직접 지정하려면 `install.cmd -Game "게임 폴더"`를 사용합니다.

호스트 플레이 / 게임 빌드 1285601용 개발 버전입니다. EMF 0.4.1과 필요한 임시 효과 데이터를 포함합니다.
신발의 원본 ItemId를 유지하며, 서버에 마지막 위치·이름과 보너스를 저장하고 연결 실패 시 한 번 새로 추첨합니다.
장착·교체 시 호스트에게 추가 보너스 알림을 표시합니다. 일반 아이템 툴팁은 수정하지 않습니다.

**설치하면 기존 0.2.x SM02 변형 신발을 캐릭터·창고에서 로드되는 대로 삭제합니다.** 원본 신발과 새 버전 신발은 유지하며, 기존 구성과 세이브를 먼저 백업합니다.

자동 테스트·공식 콘텐츠 빌드·설치 롤백을 확인했습니다. 실제 시작 메뉴/DLL 로딩도 확인했지만, 실제 월드의 속도·저장·멀티플레이와 장착 알림의 화면 표시는 아직 검증 전입니다.

게임 원본 데이터와 사용자 세이브는 ZIP에 포함하지 않습니다. `.sha256`은 다운로드 검증용입니다.

[사용법과 한계](../README.ko.md) · [소스](../runtime.cpp)
