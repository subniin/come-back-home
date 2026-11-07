// 전체 상태 관리 (하드웨어 독립 → PC에서도 시뮬레이션 가능)
//
//  [제어 모드]  REMOTE : 휴대폰(지휘부)에서 방향을 직접 전송 → 그대로 진동 (구조대원 위치를 아는 경우)
//               AUTO   : 장치가 스스로 복귀 방향 계산 (연결 끊김 시 자동 전환)
//  [환경]       INDOOR : PDR 경로 그래프 / 사전 지도로 안내
//               OUTDOOR: GPS 방위각으로 안전지점 안내 (GPS 끊기면 PDR 그래프로 대체)
//               → 멘토링 반영: 실내/실외는 버튼·명령으로 '수동' 선택
//  [단계]       IDLE → RECORDING(진입, 경로 기록) → RETURNING(복귀 안내) → ARRIVED / NO_ROUTE
//
//  경로 기록은 어떤 모드에서든 START 이후 항상 백그라운드로 진행됨
//  (원격 조종 중 연결이 끊겨도 그때까지의 경로로 바로 복귀 안내 가능)
#pragma once
#include <stdint.h>
#include "config.h"
#include "haptic.h"
#include "haptic_test.h"
#include "path_graph.h"
#include "return_guide.h"

namespace chh {

enum class CtrlMode : uint8_t { AUTO, REMOTE };
enum class EnvMode  : uint8_t { INDOOR, OUTDOOR };
enum class Phase    : uint8_t { IDLE, RECORDING, RETURNING, ARRIVED, NO_ROUTE };

const char* phaseName(Phase p);

class Controller {
 public:
  void begin(MotorWriteFn motor, LogFn log);
  void tick(uint32_t now);

  // ---- 센서 입력 (센서 담당 코드에서 호출) ----
  void onStep(float headingDeg, uint32_t now);          // 걸음 1회 + 그때 헤딩
  void onHeading(float headingDeg);                     // 헤딩 갱신 (수시로)
  void onGps(double lat, double lon, bool fix, uint32_t now);
  void onObstacle(float distM, uint32_t now);           // 전방 거리 (ToF/초음파, 선택)

  // ---- 통신/버튼 ----
  void onLink(bool connected, uint32_t now);            // BLE 연결 상태
  void onCommand(const char* line, uint32_t now);       // 시리얼/BLE 텍스트 명령
  void onButtonShort(uint32_t now);                     // IDLE이면 START, 기록중이면 RETURN, 복귀중이면 취소
  void onButtonLong(uint32_t now);                      // 실내/실외 전환

  // ---- 상태 조회 ----
  Phase    phase() const { return phase_; }
  CtrlMode ctrlMode() const { return ctrl_; }
  EnvMode  envMode() const { return env_; }
  uint8_t  guideDir() const { return guideDir_; }       // 255 = 안내 없음
  float    x() const { return x_; }
  float    y() const { return y_; }
  float    heading() const { return heading_; }
  const PathGraph&   graph() const { return graph_; }
  const ReturnGuide& guide() const { return guide_; }
  const HapticBelt&  belt() const { return belt_; }
  bool     testActive() const { return test_.active(); }

 private:
  void startMission(uint32_t now);
  void startReturn(uint32_t now);
  void cancelReturn();
  void updateGuidance(uint32_t now);
  void handleBlocked(const char* why);
  void enterAuto(const char* why, uint32_t now);
  void setGuideDir(uint8_t d);
  void finish(Phase p);
  void dump();
  void status();
  void logf(const char* fmt, ...);

  HapticBelt   belt_;
  HapticTest   test_;
  PathGraph    graph_;
  PathRecorder rec_;
  ReturnGuide  guide_;
  LogFn        log_ = nullptr;

  CtrlMode ctrl_  = CtrlMode::AUTO;
  EnvMode  env_   = EnvMode::INDOOR;
  Phase    phase_ = Phase::IDLE;

  float x_ = 0, y_ = 0, heading_ = 0;
  uint8_t guideDir_ = 255, outSector_ = 255;
  uint32_t lastGuideMs_ = 0;
  bool track_ = false;

  // 사전 지도
  bool priorMap_ = false;
  int  mapStart_ = 0, mapExit_ = 0;

  // GPS
  bool gpsFix_ = false, baseSet_ = false;
  double lat_ = 0, lon_ = 0, baseLat_ = 0, baseLon_ = 0;
  uint32_t gpsMs_ = 0;
  bool usingGps_ = false;

  // 장애물
  uint32_t obsSince_ = 0, lastBlockMs_ = 0;
  bool obsNear_ = false;

  // 링크
  bool linkUp_ = false, linkEverUp_ = false, linkLossHandled_ = true;
  uint32_t linkLostMs_ = 0;
};

}  // namespace chh
