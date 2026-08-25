#include <Arduino.h>

#include "ChargerProtocol.h"
#include "ProjectConfig.h"

#include <string.h>

namespace {

// Confirmed charger PCB connection:
//   ATmega2560 package pin 45 = PD2/RXD1/INT2 = Arduino Mega D19/RX1
//   ATmega2560 package pin 46 = PD3/TXD1/INT3 = Arduino Mega D18/TX1
HardwareSerial& espLink = Serial1;

char receiveBuffer[charger::MAX_UART_LINE_LENGTH] = {};
size_t receiveLength = 0;

charger::Command lastCommand = {};
bool hasLastCommand = false;
bool communicationTimedOut = false;
uint32_t lastValidCommandAtMs = 0;

charger::DisplayState displayState = charger::DisplayState::STANDBY;

void sendAck(uint32_t commandId) {
  espLink.print(F("ACK|"));
  espLink.println(commandId);
}

void sendNack(uint32_t commandId, const char* reason) {
  espLink.print(F("NACK|"));
  espLink.print(commandId);
  espLink.print('|');
  espLink.println(reason);
}

void printAppliedCommand(const charger::Command& command) {
  Serial.println(F("\nCloud command applied"));
  Serial.print(F("Charger ID: "));
  Serial.println(command.chargerId);
  Serial.print(F("Command ID: "));
  Serial.println(command.commandId);
  Serial.print(F("Target state: "));
  Serial.println(charger::targetStateToString(command.targetState));
  Serial.print(F("Available power: "));
  Serial.print(command.powerLimitW);
  Serial.println(F(" W"));
  Serial.print(F("Timestamp: "));
  Serial.println(command.timestamp);
  Serial.print(F("Display state: "));
  Serial.println(charger::displayStateToString(displayState));
}

void applyCommand(const charger::Command& command) {
  displayState = charger::displayStateFor(
      command, config::LIMITED_POWER_W, config::FULL_POWER_W);
  printAppliedCommand(command);
}

void handleStatus(char* line) {
  // All recognized messages have at most three separators. Work directly on
  // the bounded receive buffer rather than allocating Arduino String objects.
  char* category = line + 7;  // Skip "STATUS|".
  char* separator = strchr(category, '|');
  if (separator == nullptr) return;
  *separator = '\0';
  char* state = separator + 1;
  char* valueText = strchr(state, '|');
  if (valueText != nullptr) {
    *valueText++ = '\0';
  }
  const bool hasValue = valueText != nullptr && *valueText != '\0';

  if (strcmp(category, "API") == 0) {
    if (strcmp(state, "REQUESTING") == 0 && hasValue) {
      Serial.print(F("[ESP] API request #"));
      Serial.print(valueText);
      Serial.println(F(" started"));
    } else if (strcmp(state, "HTTP_OK") == 0 && hasValue) {
      Serial.print(F("[ESP] HTTP response: "));
      Serial.println(valueText);
    } else if (strcmp(state, "ACCESS_CONFIRMED") == 0) {
      Serial.println(F("[ESP] API access confirmed"));
    } else if (strcmp(state, "AUTH_ERROR") == 0 && hasValue) {
      Serial.print(F("[ESP] API authentication failed: HTTP "));
      Serial.println(valueText);
    } else if (strcmp(state, "HTTP_ERROR") == 0 && hasValue) {
      Serial.print(F("[ESP] API HTTP error: "));
      Serial.println(valueText);
    } else if (strcmp(state, "CONNECTION_FAILED") == 0 && hasValue) {
      Serial.print(F("[ESP] API connection failed: transport error "));
      Serial.println(valueText);
    } else if (strcmp(state, "INVALID_JSON") == 0) {
      Serial.println(F("[ESP] API response contains invalid JSON"));
    } else if (strcmp(state, "INVALID_COMMAND") == 0) {
      Serial.println(F("[ESP] API response contains an invalid command"));
    }
  } else if (strcmp(category, "UART") == 0 && hasValue) {
    if (strcmp(state, "ACK_RECEIVED") == 0) {
      Serial.print(F("[ESP] ATmega ACK received for command "));
      Serial.println(valueText);
    } else if (strcmp(state, "NO_ACK") == 0) {
      Serial.print(F("[ESP] No ATmega ACK received for command "));
      Serial.println(valueText);
    }
  }
}

void processCommand(const char* line) {
  charger::Command received = {};
  const charger::ValidationError result = charger::parseUartCommandLine(
      line, config::CHARGER_ID, config::LIMITED_POWER_W,
      config::FULL_POWER_W, received);

  if (result != charger::ValidationError::NONE) {
    Serial.print(F("Rejected ESP message: "));
    Serial.println(charger::validationErrorToString(result));
    sendNack(0, charger::validationErrorToString(result));
    return;
  }

  if (hasLastCommand && received.commandId < lastCommand.commandId) {
    Serial.println(F("Rejected stale command ID"));
    sendNack(received.commandId, "STALE_COMMAND_ID");
    return;
  }

  if (hasLastCommand && received.commandId == lastCommand.commandId) {
    if (!charger::commandsHaveSamePayload(received, lastCommand)) {
      Serial.println(F("Rejected reused command ID with different payload"));
      sendNack(received.commandId, "COMMAND_ID_REUSED");
      return;
    }

    lastValidCommandAtMs = millis();
    if (communicationTimedOut) {
      communicationTimedOut = false;
      Serial.println(F("Cloud communication restored"));
      applyCommand(received);
    }
    sendAck(received.commandId);
    return;
  }

  lastCommand = received;
  hasLastCommand = true;
  communicationTimedOut = false;
  lastValidCommandAtMs = millis();

  applyCommand(received);
  sendAck(received.commandId);
}

void dispatchLine(char* line) {
  if (strncmp(line, "CMD|", 4) == 0) {
    processCommand(line);
  } else if (strncmp(line, "STATUS|", 7) == 0) {
    handleStatus(line);
  } else {
    processCommand(line);
  }
}

void readEspLink() {
  while (espLink.available() > 0) {
    const char incoming = static_cast<char>(espLink.read());

    if (incoming == '\n') {
      receiveBuffer[receiveLength] = '\0';
      if (receiveLength > 0) {
        dispatchLine(receiveBuffer);
      }
      receiveLength = 0;
      continue;
    }

    if (incoming == '\r') {
      continue;
    }

    if (receiveLength < sizeof(receiveBuffer) - 1) {
      receiveBuffer[receiveLength++] = incoming;
    } else {
      receiveLength = 0;
      Serial.println(F("Rejected overlength ESP message"));
      sendNack(0, "LINE_TOO_LONG");
    }
  }
}

void enforceCommunicationTimeout() {
  if (!hasLastCommand || communicationTimedOut) {
    return;
  }

  if (static_cast<uint32_t>(millis() - lastValidCommandAtMs) <
      config::COMMAND_TIMEOUT_MS) {
    return;
  }

  communicationTimedOut = true;
  displayState = charger::DisplayState::STANDBY;
  Serial.println(F("\nCloud command timeout"));
  Serial.println(F("Fail-safe state: UI_STANDBY"));
}

}  // namespace

void setup() {
  Serial.begin(config::DEBUG_BAUD);
  espLink.begin(config::UART_BAUD);

  Serial.println(F("\nEV charger showroom controller"));
  Serial.println(F("Initial state: UI_STANDBY"));
  Serial.println(F("Waiting for a valid cloud command from ESP8266..."));
}

void loop() {
  readEspLink();
  enforceCommunicationTimeout();
}
