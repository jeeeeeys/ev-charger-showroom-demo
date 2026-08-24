#pragma once

#include <stddef.h>
#include <stdint.h>

namespace charger {

static constexpr size_t MAX_CHARGER_ID_LENGTH = 24;
static constexpr size_t MAX_TIMESTAMP_LENGTH = 32;
static constexpr size_t MAX_UART_LINE_LENGTH = 128;

enum class TargetState : uint8_t {
  UNKNOWN = 0,
  STANDBY,
  AVAILABLE,
};

enum class DisplayState : uint8_t {
  STANDBY = 0,
  READY_LIMITED,
  READY_FULL,
};

enum class ValidationError : uint8_t {
  NONE = 0,
  INVALID_FORMAT,
  INVALID_PREFIX,
  INVALID_CHARGER_ID,
  INVALID_COMMAND_ID,
  INVALID_TARGET_STATE,
  INVALID_POWER_LIMIT,
  INVALID_STATE_POWER_COMBINATION,
  INVALID_TIMESTAMP,
};

struct Command {
  char chargerId[MAX_CHARGER_ID_LENGTH + 1];
  uint32_t commandId;
  TargetState targetState;
  uint32_t powerLimitW;
  char timestamp[MAX_TIMESTAMP_LENGTH + 1];
};

const char* targetStateToString(TargetState state);
const char* displayStateToString(DisplayState state);
const char* validationErrorToString(ValidationError error);

bool parseTargetState(const char* text, TargetState& state);
bool isIso8601UtcTimestamp(const char* text);

ValidationError validateCommand(const Command& command,
                                const char* expectedChargerId,
                                uint32_t limitedPowerW,
                                uint32_t fullPowerW);

ValidationError parseUartCommandLine(const char* line,
                                     const char* expectedChargerId,
                                     uint32_t limitedPowerW,
                                     uint32_t fullPowerW,
                                     Command& command);

DisplayState displayStateFor(const Command& command,
                             uint32_t limitedPowerW,
                             uint32_t fullPowerW);

bool commandsHaveSamePayload(const Command& left, const Command& right);

}  // namespace charger
