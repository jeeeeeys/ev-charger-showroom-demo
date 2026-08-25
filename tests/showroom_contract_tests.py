#!/usr/bin/env python3
"""Host checks for safety-critical showroom integration constants/contracts."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ATMEGA = (ROOT / "arduino/ATmega2560_Showroom/ATmega2560_Showroom.ino").read_text()
ESP = (ROOT / "arduino/ESP8266_Showroom/ESP8266_Showroom.ino").read_text()
ATMEGA_CONFIG = (ROOT / "arduino/ATmega2560_Showroom/ProjectConfig.h").read_text()
ESP_CONFIG = (ROOT / "arduino/ESP8266_Showroom/ProjectConfig.h").read_text()

for project_config in (ATMEGA_CONFIG, ESP_CONFIG):
    assert "static constexpr uint32_t UART_BAUD = 9600;" in project_config
    assert "static constexpr uint32_t DEBUG_BAUD = 115200;" in project_config
    assert "static constexpr uint32_t ATMEGA_ACK_TIMEOUT_MS = 1000;" in project_config

assert "Serial.begin(config::DEBUG_BAUD);" in ATMEGA
assert "espLink.begin(config::UART_BAUD);" in ATMEGA
assert ATMEGA.index("sendAck(received.commandId)") < ATMEGA.index("applyCommand(received)")

for watts, current in ((5000, 22), (7000, 30)):
    assert (watts + 115) // 230 == current

for required in (
    "SoftwareSerial nexSerial(16, 17)", "EasyNex myNex(nexSerial)",
    "RFID_RST_PIN = 44", "RFID_SS_PIN = 46", "METER_CS_PIN = 48",
    'PAGE_STANDBY[] = "page page0"', 'PAGE_TAP_RFID[] = "page page1"',
    'PAGE_ACTIVE[] = "page page2"', '"numVoltage.val"', '"numCurrent.val"',
    '"numKW.val"', '"numKWH.val"', '"txtTime.txt"',
    "myNex.writeNum(NUM_VOLTAGE, 230)",
    "myNex.writeNum(NUM_POWER, powerLimitW)",
    "myNex.writeNum(NUM_CURRENT, currentA)", "myNex.writeNum(NUM_ENERGY, 0)",
    "(powerLimitW + 115UL) / 230UL", 'char latestTime[6] = "--:--"',
    "PAGE_SETTLE_MS = 50", "refreshDisplay();",
    "mfrc522.uid.size != 4", "mfrc522.PICC_HaltA()", "mfrc522.PCD_StopCrypto1()",
):
    assert required in ATMEGA, required

for uid in ("{0xB3, 0xE2, 0xE1, 0xC7}", "{0xB3, 0xC7, 0xE0, 0xC8}",
            "{0x43, 0xD1, 0xFD, 0xE3}", "{0x33, 0x5F, 0x07, 0xE4}"):
    assert uid in ATMEGA, uid

assert ATMEGA.index("digitalWrite(METER_CS_PIN, HIGH)") < ATMEGA.index("SPI.begin()")
for forbidden in ("Close_Relays", "Open_Relays", "setupPWM", "analogWrite("):
    assert forbidden not in ATMEGA

# With `data`, it must be an object and becomes the source; otherwise the root remains it.
assert 'document.containsKey("data")' in ESP
assert 'document["data"].is<JsonObjectConst>()' in ESP
assert 'commandJson["charger_id"]' in ESP
assert 'sendStatusValue("API", "INVALID_COMMAND"' in ESP
assert 'sendStatus("WIFI", "CONNECTING")' in ESP
assert 'sendStatus("WIFI", "DISCONNECTED")' in ESP
for required in ('#include <time.h>', 'configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov")',
                 'sendStatusValue("TIME", "UPDATE", timeText)', "maintainTime();"):
    assert required in ESP, required
assert ESP.index("fetchLatestCommand();") < ESP.index("maintainTime();", ESP.index("void loop()"))
print("All showroom contract tests passed.")
