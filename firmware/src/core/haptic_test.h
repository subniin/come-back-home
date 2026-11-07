// 햅틱 인지 실험 모드 (평가의 중심: "촉각만으로 신호가 구분되는가")
//  - 자극을 랜덤 순서로 제시 → 응답(R k) 입력 → 정답 여부·반응시간을 CSV로 출력
//  - 끝나면 혼동 행렬(confusion matrix), 인지율, 평균 반응시간 출력
#pragma once
#include <stdint.h>
#include "config.h"
#include "haptic.h"

namespace chh {

typedef void (*LogFn)(const char* line);

class HapticTest {
 public:
  enum class Kind : uint8_t {
    DIR8,    // 8방향 전부 (응답: 0~7)
    DIR4,    // 앞/오/뒤/왼만 (응답: 0,2,4,6) - 8방향과 비교용
    SIGNAL,  // 상황 신호 6종 (응답: 0=CONFIRM 1=BLOCKED 2=MODE_AUTO 3=ARRIVED 4=STOP 5=NO_ROUTE)
  };

  bool start(Kind k, int repsPerStimulus, uint32_t seed, uint32_t now, LogFn log);
  void stop(LogFn log);
  bool active() const { return active_; }
  void tick(uint32_t now, HapticBelt& belt, LogFn log);
  void respond(int code, uint32_t now, HapticBelt& belt, LogFn log);
  void printSummary(LogFn log) const;

  static Signal signalForCode(int code);

 private:
  uint32_t rand32();
  void finishTrial(int response, uint32_t now, HapticBelt& belt, LogFn log);

  Kind kind_ = Kind::DIR8;
  uint8_t trials_[cfg::TEST_MAX_TRIALS];
  int nTrials_ = 0, idx_ = 0;
  bool active_ = false, presenting_ = false;
  uint32_t onset_ = 0, nextAt_ = 0, rng_ = 1;
  uint16_t conf_[8][9] = {};  // [제시][응답], 마지막 열 = 무응답
  uint32_t rtSum_ = 0;
  int rtN_ = 0;
};

}  // namespace chh
