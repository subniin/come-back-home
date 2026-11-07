// ============================================================================
//  Come Back Home - 설정값 (실험하면서 여기 숫자만 바꾸면 됨)
// ============================================================================
#pragma once
#include <stdint.h>

namespace chh {
namespace cfg {

// ---------------- 햅틱 (허리 8개 모터) ----------------
constexpr int      MOTOR_COUNT      = 8;
constexpr uint16_t DIR_PULSE_MS     = 250;   // 방향 신호 1회 진동 길이
constexpr uint16_t DIR_PERIOD_MS    = 1200;  // 방향 신호 반복 주기
constexpr float    SECTOR_HYST_DEG  = 7.0f;  // 방향 경계에서 모터가 깜빡이며 바뀌는 것 방지

// ---------------- PDR / 경로 기록 ----------------
constexpr float STEP_LENGTH_M       = 0.70f; // 보폭 (피험자별로 보정 권장)
constexpr float TURN_DEG            = 35.0f; // 이 각도 이상 꺾이면 방향전환으로 판단
constexpr int   TURN_CONFIRM_STEPS  = 2;     // 연속 N걸음 꺾여 있어야 노드 생성 (흔들림 무시)
constexpr float SEG_HEADING_ALPHA   = 0.2f;  // 직진 구간 기준 헤딩 갱신 비율
constexpr float MAX_SEGMENT_M       = 8.0f;  // 직선이라도 이 거리마다 노드 생성
constexpr float MERGE_RADIUS_M      = 1.5f;  // 기존 노드와 이 거리 이내면 같은 지점으로 병합(루프 인식)

// 걸음 검출 (가속도 크기, g 단위) - 센서 담당과 함께 튜닝
constexpr float    STEP_HI_G        = 1.12f;
constexpr float    STEP_LO_G        = 1.00f;
constexpr uint32_t STEP_MIN_MS      = 300;

// ---------------- 복귀 안내 ----------------
constexpr float    ARRIVE_RADIUS_M     = 1.0f;  // 웨이포인트 도달 판정 반경 (실내)
constexpr float    OUTDOOR_ARRIVE_M    = 5.0f;  // 실외 안전지점 도달 판정 반경
constexpr uint32_t GPS_STALE_MS        = 3000;  // GPS가 이 시간 이상 끊기면 PDR 경로로 대체
constexpr float    MAG_DECLINATION_DEG = -8.5f; // 자북→진북 보정 (한국은 서편각 약 8~9°, 실측 확인 필요)
constexpr uint32_t GUIDE_UPDATE_MS     = 100;   // 안내 방향 재계산 주기

// ---------------- 경로 차단 (ToF/초음파, 선택) ----------------
constexpr float    BLOCK_DIST_M      = 1.2f;
constexpr uint32_t BLOCK_CONFIRM_MS  = 3000;
constexpr uint32_t BLOCK_COOLDOWN_MS = 5000;

// ---------------- 통신 (휴대폰 BLE) ----------------
constexpr uint32_t LINK_LOST_MS              = 3000; // 연결 끊김 확정 시간
constexpr bool     AUTO_RETURN_ON_LINK_LOSS  = true; // 연결 끊기면 자동으로 복귀 안내 시작

// ---------------- 메모리 ----------------
constexpr int MAX_NODES = 128;
constexpr int MAX_EDGES = 192;

// ---------------- 햅틱 인지 실험 모드 ----------------
constexpr uint32_t TEST_TIMEOUT_MS = 5000;  // 응답 없으면 '무응답' 처리
constexpr uint32_t TEST_GAP_MIN_MS = 1500;  // 다음 자극까지 랜덤 간격 (예측 방지)
constexpr uint32_t TEST_GAP_MAX_MS = 3000;
constexpr int      TEST_MAX_TRIALS = 160;

}  // namespace cfg
}  // namespace chh
