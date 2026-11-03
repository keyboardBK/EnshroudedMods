# EML 변형 아이템을 대체하는 세 가지 실험

기준 커밋은 b059dd1이며 기존 feature/random-boot-sprint와 main은 유지합니다.
실험 코드는 각 브랜치의 Experiments/BootLab에 있습니다. 기존 SpeedMod 폴더와 설치 EXE는 비교 기준 코드이며 실험을 설치하지 않습니다.

| 브랜치 | 구현한 것 | 남은 핵심 검증 |
|---|---|---|
| [experiment/boots-formula](https://github.com/keyboardBK/EnshroudedMods/tree/experiment/boots-formula/Experiments/BootLab) | 원본 신발을 유지하는 레벨·등급 계산, 읽기 전용 미리보기, 단독 호스트 테스트용 속도 적용/회수 | 실제 이동·로컬 예측·효과 충돌·저장 후 재시작. 개별 랜덤과 기본 툴팁은 없음 |
| [experiment/boots-ledger](https://github.com/keyboardBK/EnshroudedMods/tree/experiment/boots-ledger/Experiments/BootLab) | 한 번 추첨하는 외부 장부 엔진, 원자 저장·재로드·손상 거부, 읽기 전용 신발 관측/비교 | 영구 아이템 식별자 미확보. 자동 추첨과 속도 적용은 비활성 |
| [experiment/boots-metadata](https://github.com/keyboardBK/EnshroudedMods/tree/experiment/boots-metadata/Experiments/BootLab) | 개별 필드 읽기 전용 관측, 실제 게임 리플렉션 감사 | 안전한 보너스 저장 슬롯 미확보. 아이템 직접 쓰기는 없음 |

각 브랜치 distribution/에 DLL·manifest·시험 절차를 묶은 ZIP과 SHA-256이 있습니다. Windows/EMF/게임 빌드 1283531용 개발 도구입니다. EML이나 새 아이템 정의를 포함하지 않습니다.

## 확인한 결과

- 세 DLL 모두 MSVC /W4 /WX 빌드·모의 API 테스트를 통과했습니다.
- 빌드한 DLL을 실제 LoadLibrary로 열어 지원하지 않는 API 거부, 명령 등록, 읽기 전용 CSV 캡처, 경로 이탈 거부를 확인했습니다. 이는 게임 월드 실행 시험이 아닙니다.
- 계산형은 모의 테스트에서 기존 +6% 유지, 반복 중첩 방지, disable/맨발/참가자 진입 시 회수, 오래된 세션·아이템 요청 거부, 쓰기 실패·외부 속도 변경 거부를 확인했습니다. 기본은 읽기 전용입니다.
- 실제 빌드의 Movement는 do_not_save·server_only입니다. 서버 값 수정이 화면상 이동 예측까지 제대로 반영되는지는 별도 실제 시험이 필요합니다.
- 외부 장부는 모의 영구 키에 대해 한 번 추첨·재접속 직렬화·원자 파일 교체·손상/중복 거부를 통과했습니다. 게임에서 영구 키를 확보했다는 뜻은 아닙니다.
- 동일한 신발 두 개의 속성 서명이 충돌하며, 내구도·강화·염색 변경으로 서명이 바뀔 수 있음을 확인했습니다. PIDE나 서명을 영구 키로 자동 사용하지 않습니다.
- 실제 .cache/types.json에서 ItemState, PerkContainerNew, PerkContainer, Durability, ColorPaletteItem, WeaponGemSlot, UsedItem의 크기·선언 필드·오프셋을 확인했습니다. 이 범위에서 임의 보너스를 안전하게 저장할 전용 필드는 확인하지 못했습니다.
- 실제 사용자 게임의 모드·패치 데이터·캐릭터·월드 저장은 변경하지 않았습니다. 실험 패키지는 자동 설치하지 않았습니다.

## 시험 순서

계산형은 별도 깨끗한 테스트 캐릭터/월드에서 시작합니다. 기존 랜덤 변형 신발을 가진 실제 캐릭터에 바로 교체하지 않습니다. 기존 버전을 제거하려면 먼저 신발 복구·저장을 완료해야 합니다.

장부·메타데이터 도구는 현재 로드된 인벤토리를 읽어 관측 파일만 작성합니다. 창고 이동→거래→저장·완전 재시작 전후를 각각 다른 라벨로 기록합니다. PIDE가 몇 번 같아 보이는 것만으로 영구성을 단정하지 않습니다.

실제 월드·저장·멀티플레이 검증이 완료된 배포판은 아직 없습니다. 모든 방식에서 게임 업데이트 후 런타임 호환 확인은 필요합니다.
