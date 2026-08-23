#pragma once
#include <string.h>

enum class TradeState { Idle, Multibooting, WaitForGba, Injecting };
enum class TradeResult { None, MultibootOk, MultibootFail, InjectOk, InjectFail };

// Only idle starts a multiboot and WaitForGba advances to injection. While a
// blocking Pico operation runs, additional taps must not queue UART commands.
static inline bool trade_action_allowed(TradeState state) {
    return state == TradeState::Idle || state == TradeState::WaitForGba;
}

static inline bool interaction_locked(TradeState state) {
    return state == TradeState::Multibooting || state == TradeState::Injecting;
}

static inline TradeResult parse_trade_result(const char *line) {
    if (!line) return TradeResult::None;
    if (strncmp(line, "MB_RESULT ok", 12) == 0) return TradeResult::MultibootOk;
    if (strncmp(line, "MB_RESULT fail", 14) == 0) return TradeResult::MultibootFail;
    if (strncmp(line, "INJECT_RESULT ok", 16) == 0) return TradeResult::InjectOk;
    if (strncmp(line, "INJECT_RESULT fail", 18) == 0 ||
        strncmp(line, "INJECT_RESULT cancelled", 23) == 0 ||
        strncmp(line, "INJECT_RESULT declined", 22) == 0)
        return TradeResult::InjectFail;
    return TradeResult::None;
}

static inline TradeState state_after_result(TradeState state, TradeResult result) {
    if (state == TradeState::Multibooting) {
        if (result == TradeResult::MultibootOk) return TradeState::WaitForGba;
        if (result == TradeResult::MultibootFail) return TradeState::Idle;
    } else if (state == TradeState::Injecting &&
               (result == TradeResult::InjectOk || result == TradeResult::InjectFail)) {
        return TradeState::Idle;
    }
    return state;
}
