#include <Arduino.h>
#include <EasyNextionLibrary.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>
#include <SPI.h>

#include "ChargerProtocol.h"
#include "ProjectConfig.h"

#include <stdio.h>
#include <string.h>

namespace {

HardwareSerial& espLink = Serial1;
// Preserve the original working Nextion pin assignment and order.
// SoftwareSerial constructor arguments are RX, TX.
SoftwareSerial nexSerial(16, 17);
EasyNex myNex(nexSerial);

static constexpr uint8_t RFID_RST_PIN = 44;
static constexpr uint8_t RFID_SS_PIN = 46;
static constexpr uint8_t METER_CS_PIN = 48;
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// Keep all HMI identifiers here; session logic does not depend on their names.
static constexpr char PAGE_TAP_RFID[] = "page page1";
static constexpr char PAGE_ACTIVE[] = "page page2";
static constexpr char TXT_VOLTAGE[] = "txtVoltage.txt";
static constexpr char TXT_CURRENT[] = "txtCurrent.txt";
static constexpr char TXT_POWER[] = "txtPower.txt";
static constexpr char TXT_ENERGY[] = "txtEnergy.txt";
static constexpr uint16_t PAGE_SETTLE_MS = 20;
static constexpr uint32_t RFID_COOLDOWN_MS = 1500;

const uint8_t authorizedUIDs[][4] = {
    {0xB3, 0xE2, 0xE1, 0xC7}, {0xB3, 0xC7, 0xE0, 0xC8},
    {0x43, 0xD1, 0xFD, 0xE3}, {0x33, 0x5F, 0x07, 0xE4}};

char receiveBuffer[charger::MAX_UART_LINE_LENGTH] = {};
size_t receiveLength = 0;
charger::Command lastCommand = {};
bool hasLastCommand = false;
bool communicationTimedOut = false;
uint32_t lastValidCommandAtMs = 0;
bool sessionActive = false;
uint8_t sessionUid[4] = {};
uint32_t lastCardAtMs = 0;
bool cardCooldownStarted = false;
enum class Screen : uint8_t { TAP_RFID, ACTIVE };
Screen currentScreen = Screen::TAP_RFID;

void showTapPage() {
  if (currentScreen != Screen::TAP_RFID) {
    myNex.writeStr(PAGE_TAP_RFID);
    currentScreen = Screen::TAP_RFID;
  }
}

void formatFixed2(uint32_t scaledValue, char* output, size_t outputSize) {
  snprintf(output, outputSize, "%lu.%02lu",
           static_cast<unsigned long>(scaledValue / 100),
           static_cast<unsigned long>(scaledValue % 100));
}

void writeActiveValues(uint32_t powerLimitW) {
  char powerText[12] = {};
  char currentText[12] = {};
  const uint32_t powerCentiKw = powerLimitW / 10UL;
  const uint32_t currentCentiA = (powerLimitW * 100UL + 115UL) / 230UL;
  formatFixed2(powerCentiKw, powerText, sizeof(powerText));
  formatFixed2(currentCentiA, currentText, sizeof(currentText));
  myNex.writeStr(TXT_VOLTAGE, "230");
  myNex.writeStr(TXT_POWER, powerText);
  myNex.writeStr(TXT_CURRENT, currentText);
  myNex.writeStr(TXT_ENERGY, "00");
}

void showActivePage() {
  if (currentScreen != Screen::ACTIVE) {
    myNex.writeStr(PAGE_ACTIVE);
    currentScreen = Screen::ACTIVE;
    delay(PAGE_SETTLE_MS);
  }
  writeActiveValues(lastCommand.powerLimitW);
}

void cancelSession(const __FlashStringHelper* reason) {
  if (sessionActive) {
    sessionActive = false;
    memset(sessionUid, 0, sizeof(sessionUid));
    Serial.print(F("RFID session cancelled: "));
    Serial.println(reason);
  }
  showTapPage();
}

bool cloudAvailable() {
  return hasLastCommand && !communicationTimedOut &&
         lastCommand.targetState == charger::TargetState::AVAILABLE;
}

void sendAck(uint32_t id) { espLink.print(F("ACK|")); espLink.println(id); }
void sendNack(uint32_t id, const char* reason) {
  espLink.print(F("NACK|")); espLink.print(id); espLink.print('|');
  espLink.println(reason);
}

void applyCommand(const charger::Command& command) {
  Serial.print(F("Cloud command applied: "));
  Serial.print(command.commandId); Serial.print(F(" "));
  Serial.print(charger::targetStateToString(command.targetState));
  Serial.print(F(" ")); Serial.print(command.powerLimitW); Serial.println(F(" W"));
  if (command.targetState == charger::TargetState::STANDBY) {
    cancelSession(F("cloud STANDBY"));
  } else if (sessionActive) {
    // A new AVAILABLE command changes the simulated values without another tap.
    showActivePage();
  } else {
    showTapPage();
  }
}

void handleStatus(char* line) {
  char* category = line + 7;
  char* separator = strchr(category, '|');
  if (!separator) return;
  *separator = '\0';
  char* state = separator + 1;
  char* value = strchr(state, '|');
  if (value) *value++ = '\0';
  const bool hasValue = value && *value;
  if (strcmp(category, "WIFI") == 0) {
    if (strcmp(state, "CONNECTING") == 0) Serial.println(F("[ESP] Connecting to Wi-Fi..."));
    else if (strcmp(state, "CONNECTED") == 0 && hasValue) {
      Serial.print(F("[ESP] Wi-Fi connected; IP: ")); Serial.println(value);
    } else if (strcmp(state, "DISCONNECTED") == 0)
      Serial.println(F("[ESP] Wi-Fi disconnected; API polling paused"));
  } else if (strcmp(category, "API") == 0) {
    if (strcmp(state, "REQUESTING") == 0 && hasValue) { Serial.print(F("[ESP] API request #")); Serial.print(value); Serial.println(F(" started")); }
    else if (strcmp(state, "HTTP_OK") == 0 && hasValue) { Serial.print(F("[ESP] HTTP response: ")); Serial.println(value); }
    else if (strcmp(state, "ACCESS_CONFIRMED") == 0) Serial.println(F("[ESP] API access confirmed"));
    else if (strcmp(state, "AUTH_ERROR") == 0 && hasValue) { Serial.print(F("[ESP] API authentication failed: HTTP ")); Serial.println(value); }
    else if (strcmp(state, "HTTP_ERROR") == 0 && hasValue) { Serial.print(F("[ESP] API HTTP error: ")); Serial.println(value); }
    else if (strcmp(state, "CONNECTION_FAILED") == 0 && hasValue) { Serial.print(F("[ESP] API connection failed: transport error ")); Serial.println(value); }
    else if (strcmp(state, "HTTP_INIT_FAILED") == 0) Serial.println(F("[ESP] Could not initialize API request"));
    else if (strcmp(state, "INVALID_JSON") == 0) Serial.println(F("[ESP] API response contains invalid JSON"));
    else if (strcmp(state, "INVALID_COMMAND") == 0) {
      if (hasValue) { Serial.print(F("[ESP] API command rejected: ")); Serial.println(value); }
      else Serial.println(F("[ESP] API response contains an invalid command"));
    }
  } else if (strcmp(category, "UART") == 0 && hasValue) {
    if (strcmp(state, "ACK_RECEIVED") == 0) { Serial.print(F("[ESP] ATmega ACK received for command ")); Serial.println(value); }
    else if (strcmp(state, "NO_ACK") == 0) { Serial.print(F("[ESP] No ATmega ACK received for command ")); Serial.println(value); }
  }
}

void processCommand(const char* line) {
  charger::Command received = {};
  charger::ValidationError result = charger::parseUartCommandLine(
      line, config::CHARGER_ID, config::LIMITED_POWER_W, config::FULL_POWER_W, received);
  if (result != charger::ValidationError::NONE) { sendNack(0, charger::validationErrorToString(result)); return; }
  if (hasLastCommand && received.commandId < lastCommand.commandId) { Serial.println(F("Rejected stale command ID")); sendNack(received.commandId, "STALE_COMMAND_ID"); return; }
  if (hasLastCommand && received.commandId == lastCommand.commandId) {
    if (!charger::commandsHaveSamePayload(received, lastCommand)) { Serial.println(F("Rejected reused command ID with different payload")); sendNack(received.commandId, "COMMAND_ID_REUSED"); return; }
    lastValidCommandAtMs = millis();
    if (communicationTimedOut) { communicationTimedOut = false; Serial.println(F("Cloud communication restored")); applyCommand(received); }
    sendAck(received.commandId); return;
  }
  lastCommand = received; hasLastCommand = true; communicationTimedOut = false;
  lastValidCommandAtMs = millis(); applyCommand(received); sendAck(received.commandId);
}

void dispatchLine(char* line) {
  if (strncmp(line, "STATUS|", 7) == 0) handleStatus(line);
  else processCommand(line);
}

void readEspLink() {
  while (espLink.available()) {
    char incoming = static_cast<char>(espLink.read());
    if (incoming == '\n') { receiveBuffer[receiveLength] = '\0'; if (receiveLength) dispatchLine(receiveBuffer); receiveLength = 0; }
    else if (incoming != '\r') {
      if (receiveLength < sizeof(receiveBuffer) - 1) receiveBuffer[receiveLength++] = incoming;
      else { receiveLength = 0; sendNack(0, "LINE_TOO_LONG"); }
    }
  }
}

bool uidEquals(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 4) == 0; }
bool authorized(const uint8_t* uid) {
  for (size_t i = 0; i < sizeof(authorizedUIDs) / sizeof(authorizedUIDs[0]); ++i)
    if (uidEquals(uid, authorizedUIDs[i])) return true;
  return false;
}

void printUid(const uint8_t* uid) {
  for (uint8_t i = 0; i < 4; ++i) { if (uid[i] < 0x10) Serial.print('0'); Serial.print(uid[i], HEX); if (i != 3) Serial.print(':'); }
}

void handleRfid() {
  if (cardCooldownStarted && static_cast<uint32_t>(millis() - lastCardAtMs) < RFID_COOLDOWN_MS) return;
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;
  cardCooldownStarted = true; lastCardAtMs = millis();
  Serial.print(F("RFID UID "));
  if (mfrc522.uid.size != 4) {
    Serial.print(F("(length ")); Serial.print(mfrc522.uid.size); Serial.println(F(") rejected; four bytes required"));
  } else {
    printUid(mfrc522.uid.uidByte);
    if (!authorized(mfrc522.uid.uidByte)) Serial.println(F(" unauthorized; session unchanged"));
    else if (sessionActive && uidEquals(mfrc522.uid.uidByte, sessionUid)) { Serial.println(F(" authorized; ending owned session")); cancelSession(F("owner tapped again")); }
    else if (sessionActive) Serial.println(F(" authorized, but another card already owns the active session"));
    else if (!cloudAvailable()) Serial.println(F(" authorized, but charger is unavailable; remain on page1"));
    else { memcpy(sessionUid, mfrc522.uid.uidByte, 4); sessionActive = true; Serial.println(F(" authorized; simulated session started")); showActivePage(); }
  }
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void enforceCommunicationTimeout() {
  if (!hasLastCommand || communicationTimedOut || static_cast<uint32_t>(millis() - lastValidCommandAtMs) < config::COMMAND_TIMEOUT_MS) return;
  communicationTimedOut = true; Serial.println(F("Cloud command timeout")); cancelSession(F("communication timeout"));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  espLink.begin(115200);
  myNex.begin(9600);
  delay(500);
  myNex.writeStr("page page1");
  currentScreen = Screen::TAP_RFID;
  pinMode(METER_CS_PIN, OUTPUT);
  digitalWrite(METER_CS_PIN, HIGH);
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println(F("EV charger showroom controller; waiting for cloud command and RFID"));
}

void loop() {
  readEspLink();
  enforceCommunicationTimeout();
  handleRfid();
}
