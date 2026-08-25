#!/usr/bin/env python3
"""Host checks for safety-critical showroom integration constants/contracts."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ATMEGA = (ROOT / "arduino/ATmega2560_Showroom/ATmega2560_Showroom.ino").read_text()
ESP = (ROOT / "arduino/ESP8266_Showroom/ESP8266_Showroom.ino").read_text()

def fixed2(value: int) -> str:
    return f"{value // 100}.{value % 100:02d}"

for watts, power, current in ((5000, "5.00", "21.74"), (7000, "7.00", "30.43")):
    assert fixed2(watts // 10) == power
    assert fixed2((watts * 100 + 115) // 230) == current

for required in (
    "EasyNex myNex(Serial2)", "RFID_RST_PIN = 44", "RFID_SS_PIN = 46",
    "METER_CS_PIN = 48", 'myNex.writeStr("page page1")', '"txtVoltage.txt"',
    '"txtCurrent.txt"', '"txtPower.txt"', '"txtEnergy.txt"',
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
print("All showroom contract tests passed.")
