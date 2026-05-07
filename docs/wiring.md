# HiveScale — Wiring Reference

This document describes how to connect all hardware components to the ESP32 development board.

---

## Component overview

| Component | Interface | ESP32 Pins |
|---|---|---|
| HX711 #1 (scale 1) | Digital I/O | GPIO 16 (DOUT), GPIO 17 (SCK) |
| HX711 #2 (scale 2) | Digital I/O | GPIO 32 (DOUT), GPIO 33 (SCK) |
| DS18B20 × 2 (hive temps) | 1-Wire | GPIO 4 |
| SHT4x (ambient temp + humidity) | I²C | GPIO 21 (SDA), GPIO 22 (SCL) |
| DS3231 RTC | I²C | GPIO 21 (SDA), GPIO 22 (SCL) |
| MicroSD card module | SPI | GPIO 5 (CS), GPIO 18 (SCK), GPIO 19 (MISO), GPIO 23 (MOSI) |
| Setup / reset button | Digital input | GPIO 27 → GND |
| Power input | — | VIN or 5 V pin via MP1584EN DC-DC converter |

---

## Pin mapping (quick reference)

| GPIO | Signal | Direction |
|---|---|---|
| 4 | DS18B20 1-Wire data | Bidirectional |
| 5 | SD card CS (chip select) | Output |
| 16 | HX711 #1 DOUT | Input |
| 17 | HX711 #1 SCK | Output |
| 18 | SD card SCK | Output |
| 19 | SD card MISO | Input |
| 21 | I²C SDA (RTC + SHT4x) | Bidirectional |
| 22 | I²C SCL (RTC + SHT4x) | Output |
| 23 | SD card MOSI | Output |
| 27 | Setup button (INPUT_PULLUP) | Input |
| 32 | HX711 #2 DOUT | Input |
| 33 | HX711 #2 SCK | Output |

---

## Wiring details

### Power supply

The system is designed to run from a DC power source (e.g. a 12 V solar panel, lead-acid battery, or mains adapter). An **MP1584EN** DC-DC step-down converter reduces the input voltage to 5 V for the ESP32 and peripherals.

```
DC input (+) ──→ MP1584EN IN+ ──→ MP1584EN OUT+ ──→ ESP32 VIN (5 V)
DC input (−) ──→ MP1584EN IN− ──→ MP1584EN OUT− ──→ ESP32 GND
```

Set the MP1584EN output to **5 V** using the onboard trimmer before connecting the ESP32.

> If running from USB power only (e.g. during development), skip the converter and connect via the USB port directly.

---

### HX711 load cell amplifiers

Each HX711 module connects to its load cell(s) and to the ESP32. The E+/E−/A+/A−/B+/B− terminals on the HX711 match the coloured wires from standard load cell kits (colours vary by supplier — consult your load cell datasheet).

**HX711 #1 → ESP32:**

| HX711 pin | ESP32 pin |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| DT (DOUT) | GPIO 16 |
| SCK | GPIO 17 |

**HX711 #2 → ESP32:**

| HX711 pin | ESP32 pin |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| DT (DOUT) | GPIO 32 |
| SCK | GPIO 33 |

> HX711 modules typically accept 2.7–5 V on VCC. 3.3 V works reliably with most modules and avoids level-shifting on the data lines.

---

**4-cell platform scale — wiring 3-wire load cells into a Wheatstone bridge**

A typical bathroom/kitchen-style platform scale uses four identical 3-wire load cells (one per corner). Each cell has three wires: **excitation+ (red)**, **excitation− (black)**, and **signal (white or yellow)**. Internally each cell contains a single strain gauge; you build the full Wheatstone bridge yourself by cross-connecting the four cells.

The bridge works as two voltage dividers whose midpoints feed the differential signal into the HX711. Label the cells by corner: **FL** (front-left), **FR** (front-right), **RL** (rear-left), **RR** (rear-right).

**Bridge wiring — node by node:**

| Node | Connect here | Goes to |
|---|---|---|
| E+ | Red wires of **FL** and **FR** | HX711 E+ |
| E− | Black wires of **RL** and **RR** | HX711 E− |
| A+ | Signal wire of **FR** + Black wires of **FL** and **FR** joined together¹ | HX711 A+ |
| A− | Signal wire of **FL** + Red wires of **RL** and **RR** joined together¹ | HX711 A− |

¹ See the detailed node description below — each midpoint node ties one signal wire together with two excitation wires from adjacent cells.

More precisely, the four nodes of the bridge are formed as follows:

- **E+ node:** Red (E+) of FL + Red (E+) of FR → HX711 E+
- **E− node:** Black (E−) of RL + Black (E−) of RR → HX711 E−
- **A+ node:** Signal of FR + Black (E−) of FL + Black (E−) of FR → HX711 A+
- **A− node:** Signal of FL + Red (E+) of RL + Red (E+) of RR → HX711 A−

> **Why this works:** Each 3-wire load cell's signal wire is the midpoint of its internal half-bridge. By cross-connecting the four cells this way, you form a complete Wheatstone bridge where all four gauges contribute to the output — giving you the same result as a dedicated 6-wire full-bridge load cell, without a combinator board.

**Practical tips:**

- Use all four cells from the **same batch** if possible. Mismatched cells cause zero-point drift and non-linearity.
- Keep all signal wire runs **equal in length** to balance impedance.
- Twist or bundle each cell's wires together and keep them away from mains wiring to reduce noise.
- After wiring, check the unloaded differential voltage at A+/A− (should be close to 0 V) before connecting to the HX711.
- The HX711's onboard averaging and the software tare handle any small residual offset.

---

### DS18B20 temperature sensors (1-Wire)

Both DS18B20 sensors share the same single-wire data bus. Up to dozens of sensors can be on one bus — the firmware addresses each by its unique 64-bit ROM ID.

```
DS18B20 VDD  ──→ 3.3 V
DS18B20 GND  ──→ GND
DS18B20 DATA ──→ GPIO 4  (also connect a 4.7 kΩ pull-up resistor from DATA to 3.3 V)
```

Both sensors connect **in parallel** to the same three lines. If using waterproof probes with cables, the wires are typically:

| Wire colour | DS18B20 pin |
|---|---|
| Red | VDD |
| Black | GND |
| Yellow / White | DATA |

> The **4.7 kΩ pull-up resistor** from DATA to 3.3 V is required. Without it the bus will not operate reliably, especially with longer cables. One resistor covers both sensors on the shared bus.

---

### SHT4x (ambient temperature & humidity)

The SHT4x uses I²C. It shares the bus with the DS3231 RTC.

| SHT4x pin | ESP32 pin |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

The SHT4x I²C address is `0x44` (default). The DS3231 is at `0x68`. Both coexist on the same bus without conflict.

---

### DS3231 RTC (real-time clock)

| DS3231 pin | ESP32 pin |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

The DS3231 module typically includes a CR2032 coin cell holder for backup power. Install a CR2032 to keep the clock running when the main supply is off.

> **I²C pull-ups:** Most breakout modules for the SHT4x and DS3231 include 4.7 kΩ pull-up resistors on SDA and SCL. If using bare chips, add your own pull-ups (4.7 kΩ from SDA/SCL to 3.3 V).

---

### MicroSD card module (SPI)

The SD module uses the ESP32's VSPI bus.

| SD module pin | ESP32 pin |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| CS | GPIO 5 |
| SCK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |

Use a **FAT32**-formatted microSD card. The firmware creates a `measurements/` directory and buffers data there when offline.

---

### Setup / factory-reset button

A momentary normally-open pushbutton connects between GPIO 27 and GND. The firmware configures GPIO 27 as `INPUT_PULLUP`, so no external resistor is needed.

```
GPIO 27 ──→ Button ──→ GND
```

**Short press:** starts the Wi-Fi provisioning portal.  
**Long press (5 s):** factory-resets all stored settings.

The button is optional — if not installed, the provisioning portal can still be triggered via a remote command.

---

## I²C bus summary

Both I²C devices (DS3231 and SHT4x) share GPIO 21 / GPIO 22. Their addresses are distinct so there is no conflict:

| Device | I²C address |
|---|---|
| DS3231 RTC | 0x68 |
| SHT4x | 0x44 |

---

## Assembly tips

- Mount all electronics in an **IP67-rated enclosure** (at least 150 × 150 mm). Drill cable glands for sensor cables.
- Run DS18B20 probe cables through the gland into the hive body. Seal the entry point with silicone to prevent bee ingress.
- Place the SHT4x sensor outside the main electronics box but under a small radiation shield to get accurate ambient readings away from heat generated by the ESP32.
- Use ferrule-crimped wires or solder connections inside the box — push-in terminals can vibrate loose outdoors.
- Label the HX711 modules (scale 1 / scale 2) to avoid confusion during calibration.