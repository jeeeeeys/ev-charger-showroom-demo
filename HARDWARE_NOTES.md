# Charger Controller Hardware Notes

The numeric values in the original schematic table are ATmega2560 package pin
numbers. They are not the Arduino Mega digital-pin numbers. The firmware uses
Arduino peripheral names such as `Serial1` and `SPI`, so both mappings are kept
below.

## ESP8266 UART and flashing

| Function | ESP-12F signal | ATmega2560 signal | Arduino Mega mapping |
| --- | --- | --- | --- |
| ESP to ATmega data | `TXD0/GPIO1` | `PD2/RXD1/INT2`, package pin 45 | D19/RX1 |
| ATmega to ESP data | `RXD0/GPIO3` | `PD3/TXD1/INT3`, package pin 46 | D18/TX1 |

The firmware therefore uses `Serial` on the ESP8266 and `Serial1` on the
ATmega2560. The level shifter uses two BSN20 MOSFETs, with separate 3.3 V and
5 V pull-ups.

P1 and P2 provide the required UART crossover:

```text
ESP TXD0/GPIO1 -> Q2 -> TX -> P1 -> ATmega PD2/RXD1
ATmega PD3/TXD1 -> P2 -> RX -> Q1 -> ESP RXD0/GPIO3
```

The level shifter and jumper routing therefore connect each transmitter to the
opposite controller's receiver correctly.

Confirmed flash-mode entry:

1. Short GPIO0 to ground.
2. Short NRST to ground.
3. Release NRST while keeping GPIO0 low.
4. Upload firmware.
5. Remove the GPIO0 short.
6. Pulse NRST low to run the new firmware.

Confirmed USB-UART hookup:

| USB-UART | Charger PCB |
| --- | --- |
| 5 V | Not connected |
| RESET | Not connected |
| D0 | TX jumper pad |
| D1 | RX jumper pad |
| GND | DGND |

## ATmega2560 peripheral allocation

| Connector/function | Identifier | ATmega port/function | Package pin | Arduino Mega mapping |
| --- | --- | --- | ---: | --- |
| Relay PWM | PB4 | OC2A/PCINT4 | 23 | D10 |
| Relay RL1 | PH4 | OC4B | 16 | D7 |
| Relay RL2 | PH5 | OC4C | 17 | D8 |
| Relay RL3 | PH6 | OC2B | 18 | D9 |
| Energy meter SCK | PB1 | SCK/PCINT1 | 20 | D52/SCK |
| Energy meter MOSI | PB2 | MOSI/PCINT2 | 21 | D51/MOSI |
| Energy meter MISO | PB3 | MISO/PCINT3 | 22 | D50/MISO |
| Energy meter CS | PL1 | ICP5 | 36 | D48 |
| RGB LD1 | PG5 | OC0B | 1 | D4 |
| RGB LD2 | PE3 | OC3A/AIN1 | 5 | D5 |
| RGB LD3 | PH3 | OC4A | 15 | D6 |
| TFT TX2 | PH1 | TXD2 | 13 | D16/TX2 |
| TFT RX2 | PH0 | RXD2 | 12 | D17/RX2 |
| J8 TX3 | PJ1 | TXD3/PCINT10 | 64 | D14/TX3 |
| J8 RX3 | PJ0 | RXD3/PCINT9 | 63 | D15/RX3 |
| J4 SDA | PD1 | SDA/INT1 | 44 | D20/SDA |
| J4 SCL | PD0 | SCL/INT0 | 43 | D21/SCL |
| RFID RST | PL5 | OC5C | 40 | D44 |
| RFID MISO | PB3 | MISO/PCINT3 | 22 | D50/MISO |
| RFID MOSI | PB2 | MOSI/PCINT2 | 21 | D51/MOSI |
| RFID SCK | PB1 | SCK/PCINT1 | 20 | D52/SCK |
| RFID SDA/CS | PL3 | OC5A | 38 | D46 |
| Serial0 RX | PE0 | RXD0/PCINT8 | 2 | D0/RX0 |
| Serial0 TX | PE1 | TXD0 | 3 | D1/TX0 |

The energy meter and RFID reader share the hardware SPI clock and data lines.
Their chip-select signals are separate: energy meter CS is PL1/D48, and RFID
SDA/CS is PL3/D46. Firmware must keep every inactive device's CS high and assert
only one CS at a time.

## Power and ground notes

- Relay, energy-meter, RGB, TFT, J8, J4, and Serial0 headers expose 5 V.
- RFID exposes 3.3 V.
- ESP-12F uses 3.3 V and is not 5 V tolerant.
- All listed interfaces reference DGND.
