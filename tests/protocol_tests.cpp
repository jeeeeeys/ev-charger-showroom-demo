#include <assert.h>
#include <iostream>

#include "../arduino/ATmega2560_Showroom/ChargerProtocol.h"

namespace {

constexpr char CHARGER_ID[] = "EVSE-01";
constexpr uint32_t LIMITED_W = 5000;
constexpr uint32_t FULL_W = 7000;

void testValidLimitedCommand() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|3|AVAILABLE|5000|2026-08-19T00:35:00Z", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result == charger::ValidationError::NONE);
  assert(command.commandId == 3);
  assert(command.targetState == charger::TargetState::AVAILABLE);
  assert(command.powerLimitW == 5000);
  assert(charger::displayStateFor(command, LIMITED_W, FULL_W) ==
         charger::DisplayState::READY_LIMITED);
}
void testValidFullCommand() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|4|AVAILABLE|7000|2026-08-19T00:36:00Z", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result == charger::ValidationError::NONE);
  assert(charger::displayStateFor(command, LIMITED_W, FULL_W) ==
         charger::DisplayState::READY_FULL);
}

void testValidStandbyCommand() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|5|STANDBY|0|2026-08-19T00:37:00Z", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result == charger::ValidationError::NONE);
  assert(charger::displayStateFor(command, LIMITED_W, FULL_W) ==
         charger::DisplayState::STANDBY);
}

void testRejectsStandbyWithPower() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|6|STANDBY|5000|2026-08-19T00:38:00Z", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result ==
         charger::ValidationError::INVALID_STATE_POWER_COMBINATION);
}

void testRejectsUnsupportedPower() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|7|AVAILABLE|6000|2026-08-19T00:39:00Z", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result == charger::ValidationError::INVALID_POWER_LIMIT);
}

void testRejectsWrongCharger() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-02|8|AVAILABLE|5000|2026-08-19T00:40:00Z", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result == charger::ValidationError::INVALID_CHARGER_ID);
}

void testRejectsInvalidCommandId() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|0|AVAILABLE|5000|2026-08-19T00:41:00Z", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result == charger::ValidationError::INVALID_COMMAND_ID);
}

void testRejectsInvalidTimestamp() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|9|AVAILABLE|5000|2026-08-19 00:42:00", CHARGER_ID,
      LIMITED_W, FULL_W, command);

  assert(result == charger::ValidationError::INVALID_TIMESTAMP);
}

void testRejectsMalformedLine() {
  charger::Command command = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      "CMD|EVSE-01|10|AVAILABLE|5000", CHARGER_ID, LIMITED_W, FULL_W,
      command);

  assert(result == charger::ValidationError::INVALID_FORMAT);
}

}  // namespace

int main() {
  testValidLimitedCommand();
  testValidFullCommand();
  testValidStandbyCommand();
  testRejectsStandbyWithPower();
  testRejectsUnsupportedPower();
  testRejectsWrongCharger();
  testRejectsInvalidCommandId();
  testRejectsInvalidTimestamp();
  testRejectsMalformedLine();

  std::cout << "All ChargerProtocol tests passed.\n";
  return 0;
}
