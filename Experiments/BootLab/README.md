# 신발별 별도 저장 실험 — experiment/boots-ledger

**상태: 장부 라이브러리와 읽기 전용 관측 DLL 구현·테스트 완료. 게임의 영구 식별자는 미확보.**
따라서 현재 DLL은 랜덤 지급·속도 적용·자동 장부 연결을 실행하지 않습니다.
현재 게임에는 설치하지 않았습니다. 기존 SpeedMod/설치 EXE는 이 실험용 설치기가 아닙니다.

## 확인한 점

신발의 PIDE는 실행 중 개체 번호이며, 재시작 후의 영구 키로 보장되지 않습니다.
원본 종류·레벨·등급·강화·염색·보석·내구도로 만든 관측 서명도 영구 키가 아닙니다.
동일한 신발 두 개가 충돌하고, 수리/강화/염색으로 바뀝니다. 이를 장부 키로 자동 채택하지 않습니다.
CSV에는 프로세스·모듈 실행 구분 값·세션·현재 슬롯·PIDE와 개별 속성의 존재 여부를 기록합니다.

ledger.h는 **검증된 영구 키 제공자가 있을 때 연결할** 저장 엔진입니다.
월드와 아이템의 영구 키로 한 번만 추첨하고, 강화·소유자·창고가 바뀌어도 재추첨하지 않습니다.
재시작 직렬화/역직렬화, 파일 원자 교체, 손상·중복 기록 거부를 테스트했습니다.
영구 키의 재시작 유지·거래 유지·동일템 구별이 모두 확인되지 않으면 추첨을 거부합니다.
현재 게임에서 이 조건을 통과한 키 제공자는 없습니다. 모의 테스트의 고정 키는 실제 게임의 검증 증거가 아닙니다.

## 관측 도구 사용

ZIP의 speedmod_ledger/를 별도 테스트 게임의 emf/mods/에 넣습니다. EMF가 필요하며 EML은 바꾸지 않습니다.
호스트한 월드에서 아래처럼 **서로 다른 라벨**로 캡처합니다.

- speedmod_ledger.status
- speedmod_ledger.capture before_storage
- 같은 신발을 창고로 옮긴 뒤 speedmod_ledger.capture after_storage
- 참가자에게 거래한 뒤 speedmod_ledger.capture after_trade
- 저장·완전 종료·재실행 뒤 speedmod_ledger.capture after_restart

해당 모드 폴더의 observations/에 CSV가 생깁니다. 현재 로드된 인벤토리만 관측합니다.
같은 종류·레벨·등급인 신발 두 개, 수리·강화·염색·보석 변경도 별도로 시험해야 합니다.
Python 3이 있다면 python compare.py observations/before_storage.csv observations/after_storage.csv로 충돌·비교 수를 봅니다.
숫자가 같거나 서명이 유일하게 매칭되어도 permanent_identity_verified는 항상 false입니다. 관측 결과만으로 영구성을 증명하지 않습니다.
DLL을 빼도 원본 아이템은 바뀌지 않습니다. CSV는 별도 관측 파일일 뿐 게임 저장 파일이 아닙니다.

## 다음 구현 조건

실제 저장·전송 과정에서 유지되는 개별 아이템 식별자나 안전한 개별 메타데이터 슬롯을 찾아야 합니다.
그 전에는 현재 서버에서도 “같은 신발의 값 보존”을 보장할 수 없습니다.
연결 이후에도 서버 장부·세이브의 백업 시점 일치, 네트워크 동기화, 툴팁/별도 UI가 필요합니다.

## 빌드·검증

Windows C++ Build Tools에서 build.cmd를 실행합니다. build/speedmod_ledger.dll이 생깁니다.
실제 DLL 로딩/명령/캡처 시험과 별도 저장 엔진 테스트를 실행합니다. 라이브 거래·저장 시험은 아닙니다.
package.ps1은 DLL·manifest·설명과 비교 도구를 묶은 ZIP을 만듭니다.
