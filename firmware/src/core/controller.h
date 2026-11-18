// 전체 상태 관리 (하드웨어 독립 → PC에서도 시뮬레이션 가능)
//
//  담당 범위: PDR 경로 기록 + 백트래킹 복귀 안내
//  [단계]  IDLE → RECORDING(진입, 경로 기록) → RETURNING(복귀 안내) → ARRIVED / NO_ROUTE
//
//  입력: 걸음(onStep)·헤딩(onHeading)·버튼(onButtonShort)·텍스트 명령(onCommand)
//  출력: HapticBelt를 통한 진동 (방향 1개 모터 / 상황 신호)
#pragma once
#include <stdint.h>
#include "config.h"
#include "haptic.h"
#include "path_graph.h"
#include "pdr_selftest.h"  // LogFn
#include "return_guide.h"

namespace chh {

enum class Phase : uint8_t { IDLE, RECORDING, RETURNING, ARRIVED, NO_ROUTE };

const char* phaseName(Phase p);

class Controller {
 public:
  void begin(MotorWriteFn motor, LogFn log);
  void tick(uint32_t now);

  // ---- 센서 입력 ----
  void onStep(float headingDeg, uint32_t now);          // 걸음 1회 + 그때 헤딩
  void onHeading(float headingDeg);                     // 헤딩 갱신 (수시로)

  // ---- 버튼 / 명령 ----
  void onCommand(const char* line, uint32_t now);       // 시리얼/BLE 텍스트 명령
  void onButtonShort(uint32_t now);                     // IDLE이면 START, 기록중이면 RETURN, 복귀중이면 취소

  // ---- 상태 조회 ----
  Phase    phase() const { return phase_; }
  uint8_t  guideDir() const { return guideDir_; }       // 255 = 안내 없음
  float    x() const { return x_; }
  float    y() const { return y_; }
  float    heading() const { return heading_; }
  const PathGraph&   graph() const { return graph_; }
  const ReturnGuide& guide() const { return guide_; }
  const HapticBelt&  belt() const { return belt_; }

 private:
  void startMission();
  void startReturn(uint32_t now);
  void cancelReturn();
  void updateGuidance(uint32_t now);
  void handleBlocked(const char* why);
  void setGuideDir(uint8_t d);
  void finish(Phase p);
  void dump();
  void status();
  void logf(const char* fmt, ...);

  HapticBelt   belt_;
  PathGraph    graph_;
  PathRecorder rec_;
  ReturnGuide  guide_;
  LogFn        log_ = nullptr;

  Phase phase_ = Phase::IDLE;

  float x_ = 0, y_ = 0, heading_ = 0;
  uint8_t guideDir_ = 255;
  uint32_t lastGuideMs_ = 0;
  bool track_ = false;
};

}  // namespace chh
