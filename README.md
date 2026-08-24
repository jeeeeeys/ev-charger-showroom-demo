# EV Charger Showroom Communications MVP

This project implements the first showroom-development milestone:

```text
Platform or mock API -> ESP8266 -> UART -> ATmega2560 -> USB serial monitor
```

It deliberately does not control the contactor, pilot circuit, metering IC, or
Nextion display. The ATmega exposes a three-state display interface that can be
connected to the display later.

## Agreed API response

The GET endpoint must return one JSON object containing all five fields:

```json
{
  "charger_id": "EVSE-01",
  "command_id": 3,
  "target_state": "AVAILABLE",
  "power_limit_w": 5000,
  "timestamp": "2026-08-19T00:35:00Z"
}
```

For this MVP, the accepted combinations are:

| `target_state` | `power_limit_w` | Internal display state |
| --- | ---: | --- |
| `STANDBY` | `0` | `UI_STANDBY` |
| `AVAILABLE` | `5000` | `UI_READY_LIMITED` |
| `AVAILABLE` | `7000` | `UI_READY_FULL` |

`command_id` is a positive integer that increases whenever the platform issues
a new command. Repeated GET responses for the current command retain the same
ID. `timestamp` must use the exact UTC form `YYYY-MM-DDTHH:MM:SSZ`.

## UART contract

The ESP sends one newline-terminated command every time it receives a valid API
response:

```text
CMD|EVSE-01|3|AVAILABLE|5000|2026-08-19T00:35:00Z
```

The ATmega responds with:

```text
ACK|3
```

or:

```text
NACK|3|REASON
```

Repeated valid commands refresh the ATmega communication timer without causing
another state transition. If valid commands stop for 60 seconds, the ATmega
returns to `UI_STANDBY`.

## Hardware UART assumption

The charger schematic confirms ATmega2560 USART1 for the ESP link:

| Device signal | MCU package pin | Arduino pin/use |
| --- | ---: | --- |
| ATmega `PD2/RXD1/INT2` | 45 | Mega D19/RX1, ESP link |
| ATmega `PD3/TXD1/INT3` | 46 | Mega D18/TX1, ESP link |
| ATmega UART0 | — | USB serial monitor, 115200 baud |
| ESP8266 `TXD0/GPIO1` | ESP-12F pin 16 | ATmega link |
| ESP8266 `RXD0/GPIO3` | ESP-12F pin 15 | ATmega link |

The PCB includes two BSN20 MOSFET level-shifter channels with 10 kOhm pull-ups
to 3.3 V and 5 V. Both controllers share digital ground.

P1 and P2 provide the required logical crossing:

```text
ESP TXD0/GPIO1 -> Q2 -> TX -> P1 -> ATmega PD2/RXD1
ESP RXD0/GPIO3 <- Q1 <- RX <- P2 <- ATmega PD3/TXD1
```

Therefore, ESP transmit reaches ATmega receive, and ATmega transmit reaches ESP
receive. No additional UART crossover is required.

ESP debug output is disabled. UART0 carries only protocol messages to the
ATmega, and the ATmega UART0 serial monitor provides all required diagnostics.

## Arduino IDE sketch folders

Open and upload the two sketches separately:

| Controller | Sketch to open |
| --- | --- |
| ESP-12F / ESP8266 | `arduino/ESP8266_Showroom/ESP8266_Showroom.ino` |
| ATmega2560 | `arduino/ATmega2560_Showroom/ATmega2560_Showroom.ino` |

Each folder is self-contained so it can be opened directly in Arduino IDE.
The duplicated `ChargerProtocol` files must remain identical in both folders.

## Configuration

1. Edit both copies of `ProjectConfig.h` if the charger ID or the 5 kW / 7 kW
   state mapping changes.
2. Locally copy `arduino/ESP8266_Showroom/Secrets.example.h` to
   `arduino/ESP8266_Showroom/Secrets.h`, then enter the actual Wi-Fi
   credentials, API URL, and API key.
3. Never commit real credentials or the real API key. `Secrets.h` is ignored by
   Git; `Secrets.example.h` is the tracked template.

The ESP8266 sends the key in the `X-API-Key` header on each platform request:

```http
GET /api/v1/chargers/EVSE-01/command HTTP/1.1
Accept: application/json
X-API-Key: YOUR_API_KEY

```

An invalid or missing key is expected to produce HTTP `401` or `403`, depending
on the platform implementation. After changing Wi-Fi credentials, the API
endpoint, or the API key, recompile and reupload only the ESP8266 firmware. The
ATmega2560 firmware and Nextion display do not need to be reuploaded for these
configuration-only changes.

HTTPS currently uses `setInsecure()` only when
`ALLOW_INSECURE_HTTPS_FOR_DEMO` is true. This is acceptable only for the isolated
showroom MVP. Configure CA validation before the design is used outside the
demonstration environment.

## Run the local mock API

Until the platform endpoint is ready, start the included server on a laptop
connected to the same Wi-Fi network as the ESP8266:

On macOS or Linux, run the Python version:

```bash
python mock_api/server.py
```

On Windows, open PowerShell in the repository root and run the native
PowerShell version:

```powershell
powershell -ExecutionPolicy Bypass -File .\mock_api\server.ps1
```

The PowerShell server uses only Windows PowerShell and built-in .NET classes;
it does not require Python, Node.js, administrator privileges, or external
PowerShell modules. Both versions listen on all network interfaces on TCP port
8080. Press Ctrl+C to stop the server.

Find the laptop's LAN address and set `secrets::API_URL` to, for example:

```cpp
static constexpr char API_URL[] =
    "http://192.168.1.25:8080/api/v1/chargers/EVSE-01/command";
static constexpr char API_KEY[] = "";
```

Edit `mock_api/command.json` to switch among the three test commands. Increment
`command_id` and update `timestamp` whenever a new command is issued. The laptop
firewall must allow incoming TCP connections to port 8080 on the private network.

## Build and upload with Arduino IDE

### One-time setup

1. Install the ESP8266 board package in Arduino IDE's Boards Manager.
2. Install `ArduinoJson` by Benoit Blanchon in Library Manager. The ESP sketch
   uses `StaticJsonDocument`, which is supported by ArduinoJson 6 and 7.
3. The ATmega sketch uses only the standard Arduino AVR core.

### ESP-12F settings

Open `arduino/ESP8266_Showroom/ESP8266_Showroom.ino`, then use the settings
already proven on this PCB:

| Setting | Value |
| --- | --- |
| Board | Generic ESP8266 Module |
| Upload speed | 115200 |
| CPU frequency | 80 MHz |
| Flash size | 4 MB (FS: none) |
| Flash frequency | 40 MHz |
| Flash mode | DIO |
| Reset method | `ck` |
| Debug port / level | Disabled / None |
| lwIP variant | v2 Lower Memory |
| VTables | Flash |
| Builtin LED | 2 |
| Erase flash | Only Sketch |
| Programmer | Default |

ESP-12F flashing also requires its boot-mode pins to be accessible and correctly
strapped. Confirm GPIO0, EN, RESET, GPIO2, and GPIO15 before attempting an upload.

### Confirmed manual flash procedure

1. Connect the USB-UART adapter ground to `DGND`. Do not connect its 5 V output.
2. Connect adapter `D0` to the board `TX` jumper pad and adapter `D1` to the
   board `RX` jumper pad, following the previously working setup.
3. Short `GPIO0` to ground.
4. Briefly short `NRST` to ground, then release `NRST` while keeping GPIO0 low.
5. Upload the ESP firmware at 115200 baud.
6. Remove the GPIO0-to-ground short.
7. Briefly short `NRST` to ground again to boot the application.

When the USB-UART adapter is connected, ensure the ATmega-side UART transmitter
is isolated or inactive so that it does not contend with the adapter.

### ATmega2560 settings

Open `arduino/ATmega2560_Showroom/ATmega2560_Showroom.ino` and select:

| Setting | Value |
| --- | --- |
| Board | Arduino Mega or Mega 2560 |
| Processor | ATmega2560 |
| Serial Monitor baud | 115200 |

Upload the ATmega sketch, then open Serial Monitor at 115200 baud. Restore the
P1/P2 UART path after ESP flashing so the ESP and ATmega can communicate.

See `HARDWARE_NOTES.md` for the full MCU pin allocation supplied for this PCB.

## Host-side protocol test

The shared UART parser and state mapping can be tested without either board:

```bash
./tests/run_host_tests.sh
```

The test covers all three valid states and rejection of malformed commands,
wrong charger IDs, invalid command IDs, invalid timestamps, and unsupported
power limits.

## Expected ATmega serial monitor output

```text
EV charger showroom controller
Initial state: UI_STANDBY
Waiting for a valid cloud command from ESP8266...

Cloud command applied
Charger ID: EVSE-01
Command ID: 3
Target state: AVAILABLE
Available power: 5000 W
Timestamp: 2026-08-19T00:35:00Z
Display state: UI_READY_LIMITED
```

The later Nextion integration only needs to map `UI_STANDBY`,
`UI_READY_LIMITED`, and `UI_READY_FULL` to its three pages.

## Configuration references

- [ESP8266 Arduino core IDE options](https://arduino-esp8266.readthedocs.io/en/latest/ideoptions.html)
- [Arduino Mega AVR pin mapping](https://github.com/arduino/ArduinoCore-avr/blob/master/variants/mega/pins_arduino.h)
