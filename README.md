# EV Charger Showroom Demo

This repository implements an **indoor, display-only simulation**:

```text
Platform/mock API -> ESP8266 -> Serial1 -> ATmega2560 -> RFID + Nextion
```

It has no relay, contactor, PWM, control-pilot, vehicle detection, ATM90E36A
metering, energy accumulation, or charging/power-delivery functionality. Never
connect a vehicle on the strength of this demonstration firmware.

## API and UART protocol

The ESP accepts the command either directly at the JSON root or, temporarily,
inside a top-level `data` object:

```json
{
  "charger_id": "EVSE-01",
  "command_id": 3,
  "target_state": "AVAILABLE",
  "power_limit_w": 5000,
  "timestamp": "2026-08-25T00:10:11Z"
}
```

```json
{
  "data": {
    "charger_id": "EVSE-01",
    "command_id": 3,
    "target_state": "STANDBY",
    "power_limit_w": 0,
    "timestamp": "2026-08-25T00:10:11Z"
  }
}
```

If `data` is present it must be an object. Both forms require the exact charger
ID, a positive numeric command ID, numeric power, an exact
`YYYY-MM-DDTHH:MM:SSZ` timestamp, and one of these combinations:

| State | Power |
| --- | ---: |
| `STANDBY` | 0 W |
| `AVAILABLE` | 5000 W or 7000 W |

The platform must increment `command_id` whenever payload fields change. An
identical repeated command refreshes the 60-second watchdog; a lower ID or a
reused ID with a changed payload is rejected. The ESP preserves the
`X-API-Key` header and never logs credentials. Create the ignored
`arduino/ESP8266_Showroom/Secrets.h` from `Secrets.example.h` locally.

Commands use `CMD|...` and `ACK|id`/`NACK|id|reason`. Structured API, UART, and
Wi-Fi `STATUS|...` diagnostics are not acknowledged and cannot refresh the
watchdog or affect a session. API polling pauses while Wi-Fi is down and occurs
immediately after reconnection. No external connectivity-check endpoint is
used. Existing showroom HTTPS behavior is unchanged.

## Fixed wiring

All grounds are common.

| Function | Fixed wiring | Baud/power |
| --- | --- | --- |
| ESP8266 UART | ESP TXD0 -> Mega D19/RX1; ESP RXD0 <- Mega D18/TX1 (`Serial1`) | 115200 |
| USB monitor | Mega UART0 (`Serial`) | 115200 |
| Nextion NX4832F035 | Mega D16 RX <- TX; D17 TX -> RX (`SoftwareSerial`) | 9600, 5 V |
| MFRC522 | RST D44/PL5; SS D46/PL3; MISO D50/PB3; MOSI D51/PB2; SCK D52/PB1 | hardware SPI, 3.3 V |
| ATM90E36A | CS D48/PL1 | unused; output held HIGH |

D48 is driven HIGH **before** `SPI.begin()` and RFID initialization so the
unused meter releases the shared bus. The firmware never initializes or reads
the meter. Pins 10 and 11 are not used.

## RFID and cloud behavior

The four authorized four-byte UIDs are `B3:E2:E1:C7`, `B3:C7:E0:C8`,
`43:D1:FD:E3`, and `33:5F:07:E4`. Scanning runs continuously without a blocking
loop delay, and a cooldown prevents one tap from registering twice.

An authorized card starts a simulated session only while a valid, non-timed-out
`AVAILABLE` command exists. An unauthorized card or an authorized tap while
unavailable leaves page 1 unchanged. The owner UID is retained: tapping that
same card again ends the session, while another authorized card cannot take or
stop it. `STANDBY` or the 60-second communication timeout immediately cancels
the session. A new valid `AVAILABLE` power command updates an active page 2
without another tap. These actions are reported on the USB Serial Monitor.

## Nextion project

The firmware uses only page `page1` (default, “Tap RFID”) and page `page2`
(active simulation), via Easy Nextion Library 1.0.6 and `SoftwareSerial` (RX =
16, TX = 17, baud = 9600). It changes pages only on startup or a real
transition.

No editable `.HMI` is included, so update the Nextion project manually. Create
Text components with a maximum text length of at least 16:

| Page 2 Text component | Dynamic `.txt` value | Separate static label |
| --- | --- | --- |
| `txtVoltage` | `230` | `V` |
| `txtPower` | `5.00` or `7.00` | `kW` |
| `txtCurrent` | `21.74` or `30.43` | `A` |
| `txtEnergy` | `00` | `kWh` |

The four dynamic values are numeric characters only—no spaces or units. Voltage
is fixed at 230 V. Integer arithmetic calculates hundredths of an ampere as
`(power_limit_w * 100 + 115) / 230`, rounded to 0.01 A. Energy remains exactly
`00` for the entire demo. Ensure the HMI page names are exactly `page1` and
`page2`; no touch events, connectivity page, fault page, or maintenance page is
required.

## Arduino IDE setup

Open and upload the sketches separately:

* `arduino/ESP8266_Showroom/ESP8266_Showroom.ino` — Generic ESP8266 Module.
* `arduino/ATmega2560_Showroom/ATmega2560_Showroom.ino` — Arduino Mega or Mega
  2560.

Install these libraries from Arduino IDE Library Manager:

* **ArduinoJson** (ESP8266; ArduinoJson 6 or 7)
* **MFRC522** (ATmega2560)
* **EasyNextionLibrary** (ATmega2560)

The standard ESP8266 and Arduino AVR board cores are also required. The two
copies of `ChargerProtocol` must remain identical.

## Local API and host verification

Run `python mock_api/server.py` (or `mock_api/server.ps1` on Windows), configure
the local URL in the untracked `Secrets.h`, and edit `mock_api/command.json`.
Increment the ID and timestamp for every changed command.

Run the portable protocol checks with:

```bash
./tests/run_host_tests.sh
```

Physical verification must confirm page startup/transitions, all four RFID
cards and ownership rules, the 5/7 kW values, active-session power updates,
timeout/STANDBY cancellation, Wi-Fi loss/restoration diagnostics, ongoing ESP
poll/ACK behavior during scans, and that no power output is activated.
