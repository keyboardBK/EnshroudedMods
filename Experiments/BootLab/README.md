# 아이템 저장 데이터 실험 — experiment/boots-metadata

**상태: 읽기 전용 필드 관측 DLL과 실제 게임 스키마 감사 도구 구현·테스트 완료. 직접 보너스 저장은 미구현.**
검증되지 않은 필드·패딩·강화·내구도·보석에 값을 숨기는 쓰기는 하지 않습니다.
현재 게임에는 설치하지 않았습니다. 기존 SpeedMod/설치 EXE는 이 실험용 설치기가 아닙니다.

## 분석 결과

빌드 1283531의 실제 .cache/types.json에서 다음 7개 컴포넌트를 읽어 구조를 확인했습니다.

- ItemState: 현재 보관 위치, 종류 ID, UI용 레벨/등급/공격력, 상태 플래그
- PerkContainerNew: 사용 가능/해금된 Perk 수
- PerkContainer: 기존 Perk ID와 해금 정보
- Durability: 게임의 내구도 값
- ColorPaletteItem: 염색 ID
- WeaponGemSlot: 보석 아이템
- UsedItem: 원본 아이템 종류 ID

확인한 선언 필드 중 임의의 질주 보너스를 안전하게 저장할 전용 슬롯은 없습니다.
예를 들어 ItemState의 끝 패딩은 선언 필드가 아니며, 빈 바이트로 보인다고 저장·네트워크 전송된다는 근거가 되지 않습니다.
스키마의 필드 존재만으로 저장 여부를 단정하지 않습니다. 다른 저장 경로나 개별 메타데이터 가능성까지 배제한 결론도 아닙니다.

## 관측 도구 사용

ZIP의 speedmod_metadata/를 별도 테스트 게임의 emf/mods/에 넣습니다. EMF는 유지하고 EML은 바꾸지 않습니다.
호스트한 월드에서 콘솔 명령을 실행합니다.

- speedmod_metadata.status
- speedmod_metadata.inspect
- speedmod_metadata.capture before_save
- 저장·완전 종료·재실행 후 speedmod_metadata.capture after_restart

이 모드 폴더의 observations/에 CSV가 생깁니다. 현재 로드된 신발만 관측합니다.
PIDE·보관 위치·레벨·등급·Perk·염색·보석·내구도와 해당 컴포넌트의 존재 여부를 비교할 수 있습니다.
DLL이 신발 ID나 개별 속성을 바꾸지 않으므로 제거 전 신발 복구 명령은 필요 없습니다.

## 오프라인 스키마 감사

Python 3으로 아래 명령을 실행합니다.

python audit_schema.py "게임 폴더/.cache/types.json" --output build/schema-audit.json
python -m unittest -v test_audit.py

입력 파일을 읽기만 합니다. known은 현재 확인한 레이아웃이라는 뜻이며 쓰기/저장이 검증됐다는 뜻이 아닙니다.
새 필드나 크기 변화는 changed로 표시하고 조사 대상으로 남깁니다.
네이티브 헤더에 있는 속성 레지스트리 헤더는 리플렉션의 structFields에 개별 선언되지 않을 수 있습니다.

## 다음 구현 조건

직렬화·역직렬화와 네트워크 전송 경로, 기존 옵션 보존을 검증해야 합니다.
정수 범위를 임의 Perk 수나 내구도에 인코딩하는 방식은 본래 게임 의미를 바꾸므로 채택하지 않습니다.
전용 슬롯이 확인되기 전에는 직접 저장 모드를 배포할 수 없습니다.

## 빌드·검증

Windows C++ Build Tools에서 build.cmd를 실행합니다. build/speedmod_metadata.dll이 생깁니다.
모의 API와 실제 DLL 로딩 시험에서 아이템 메모리가 불변인지 확인합니다. 실제 세이브 왕복 시험은 아닙니다.
package.ps1은 DLL·manifest·설명과 스키마 감사 도구를 묶은 ZIP을 만듭니다.
