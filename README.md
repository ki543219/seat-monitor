# Seat Monitor — ESP32-S3 자리 비움 감지

[![CI](https://github.com/ki543219/seat-monitor/actions/workflows/ci.yml/badge.svg)](https://github.com/ki543219/seat-monitor/actions/workflows/ci.yml) · [상태 페이지](https://ki543219.github.io/seat-monitor/) · [예시 데이터](https://ki543219.github.io/seat-monitor/?demo)

책상 위 ESP32-S3 카메라가 **자리에 있음 / 짐만 두고 자리 비움 / 빈 자리**를 기기 안에서 판단하고, 상태 변화와 머문 시간을 Supabase DB에 기록합니다. 영상은 기기 밖으로 나가지 않고 판정 결과만 전송됩니다.

같은 기기를 두 곳에 씁니다.
- **개인 공부 기록기**: 앉아 있는 시간만 자동으로 기록합니다. 타이머 앱처럼 직접 누를 필요가 없고, 자리를 비운 시간은 빠집니다. PC나 폰 없이 동작합니다.
- **도서관·독서실 좌석**: "짐만 두고 장시간 자리 비움"을 감지해 좌석 관리에 씁니다.

## 동작
```
카메라 96x96 흑백 ─┬─ Person Detection 모델 (TFLite Micro, int8) → 사람 있음?
                   └─ 빈 자리 기준 이미지와 픽셀 비교 (밝기 보정) → 물건 있음?
                              ▼
          상태 판정: occupied / away / empty  (4프레임 연속일 때만 확정)
                              ▼
          상태 변화 + 이전 상태 지속 시간, 장시간 자리 비움(30분) 알림
                              ▼
          Supabase seat_events  →  상태 웹 페이지 (5초마다 갱신)
```

## 필수 조건
| 조건 | 구현 |
|---|---|
| ESP32에 모델 | `model/person_detect.tflite` (MobileNet v1 0.25, Visual Wake Words로 학습, int8, 300KB) → `src/model_data.cpp`로 펌웨어에 포함, `src/detector.cpp`에서 추론 (약 310ms) |
| 로그를 DB에 | `src/uploader.cpp` → Supabase REST `seat_events` (Wi-Fi가 끊기면 큐에 보관 후 재전송). 스키마와 일별 요약 뷰 `seat_daily`는 `supabase/schema.sql` |
| CI | `.github/workflows/ci.yml` (GitHub Actions, push마다 실행): 상태 로직 유닛 테스트, 모델 검사, 펌웨어 빌드, 크기 제한, 바이너리 artifact |

CI는 실제로 버그를 잡았습니다. 첫 커밋에서 짐 감지의 밝기 보정이 평균값을 써서 큰 짐이 놓이면 화면 전체가 바뀐 것으로 잡혔고(기대 25%, 실제 100%), 유닛 테스트가 실패해 중앙값 보정으로 고쳤습니다.

## 상태 페이지
`docs/index.html` (GitHub Pages: Settings → Pages → `main` / `/docs`)
- **개인**: 현재 상태와 실시간 타이머, 오늘 요약(앉아 있던 시간, 자리 비움, 집중 유지율), 오늘 목표 진행률, 공부 잔디(12주)와 연속 달성일, 오늘 타임라인, 최근 기록
- **도서관** (`?mode=library`): 좌석 현황 요약과 실제 이용률, 열람실 좌석 지도, 오래 비운 순 자리 비움 목록
- 기기 연결 상태 (1분마다 보내는 heartbeat 기준)
- 예시 데이터로 보기: `?demo` (기기 없이 발표용)

장시간 자리 비움 알림은 짐이 보이는지와 상관없이 **사람이 떠난 지 30분**이 지나면 발생합니다. 짐이 카메라에 안 잡히는 경우(바닥의 가방 등)도 놓치지 않기 위해서이고, 알림에는 짐이 보였는지가 함께 기록됩니다.

**로컬 뷰어** `python tools/viewer.py`: ESP32 카메라 화면과 사람 점수·화면 변화·현재 상태·기기 로그를 PC에서 실시간으로 봅니다. 카메라 위치를 잡을 때 씁니다. 화면은 메모리에서만 보여주고 저장하거나 전송하지 않습니다.

Arduino IDE의 **시리얼 플로터**(115200 baud)로 사람 점수, 짐 감지 값, 기준선, 좌석 상태를 실시간 그래프로 볼 수 있습니다.
- 전체: https://ki543219.github.io/seat-monitor/
- 배지 모드 `?badge=desk-01`: 한 좌석 상태만 화면 가득 표시합니다. 화면 공유나 OBS 가상 카메라용입니다.

## 구성
```
src/            펌웨어 (camera, detector, uploader, main, config.h)
lib/seat/       상태 판정 로직 (하드웨어 독립, PC에서 유닛 테스트)
test/           유닛 테스트 (Unity)
model/          TFLite 모델
tools/          model_to_c.py (모델→C 배열), check_model.py (CI 모델 검사), capture.py (학습 사진 수집)
supabase/       DB 스키마
docs/           상태 웹 페이지
```

## 하드웨어
Seeed XIAO ESP32S3 Sense (OV2640 카메라, 8MB PSRAM)

## 실행
1. Supabase SQL Editor에서 `supabase/schema.sql` 실행
2. `include/secrets.example.h`를 `include/secrets.h`로 복사해 Wi-Fi(2.4GHz)와 Supabase 정보 입력 (git에 올라가지 않음)
3. `pio run -e xiao -t upload` 후 `pio device monitor`
4. 부팅 시 자리가 비어 있는 상태로 기준 이미지를 찍음. 다시 찍으려면 BOOT 버튼 1초

튜닝 값(사람 판정 기준, 짐 감지 기준, 장시간 자리 비움 시간)은 `src/config.h`에 있습니다.

## 테스트
```
pio test -e native                                                          # 상태 판정 로직
python tools/check_model.py model/person_detect.tflite src/model_data.cpp   # 모델 검사
```

## 개발 중: 손 제스처 공부 타이머
직접 학습한 두 번째 모델로 **손바닥 = 공부 시작, 주먹 = 종료**를 인식하고, 자리를 비우면 자동으로 일시정지해 순수 공부 시간을 기록합니다.
- 데이터 수집: `python tools/capture.py palm 80` (ESP 카메라로 직접 촬영, `data/gesture/<label>/`)

## 한계와 개선 방향
| 한계 | 개선 방향 |
|---|---|
| 사람 유무만 알고 누구인지는 모름 | 얼굴 인식 모델 추가 (ESP32-S3에서 동작 가능) |
| 앉아서 폰을 보는 시간도 공부로 기록됨 | 손에 든 폰 감지 모델 |
| 조명이 크게 바뀌면 짐 감지가 흔들릴 수 있음 | 빈 자리일 때 기준 이미지 자동 갱신 |
| 온라인 미팅은 참석자마다 기기가 필요하고, 미팅 앱과 연동되지 않음 | 웹캠 소프트웨어 버전, Zoom/Teams 상태 API 연동 |
