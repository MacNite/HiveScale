# HiveScale V0 breakout PCB

This directory contains the KiCad design for the first HiveScale breakout PCB revision. The PCB is intended to reduce field wiring by bringing the ESP32, scale amplifiers, load-cell terminal blocks, sensors, SD card module, solar/battery modules, and SIM7080G connector onto one documented wiring board.

The current design is a **V0 prototype**. Build and electrically test the first boards before using them in sealed outdoor deployments.

---

## Files

| Path | Description |
|---|---|
| `HiveScale_V0.kicad_pro` | KiCad project file |
| `HiveScale_V0.kicad_sch` | Schematic |
| `HiveScale_V0.kicad_pcb` | PCB layout |
| `HiveScale_V0.kicad_prl` | KiCad project local settings |
| `fabrication/` | Gerber, drill, and job files for fabrication review/order |
| `HiveScale_V0-backups/` | KiCad autosave/backup ZIPs |
| `todo-list.md` | Open hardware TODOs for the next PCB revision |

---

## Design goals

- Provide a clear breakout for a 30-pin ESP32 dev board.
- Route two HX711 scale channels and external load-cell terminal blocks.
- Keep DS18B20, SHT40/SHT4x, RTC, SD, and setup-button wiring consistent with firmware.
- Add optional off-grid support for solar input, LiPo monitoring, buck-boost regulation, and SIM7080G cellular transport.
- Expose an LTE modem power/control pin so the firmware can wake, reset, or shut down the modem before sleep.
- Make the board easy to assemble from common 2.54 mm modules and terminal blocks.

---

## Main connector overview

| Ref | Label / value | Purpose |
|---|---|---|
| `J1` | `ESP32-left-RXTX` | Left side of ESP32 dev board |
| `J2` | `ESP32-right-powerin` | Right side of ESP32 dev board |
| `J3` | `Scale2_to_ESP32` | HX711 scale 2 module connection to ESP32 |
| `J4` | `Scale1_to_ESP32` | HX711 scale 1 module connection to ESP32 |
| `J5` | `RTC` | DS3231 RTC module |
| `J7` | `Scale1_to_Terminal` | Scale 1 HX711 to load-cell terminal bridge |
| `J8` | `Scale2_to_Terminal` | Scale 2 HX711 to load-cell terminal bridge |
| `J9` | `SD-Module` | MicroSD module |
| `J10` | `PowerIn` | Main power input terminal |
| `J11` | `Loadcell1-Input` | Scale 1 external load-cell terminal block |
| `J12` | `Loadcell2-Input` | Scale 2 external load-cell terminal block |
| `J13` | `DS18B20 Hive1` | Hive 1 waterproof temperature probe |
| `J14` | `DS18B20 Hive2` | Hive 2 waterproof temperature probe |
| `J15` | `SHT40 Ambient` | Ambient temp/humidity sensor |
| `J16` | `SolarPowerIn and INA219` | Solar / INA219 monitor header |
| `J17` | `CN3971` | Solar LiPo charger module header |
| `J18` | `TPS63020-GND-OUT` | Buck-boost output ground header |
| `J19` | `TPS63020-V-IN` | Buck-boost input voltage header |
| `J20` | `MAX17048 to ESP32` | LiPo fuel gauge I2C header |
| `J21` | `BatteryMAX17048` | Battery connection to MAX17048 circuit/module |
| `J22` | `SIM7080G` | LTE/NB-IoT modem connector |
| `J23` | `TPS63020-3.3V-OUT` | Buck-boost 3.3 V output header |
| `R1` | 1-Wire pull-up resistor | DS18B20 data pull-up from GPIO4 to 3.3 V |
| `SW1` | Setup button | GPIO27 to GND |

---

## ESP32 pin breakout

### `J1` ESP32 left side

| Pin | Net |
|---:|---|
| 1 | Not connected |
| 2 | GND |
| 3 | GPIO15 |
| 4 | GPIO2 |
| 5 | GPIO4 |
| 6 | GPIO16 |
| 7 | GPIO17 |
| 8 | GPIO5 |
| 9 | GPIO18 |
| 10 | GPIO19 |
| 11 | GPIO21 |
| 12 | ESP32-RX0 |
| 13 | ESP32-TX0 |
| 14 | GPIO22 |
| 15 | GPIO23 |

### `J2` ESP32 right side

| Pin | Net |
|---:|---|
| 1 | 3.3 V |
| 2 | GND |
| 3 | GPIO13 |
| 4 | GPIO12 |
| 5 | GPIO14 |
| 6 | GPIO27 |
| 7 | GPIO26 |
| 8 | GPIO25 |
| 9 | GPIO33 |
| 10 | GPIO32 |
| 11 | GPIO35 |
| 12 | GPIO34 |
| 13 | Not connected |
| 14 | Not connected |
| 15 | ESP32 EN |

---

## Firmware-relevant connector pinout

### HX711 headers

| Ref | Pin | Net | Firmware use |
|---|---:|---|---|
| `J4` Scale 1 | 1 | 3.3 V | HX711 VCC |
| `J4` Scale 1 | 2 | GPIO17 | HX711 #1 SCK |
| `J4` Scale 1 | 3 | GPIO16 | HX711 #1 DOUT |
| `J4` Scale 1 | 4 | GND | HX711 GND |
| `J3` Scale 2 | 1 | 3.3 V | HX711 VCC |
| `J3` Scale 2 | 2 | GPIO33 | HX711 #2 SCK |
| `J3` Scale 2 | 3 | GPIO32 | HX711 #2 DOUT |
| `J3` Scale 2 | 4 | GND | HX711 GND |

### Sensor headers

| Ref | Pin | Net | Use |
|---|---:|---|---|
| `J13` Hive 1 DS18B20 | 1 | 3.3 V | Probe VDD |
| `J13` Hive 1 DS18B20 | 2 | GND | Probe GND |
| `J13` Hive 1 DS18B20 | 3 | GPIO4 | 1-Wire data |
| `J14` Hive 2 DS18B20 | 1 | 3.3 V | Probe VDD |
| `J14` Hive 2 DS18B20 | 2 | GND | Probe GND |
| `J14` Hive 2 DS18B20 | 3 | GPIO4 | 1-Wire data |
| `J15` SHT40/SHT4x | 1 | 3.3 V | VCC |
| `J15` SHT40/SHT4x | 2 | GND | GND |
| `J15` SHT40/SHT4x | 3 | GPIO21 | I2C SDA |
| `J15` SHT40/SHT4x | 4 | GPIO22 | I2C SCL |
| `J5` RTC | 1 | GPIO22 | I2C SCL |
| `J5` RTC | 2 | GPIO21 | I2C SDA |
| `J5` RTC | 3 | 3.3 V | VCC |
| `J5` RTC | 4 | GND | GND |

### SD module header

The board follows the current firmware mapping: **MISO on GPIO23** and **MOSI on GPIO19**.

| `J9` pin | Net | Firmware use |
|---:|---|---|
| 1 | GND | SD GND |
| 2 | 3.3 V | SD VCC |
| 3 | GPIO23 | SD MISO |
| 4 | GPIO19 | SD MOSI |
| 5 | GPIO18 | SD SCK |
| 6 | GPIO5 | SD CS |

### Setup button

| Ref | Pin | Net |
|---|---:|---|
| `SW1` | 1 | GND |
| `SW1` | 2 | GPIO27 |

Short press opens the provisioning AP. Long press clears firmware Preferences and reboots.

---

## Off-grid connector pinout

### SIM7080G LTE/NB-IoT connector

| `J22` pin | Net | Suggested signal |
|---:|---|---|
| 1 | GND | Modem GND |
| 2 | GPIO25 | ESP32 TX -> modem RX |
| 3 | GPIO26 | ESP32 RX <- modem TX |
| 4 | GPIO14 | Modem PWRKEY or power-enable/control input |
| 5 | 3.3 V | Logic/module supply if the modem board supports it |
| 6 | GND | Modem GND |
| 7 | Not connected | Spare / future use |

Firmware defaults use GPIO25/GPIO26 for UART. To use the PCB's GPIO14 control pin, set one of the following in `secrets.h` depending on the modem module wiring:

```cpp
#define SIM7080G_PWRKEY_PIN   14
#define SIM7080G_POWER_EN_PIN -1
```

or:

```cpp
#define SIM7080G_PWRKEY_PIN      -1
#define SIM7080G_POWER_EN_PIN    14
#define SIM7080G_POWER_EN_ACTIVE_HIGH 1
```

Do not power a high-current LTE modem directly from a weak ESP32 3.3 V regulator. Verify the modem breakout's required voltage and peak-current budget.

### Solar / INA219 / CN3971 / MAX17048 / TPS63020 headers

| Ref | Pin | Net | Use |
|---|---:|---|---|
| `J16` | 1 | 3.3 V | INA219 VCC |
| `J16` | 2 | GND | INA219 GND |
| `J16` | 3 | GPIO22 | INA219 SCL |
| `J16` | 4 | GPIO21 | INA219 SDA |
| `J16` | 5 | Solar path net | Solar/charge path |
| `J16` | 6 | Solar path net | Solar/charge path |
| `J17` | 1 | Charger/battery net | CN3971 battery side |
| `J17` | 2 | Charger/battery net | CN3971 battery side |
| `J17` | 3 | Solar path net | CN3971 solar side |
| `J17` | 4 | Solar path net | CN3971 solar side |
| `J20` | 1 | 3.3 V | MAX17048 logic VCC |
| `J20` | 2 | GND | MAX17048 GND |
| `J20` | 3 | GPIO22 | MAX17048 SCL |
| `J20` | 4 | GPIO21 | MAX17048 SDA |
| `J20` | 5 | Not connected | Spare |
| `J20` | 6 | Not connected | Spare |
| `J21` | 1 | Battery net | MAX17048 battery sense path |
| `J21` | 2 | Battery net | MAX17048 battery sense path |
| `J19` | 1-2 | +5 V | TPS63020 input voltage header |
| `J23` | 1-2 | 3.3 V | TPS63020 output header |
| `J6` / `J18` | 1-2 | GND | TPS63020 ground headers |

The solar/battery section needs prototype validation before field use. See `todo-list.md` for known next steps, especially LTE modem decoupling and charge-path improvements.

---

## Load-cell terminal blocks

`J11` and `J12` are 12-pin terminal blocks for the external load-cell wiring. They are intended to make platform-scale wiring easier and to bridge into the HX711 module headers via `J7` and `J8`.

Because 3-wire load-cell color codes differ by supplier, label your bridge nodes during assembly rather than relying only on color. Verify the Wheatstone bridge with a multimeter before powering the HX711.

Recommended workflow:

1. Wire one scale completely on the bench.
2. Verify no short between excitation and signal nodes.
3. Check that the HX711 raw reading changes consistently when each corner is pressed.
4. Repeat for scale 2.
5. Run tare and known-weight calibration after final mechanical installation.

---

## Fabrication and assembly checklist

Before ordering:

- Open `HiveScale_V0.kicad_sch` and run ERC.
- Open `HiveScale_V0.kicad_pcb` and run DRC.
- Review net classes and trace widths for modem, solar, and battery current paths.
- Review connector orientation against the physical modules you will solder or plug in.
- Confirm SD MISO/MOSI routing matches the firmware mapping.
- Confirm GPIO14 is suitable for the selected SIM7080G control function.

After assembly:

- Power the board without the ESP32 installed and verify rails.
- Install the ESP32 and check serial boot output.
- Test I2C device detection.
- Test SD card initialization.
- Test HX711 raw readings on both channels.
- Test SIM7080G attach/upload with the expected APN.
- Test deep-sleep current and modem shutoff current.

---

## Known V0 limitations

- LTE modem decoupling still needs improvement.
- Ground vias and high-current return paths should be optimized.
- Mechanical mounting and enclosure fit are not yet finalized.
- Solar/LiPo charging path should be revised to allow USB/5 V LiPo charging as an alternative to solar-only charging.
- Layout should be optimized after the first prototype measurements.
