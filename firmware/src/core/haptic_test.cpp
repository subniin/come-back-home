#include "haptic_test.h"
#include <stdio.h>
#include <string.h>

namespace chh {

static const char* kindName(HapticTest::Kind k) {
  switch (k) {
    case HapticTest::Kind::DIR8:   return "DIR8";
    case HapticTest::Kind::DIR4:   return "DIR4";
    case HapticTest::Kind::SIGNAL: return "SIGNAL";
  }
  return "?";
}

Signal HapticTest::signalForCode(int c) {
  static const Signal tbl[6] = {Signal::CONFIRM, Signal::BLOCKED, Signal::MODE_AUTO,
                                Signal::ARRIVED, Signal::STOP,    Signal::NO_ROUTE};
  return (c >= 0 && c < 6) ? tbl[c] : Signal::NONE;
}

uint32_t HapticTest::rand32() {  // xorshift32
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

bool HapticTest::start(Kind k, int reps, uint32_t seed, uint32_t now, LogFn log) {
  uint8_t stim[8];
  int nStim = 0;
  if (k == Kind::DIR8)        for (int i = 0; i < 8; i++) stim[nStim++] = (uint8_t)i;
  else if (k == Kind::DIR4)   for (int i = 0; i < 8; i += 2) stim[nStim++] = (uint8_t)i;
  else                        for (int i = 0; i < 6; i++) stim[nStim++] = (uint8_t)i;

  if (reps < 1) reps = 1;
  if (reps * nStim > cfg::TEST_MAX_TRIALS) reps = cfg::TEST_MAX_TRIALS / nStim;

  kind_ = k;
  rng_ = seed ? seed : 0x12345678u;
  nTrials_ = 0;
  for (int r = 0; r < reps; r++)
    for (int s = 0; s < nStim; s++) trials_[nTrials_++] = stim[s];
  for (int i = nTrials_ - 1; i > 0; i--) {  // Fisher-Yates 셔플
    int j = (int)(rand32() % (uint32_t)(i + 1));
    uint8_t t = trials_[i]; trials_[i] = trials_[j]; trials_[j] = t;
  }
  memset(conf_, 0, sizeof(conf_));
  rtSum_ = 0; rtN_ = 0;
  idx_ = 0;
  active_ = true;
  presenting_ = false;
  nextAt_ = now + 2000;  // 2초 뒤 첫 자극

  char buf[96];
  snprintf(buf, sizeof(buf), "TEST,START,%s,trials=%d", kindName(k), nTrials_);
  log(buf);
  log("CSV,trial,kind,stimulus,response,rt_ms,correct");
  return true;
}

void HapticTest::stop(LogFn log) {
  if (!active_) return;
  active_ = false;
  log("TEST,STOPPED");
  printSummary(log);
}

void HapticTest::tick(uint32_t now, HapticBelt& belt, LogFn log) {
  if (!active_) return;
  if (presenting_) {
    if (now - onset_ >= cfg::TEST_TIMEOUT_MS) finishTrial(-1, now, belt, log);
    return;
  }
  if ((int32_t)(now - nextAt_) < 0) return;
  if (idx_ >= nTrials_) {
    active_ = false;
    log("TEST,DONE");
    printSummary(log);
    return;
  }
  int s = trials_[idx_];
  if (kind_ == Kind::SIGNAL) belt.playOnce(signalForCode(s));
  else                       belt.setBackground(Signal::DIRECTION, (uint8_t)s);
  onset_ = now;
  presenting_ = true;
}

void HapticTest::respond(int code, uint32_t now, HapticBelt& belt, LogFn log) {
  if (!active_ || !presenting_) return;
  finishTrial(code, now, belt, log);
}

void HapticTest::finishTrial(int resp, uint32_t now, HapticBelt& belt, LogFn log) {
  belt.allOff();
  int s = trials_[idx_];
  uint32_t rt = now - onset_;
  bool correct = (resp == s);
  int col = (resp >= 0 && resp < 8) ? resp : 8;
  conf_[s][col]++;
  if (correct) { rtSum_ += rt; rtN_++; }

  char buf[96];
  snprintf(buf, sizeof(buf), "CSV,%d,%s,%d,%d,%lu,%d", idx_ + 1, kindName(kind_), s, resp,
           (unsigned long)(resp < 0 ? 0 : rt), correct ? 1 : 0);
  log(buf);

  idx_++;
  presenting_ = false;
  uint32_t span = cfg::TEST_GAP_MAX_MS - cfg::TEST_GAP_MIN_MS;
  nextAt_ = now + cfg::TEST_GAP_MIN_MS + (span ? rand32() % span : 0);
}

void HapticTest::printSummary(LogFn log) const {
  int nStim = (kind_ == Kind::SIGNAL) ? 6 : 8;
  int total = 0, correct = 0;
  char buf[128];
  log("CONFUSION (row=제시, col=응답, 마지막=무응답)");
  int n = snprintf(buf, sizeof(buf), "   ");
  for (int c = 0; c < nStim; c++) n += snprintf(buf + n, sizeof(buf) - n, "%4d", c);
  snprintf(buf + n, sizeof(buf) - n, "  miss");
  log(buf);
  for (int r = 0; r < nStim; r++) {
    int rowSum = 0;
    for (int c = 0; c < 9; c++) rowSum += conf_[r][c];
    if (rowSum == 0) continue;
    n = snprintf(buf, sizeof(buf), "%2d ", r);
    for (int c = 0; c < nStim; c++) n += snprintf(buf + n, sizeof(buf) - n, "%4u", conf_[r][c]);
    n += snprintf(buf + n, sizeof(buf) - n, "%6u  acc=%.0f%%", conf_[r][8],
                  100.0 * conf_[r][r] / rowSum);
    log(buf);
    total += rowSum;
    correct += conf_[r][r];
  }
  snprintf(buf, sizeof(buf), "SUMMARY,recognition=%.1f%%,n=%d,meanRT_correct=%.0fms",
           total ? 100.0 * correct / total : 0.0, total, rtN_ ? (double)rtSum_ / rtN_ : 0.0);
  log(buf);
}

}  // namespace chh
