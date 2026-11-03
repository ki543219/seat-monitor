# Seat Monitor — ESP32-S3 자리 비움 감지

도서관·독서실 좌석에서 **사용 중 / 짐만 두고 자리 비움 / 빈 자리**를 ESP32-S3 카메라가 기기 안에서 판단하고, 상태 변화를 Supabase DB에 기록합니다. 영상은 기기 밖으로 나가지 않고 판정 결과만 전송됩니다.

## 동작
```
카메라 96x96 흑백 ─┬─ Person Detection 모델 (TFLite Micro, int8) → 사람 있음?
                   └─ 빈 자리 기준 이미지와 픽셀 비교            → 물건 있음?
                              ▼
          상태 판정: occupied / away / empty  (4프레임 연속일 때만 확정)
                              ▼
          상태 변화·장시간 자리 비움(30분) 이벤트 → Supabase seat_events
```

| 필수 조건 | 구현 |
|---|---|
| ESP32에 모델 | `model/person_detect.tflite` (MobileNet v1 0.25, 300KB) → `src/model_data.cpp`로 펌웨어에 포함, `src/detector.cpp`에서 추론 (약 310ms) |
| 로그를 DB에 | `src/uploader.cpp` → Supabase REST `seat_events` (오프라인 시 큐에 보관 후 재전송), 스키마 `supabase/schema.sql` |
| CI | `.github/workflows/ci.yml`: 상태 로직 유닛 테스트, 모델 검사, 펌웨어 빌드·크기 제한, 바이너리 artifact |

## 하드웨어
Seeed XIAO ESP32S3 Sense (OV2640 카메라)

## 실행
1. Supabase SQL Editor에서 `supabase/schema.sql` 실행
2. `include/secrets.example.h`를 `include/secrets.h`로 복사해 Wi-Fi·Supabase 정보 입력
3. `pio run -e xiao -t upload` 후 `pio device monitor`
4. 부팅 시 자리가 비어 있는 상태로 기준 이미지를 찍음. 다시 찍으려면 BOOT 버튼 1초

튜닝 값(임계값, 자리 비움 알림 시간)은 `src/config.h`에 있습니다.

## 테스트
```
pio test -e native                                               # 상태 판정 로직
python tools/check_model.py model/person_detect.tflite src/model_data.cpp   # 모델 검사
```
