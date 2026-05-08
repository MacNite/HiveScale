# HiveScale optional off-grid mode

Off-grid mode adds cellular transport and power telemetry to the normal HiveScale measurement cycle. It is designed for installations where Wi-Fi is not available or where the device should run from solar and LiPo power.

All off-grid hardware is disabled by default. Enable modules per device in `firmware/include/secrets.h`.

---

## Feature flags

Use numeric `0` / `1` values because the firmware uses preprocessor `#if` checks.

```cpp
#define ENABLE_INA219_SOLAR      1
#define ENABLE_MAX17048_BATTERY  1
#define ENABLE_SIM7080G          1
#define CELLULAR_OTA_ENABLED     0
```

| Flag | Effect |
|---|---|
| `ENABLE_INA219_SOLAR` | Compiles in INA219 support and adds solar/load telemetry fields |
| `ENABLE_MAX17048_BATTERY` | Compiles in MAX17048 support and adds LiPo telemetry fields |
| `ENABLE_SIM7080G` | Uses SIM7080G cellular transport for normal device traffic |
| `CELLULAR_OTA_ENABLED` | Allows firmware OTA over cellular; keep disabled unless you accept the data cost |

---

## SIM7080G configuration

```cpp
#define SIM7080G_APN             "your-apn"
#define SIM7080G_USER            ""
#define SIM7080G_PASS            ""
#define SIM7080G_PIN             ""

#define SIM7080G_BAUD            115200
#define SIM7080G_RX_PIN          26
#define SIM7080G_TX_PIN          25

// Choose the control style that matches your modem board.
#define SIM7080G_PWRKEY_PIN      14
#define SIM7080G_POWER_EN_PIN    -1
#define SIM7080G_POWER_EN_ACTIVE_HIGH 1

#define SIM7080G_NETWORK_TIMEOUT_MS 180000UL
#define SIM7080G_GPRS_TIMEOUT_MS    60000UL
#define SIM7080G_CONNECT_RETRIES    3
#define SIM7080G_RETRY_BACKOFF_MS   5000UL
```

### UART and power pins

| Signal | Default / breakout PCB mapping | Notes |
|---|---|---|
| ESP32 RX from modem TX | GPIO26 | `SIM7080G_RX_PIN` |
| ESP32 TX to modem RX | GPIO25 | `SIM7080G_TX_PIN` |
| Modem PWRKEY / power control | PCB exposes GPIO14 | Set either `SIM7080G_PWRKEY_PIN` or `SIM7080G_POWER_EN_PIN` depending on board wiring |

The firmware supports two power-control styles:

- **PWRKEY pulse control:** set `SIM7080G_PWRKEY_PIN`. The pin is driven open drain and pulsed for wake, reset, and power-off.
- **Regulator enable control:** set `SIM7080G_POWER_EN_PIN`. The firmware enables the modem before attach and disables it before sleep.

Use only the style that matches the modem breakout. Leave unused control pins as `-1`.

---

## Transport behavior

When `ENABLE_SIM7080G` is `1`:

- Measurement upload uses SIM7080G.
- Config polling uses SIM7080G.
- Command polling uses SIM7080G.
- Server time sync uses cellular network time when available.
- Wi-Fi station mode is skipped during normal operation to save power.
- The Wi-Fi provisioning AP remains available from the setup button.
- OTA over cellular is skipped unless `CELLULAR_OTA_ENABLED` is `1`.

The firmware reports `network_transport: "sim7080g"`, `cellular_ok`, `cellular_csq`, and an approximate `rssi_dbm` derived from CSQ. Wi-Fi builds report `network_transport: "wifi"`.

---

## Solar telemetry with INA219

Enable:

```cpp
#define ENABLE_INA219_SOLAR 1
#define INA219_I2C_ADDRESS  0x40
```

Wiring uses the shared I2C bus:

| INA219 pin | ESP32 pin |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

Measurement fields:

| Field | Description |
|---|---|
| `solar_monitor_ok` | INA219 detected and readable |
| `solar_bus_voltage_v` | Bus voltage in volts |
| `solar_shunt_voltage_mv` | Shunt voltage in millivolts |
| `solar_load_voltage_v` | Calculated bus + shunt voltage |
| `solar_current_ma` | Current in milliamps |
| `solar_power_mw` | Power in milliwatts |

---

## LiPo telemetry with MAX17048

Enable:

```cpp
#define ENABLE_MAX17048_BATTERY 1
#define MAX17048_ALERT_PERCENT  20
```

Wiring uses the shared I2C bus plus the battery sense connection required by the breakout board.

Measurement fields:

| Field | Description |
|---|---|
| `battery_monitor_ok` | MAX17048 detected and readable |
| `battery_voltage_v` | Battery voltage in volts |
| `battery_soc_percent` | Battery state-of-charge percentage |
| `battery_alert` | Alert flag from the fuel gauge |

The backend also returns `battery_voltage` for backwards compatibility, mapped from `battery_voltage_v` when available.

---

## Backend storage and API behavior

The backend now stores off-grid telemetry in dedicated columns as well as in `raw_json`.

The startup schema and `server/migrations/001_offgrid_telemetry.sql` include columns for:

- battery state-of-charge, voltage, monitor status, and alert
- solar voltage/current/power values
- transport and cellular status
- calibration mode state
- boot count
- time source

These fields are returned by `/api/v1/measurements/latest` and the HivePal app measurement endpoints.

---

## Power-saving behavior

Normal operation is one wake cycle:

1. Wake from deep sleep or reset.
2. Power up sensors and HX711 modules.
3. Measure weights, temperatures, and optional power telemetry.
4. Connect over Wi-Fi or SIM7080G.
5. Upload the measurement and retry cached rows.
6. Poll config and commands.
7. Power down HX711, SD, cellular modem, and optional monitors where supported.
8. Enter deep sleep until the next send interval.

In cellular mode, the firmware attempts a hardware reset if modem initialization or network attach fails and a PWRKEY or power-enable pin is configured.

---

## Practical notes

- Test the SIM/APN with serial monitor output before relying on unattended operation.
- Add bulk capacitance close to the LTE modem power input; LTE attach and transmit peaks can brown out weak supplies.
- Keep LTE antenna and high-current modem wiring away from HX711 and load-cell wiring.
- Keep OTA over cellular disabled unless you have tested the data plan, network coverage, and firmware binary size.
- Use the SD card cache as protection against temporary cellular outages, not as a replacement for regular connectivity checks.
