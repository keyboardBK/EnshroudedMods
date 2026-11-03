SpeedMod 0.2.1-alpha 설치 안내

1. 게임과 모드 매니저를 종료합니다.
2. SpeedMod-Setup-0.2.1.exe를 실행합니다.
3. 자동으로 찾은 게임 폴더를 확인하거나 직접 선택하고 '설치 / 업데이트'를 누릅니다.
4. 완료 후 게임을 실행합니다. 처음 실행할 때 패치 확인으로 잠시 걸릴 수 있습니다.

게임 클라이언트 빌드 1283531 전용 개발 버전입니다. 전용 서버 실행 파일은 지원하지 않습니다.
호스트와 모든 참가자가 같은 버전을 설치해야 합니다.
실제 신발 효과, 거래·창고, 저장·재시작과 멀티플레이 검증은 아직 남아 있습니다.

설치 프로그램에는 신발 모드의 Lua 데이터 패치와 DLL이 들어 있습니다.
EML 0.2.0과 EMF 0.3.5는 필요할 때 공식 GitHub 배포처에서 자동으로 내려받고 고정된 SHA-256을 검증합니다.
인터넷 연결이 필요할 수 있습니다. 이미 설치된 파일이 정확히 같은 배포본이면 재사용합니다.
별도의 .NET SDK나 PowerShell 명령 입력은 필요하지 않습니다. Windows 10/11의 .NET Framework를 사용합니다.
디지털 서명이 없는 개발용 설치 파일입니다.

설치되는 항목:
- 게임 실행 파일 옆 speedmod_bootstrap.dll, speedmod_eml.dll 및 EMF dinput8.dll
- emf/ 의 EMF 필수 파일과 emf/mods/speedmod_boots/
- mods/SpeedMod/ 의 EML 패치 스크립트
- SpeedModInstaller/ 의 패치 도구, 설치 기록, 라이선스와 백업

기존의 다른 dinput8 모드는 백업과 이름 변경 후 연결하며, 다른 모드 폴더는 삭제하지 않습니다.
다른 EMF 버전이 발견되면 기존 모드 구성을 보호하기 위해 자동 설치를 중단합니다.
SpeedMod config.ini가 이미 있으면 그대로 유지합니다.
실행 중 메모리의 리소스 한도를 확장하지만 디스크의 enshrouded.exe는 바꾸지 않습니다.

백업:
게임 폴더/SpeedModInstaller/backups/날짜-고유번호 에 기존 설정, KFC 데이터와 발견한 저장 파일의 복사본을 둡니다.
저장 파일 원본이나 Steam Cloud 설정은 변경하지 않습니다.
설치 중 실패하면 변경한 파일을 백업에서 되돌립니다. 강제 종료·정전까지 자동 복구를 보장하지는 않습니다.
설치 로그는 설치 창에 표시된 임시 로그 파일과 백업 폴더의 patch.log에 있습니다.

SpeedMod만 제거 (설치기 1.1.0부터):
같은 EXE의 'SpeedMod만 제거' 버튼을 사용합니다. 신발 복구 확인란을 체크해야 제거할 수 있습니다.
mods/SpeedMod/, emf/mods/speedmod_boots/, speedmod_bootstrap.dll과 해당 부트스트랩 연결 항목만 제거합니다.
EML·EMF, 다른 모드, speedmod_eml.dll과 기존 EML 연결은 유지합니다.
SpeedMod를 제외한 나머지 EML 모드를 현재 원본 데이터에 다시 적용합니다. 예전 KFC 전체를 덮어써서 제거하지 않습니다.
제거 파일·설정·현재 데이터·발견한 세이브는 백업하며, 제거 도중 오류가 나면 현재 구성으로 되돌립니다.
SpeedModInstaller/의 도구·라이선스·백업은 남겨 둡니다. 이 폴더는 모드로 로드되지 않습니다.

모드 제거와 설치 실패 복구는 다릅니다.
모드로 변형된 신발을 저장한 뒤 데이터 모드를 제거하면 신발이 유실될 수 있습니다.
설치 이전 KFC 백업을 플레이 후 무작정 복원하는 것도 데이터 모드 제거에 해당할 수 있습니다.
제거 전에는 모드를 유지한 채 호스트 EMF 콘솔에서 speedmod_boots.restore를 실행해 원래 신발로 복구하고 저장해야 합니다.
이 명령은 현재 로드된 인벤토리만 처리합니다. 오프라인 캐릭터와 미로드 창고까지 모두 복구했는지 확인해야 합니다.
모드가 없는 서버·월드로 변형 신발을 가져가는 것은 검증되지 않았으며 유실·오류 위험이 있습니다.

소스와 검증 상태:
https://github.com/keyboardBK/EnshroudedMods/tree/feature/random-boot-sprint
EML 공식 배포본·소스(GPL-3.0): https://github.com/Brabb3l/kfc-parser/tree/v0.2.0
EMF 공식 배포본·소스(MIT): https://github.com/matenm/emf-enshrouded/tree/v0.3.5
개별 라이선스는 SpeedModInstaller/licenses/ 에 설치됩니다.
