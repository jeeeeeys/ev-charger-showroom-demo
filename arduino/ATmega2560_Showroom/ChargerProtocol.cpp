#include "ChargerProtocol.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace charger {
namespace {

bool copyChecked(char* destination, size_t destinationSize, const char* source) {
  if (destination == nullptr || destinationSize == 0 || source == nullptr) {
    return false;
  }

  const size_t sourceLength = strlen(source);
  if (sourceLength == 0 || sourceLength >= destinationSize) {
    return false;
  }

  memcpy(destination, source, sourceLength + 1);
  return true;
}

bool parseUint32(const char* text, uint32_t& value) {
  if (text == nullptr || text[0] == '\0' || text[0] == '-') {
    return false;
  }

  char* end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || *end != '\0') {
    return false;
  }

#if ULONG_MAX > UINT32_MAX
  if (parsed > UINT32_MAX) {
    return false;
  }
#endif

  value = static_cast<uint32_t>(parsed);
  return true;
}

bool allDigits(const char* text, size_t start, size_t count) {
  for (size_t index = start; index < start + count; ++index) {
    if (!isdigit(static_cast<unsigned char>(text[index]))) {
      return false;
    }
  }
  return true;
}

uint8_t twoDigitValue(const char* text, size_t start) {
  return static_cast<uint8_t>((text[start] - '0') * 10 + (text[start + 1] - '0'));
}

}  // namespace

const char* targetStateToString(TargetState state) {
  switch (state) {
    case TargetState::STANDBY:
      return "STANDBY";
    case TargetState::AVAILABLE:
      return "AVAILABLE";
    default:
      return "UNKNOWN";
  }
}

const char* displayStateToString(DisplayState state) {
  switch (state) {
    case DisplayState::STANDBY:
      return "UI_STANDBY";
    case DisplayState::READY_LIMITED:
      return "UI_READY_LIMITED";
    case DisplayState::READY_FULL:
      return "UI_READY_FULL";
    default:
      return "UI_STANDBY";
  }
}

const char* validationErrorToString(ValidationError error) {
  switch (error) {
    case ValidationError::NONE:
      return "NONE";
    case ValidationError::INVALID_FORMAT:
      return "INVALID_FORMAT";
    case ValidationError::INVALID_PREFIX:
      return "INVALID_PREFIX";
    case ValidationError::INVALID_CHARGER_ID:
      return "INVALID_CHARGER_ID";
    case ValidationError::INVALID_COMMAND_ID:
      return "INVALID_COMMAND_ID";
    case ValidationError::INVALID_TARGET_STATE:
      return "INVALID_TARGET_STATE";
    case ValidationError::INVALID_POWER_LIMIT:
      return "INVALID_POWER_LIMIT";
    case ValidationError::INVALID_STATE_POWER_COMBINATION:
      return "INVALID_STATE_POWER_COMBINATION";
    case ValidationError::INVALID_TIMESTAMP:
      return "INVALID_TIMESTAMP";
    default:
      return "UNKNOWN_ERROR";
  }
}

bool parseTargetState(const char* text, TargetState& state) {
  if (text == nullptr) {
    state = TargetState::UNKNOWN;
    return false;
  }

  if (strcmp(text, "STANDBY") == 0) {
    state = TargetState::STANDBY;
    return true;
  }

  if (strcmp(text, "AVAILABLE") == 0) {
    state = TargetState::AVAILABLE;
    return true;
  }

  state = TargetState::UNKNOWN;
  return false;
}

bool isIso8601UtcTimestamp(const char* text) {
  // The MVP contract intentionally accepts exactly YYYY-MM-DDTHH:MM:SSZ.
  if (text == nullptr || strlen(text) != 20) {
    return false;
  }

  if (text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
      text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
    return false;
  }

  if (!allDigits(text, 0, 4) || !allDigits(text, 5, 2) ||
      !allDigits(text, 8, 2) || !allDigits(text, 11, 2) ||
      !allDigits(text, 14, 2) || !allDigits(text, 17, 2)) {
    return false;
  }

  const uint8_t month = twoDigitValue(text, 5);
  const uint8_t day = twoDigitValue(text, 8);
  const uint8_t hour = twoDigitValue(text, 11);
  const uint8_t minute = twoDigitValue(text, 14);
  const uint8_t second = twoDigitValue(text, 17);

  return month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour <= 23 &&
         minute <= 59 && second <= 59;
}

ValidationError validateCommand(const Command& command,
                                const char* expectedChargerId,
                                uint32_t limitedPowerW,
                                uint32_t fullPowerW) {
  if (expectedChargerId == nullptr || expectedChargerId[0] == '\0' ||
      strcmp(command.chargerId, expectedChargerId) != 0) {
    return ValidationError::INVALID_CHARGER_ID;
  }

  if (command.commandId == 0) {
    return ValidationError::INVALID_COMMAND_ID;
  }

  if (command.targetState != TargetState::STANDBY &&
      command.targetState != TargetState::AVAILABLE) {
    return ValidationError::INVALID_TARGET_STATE;
  }

  if (!isIso8601UtcTimestamp(command.timestamp)) {
    return ValidationError::INVALID_TIMESTAMP;
  }

  if (command.targetState == TargetState::STANDBY) {
    return command.powerLimitW == 0
               ? ValidationError::NONE
               : ValidationError::INVALID_STATE_POWER_COMBINATION;
  }

  if (command.powerLimitW != limitedPowerW && command.powerLimitW != fullPowerW) {
    return ValidationError::INVALID_POWER_LIMIT;
  }

  return ValidationError::NONE;
}

ValidationError parseUartCommandLine(const char* line,
                                     const char* expectedChargerId,
                                     uint32_t limitedPowerW,
                                     uint32_t fullPowerW,
                                     Command& command) {
  if (line == nullptr) {
    return ValidationError::INVALID_FORMAT;
  }

  const size_t lineLength = strcspn(line, "\r\n");
  if (lineLength == 0 || lineLength >= MAX_UART_LINE_LENGTH) {
    return ValidationError::INVALID_FORMAT;
  }

  char working[MAX_UART_LINE_LENGTH];
  memcpy(working, line, lineLength);
  working[lineLength] = '\0';

  char* tokens[6] = {nullptr};
  size_t tokenCount = 1;
  tokens[0] = working;

  for (size_t index = 0; index < lineLength; ++index) {
    if (working[index] != '|') {
      continue;
    }

    working[index] = '\0';
    if (tokenCount >= 6) {
      return ValidationError::INVALID_FORMAT;
    }
    tokens[tokenCount++] = &working[index + 1];
  }

  if (tokenCount != 6) {
    return ValidationError::INVALID_FORMAT;
  }

  if (strcmp(tokens[0], "CMD") != 0) {
    return ValidationError::INVALID_PREFIX;
  }

  Command parsed = {};
  if (!copyChecked(parsed.chargerId, sizeof(parsed.chargerId), tokens[1])) {
    return ValidationError::INVALID_CHARGER_ID;
  }

  if (!parseUint32(tokens[2], parsed.commandId) || parsed.commandId == 0) {
    return ValidationError::INVALID_COMMAND_ID;
  }

  if (!parseTargetState(tokens[3], parsed.targetState)) {
    return ValidationError::INVALID_TARGET_STATE;
  }

  if (!parseUint32(tokens[4], parsed.powerLimitW)) {
    return ValidationError::INVALID_POWER_LIMIT;
  }

  if (!copyChecked(parsed.timestamp, sizeof(parsed.timestamp), tokens[5])) {
    return ValidationError::INVALID_TIMESTAMP;
  }

  const ValidationError validation =
      validateCommand(parsed, expectedChargerId, limitedPowerW, fullPowerW);
  if (validation != ValidationError::NONE) {
    return validation;
  }

  command = parsed;
  return ValidationError::NONE;
}

DisplayState displayStateFor(const Command& command,
                             uint32_t limitedPowerW,
                             uint32_t fullPowerW) {
  if (command.targetState == TargetState::STANDBY) {
    return DisplayState::STANDBY;
  }

  if (command.targetState == TargetState::AVAILABLE &&
      command.powerLimitW == limitedPowerW) {
    return DisplayState::READY_LIMITED;
  }

  if (command.targetState == TargetState::AVAILABLE &&
      command.powerLimitW == fullPowerW) {
    return DisplayState::READY_FULL;
  }

  return DisplayState::STANDBY;
}

bool commandsHaveSamePayload(const Command& left, const Command& right) {
  return strcmp(left.chargerId, right.chargerId) == 0 &&
         left.commandId == right.commandId &&
         left.targetState == right.targetState &&
         left.powerLimitW == right.powerLimitW &&
         strcmp(left.timestamp, right.timestamp) == 0;
}

}  // namespace charger
