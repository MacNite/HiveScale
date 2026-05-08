# HiveScale optional off-grid integration

This branch keeps all off-grid hardware disabled by default and enables each module per device from `firmware/include/secrets.h`.

## Enable modules

Use numeric `0` / `1` values because the firmware uses preprocessor `#if` checks.

```cpp
#define ENABLE_INA219_SOLAR      1
#define ENABLE_MAX17048_BATTERY  1
#define ENABLE_SIM7080G          1

#define SIM7080G_APN             "your-apn"
#define SIM7080G_USER            ""
#define SIM7080G_PASS            ""
#define SIM7080G_PIN             ""

#define SIM7080G_RX_PIN          26
#define SIM7080G_TX_PIN          25
#define SIM7080G_PWRKEY_PIN      -1
#define SIM7080G_POWER_EN_PIN    -1
```

## Wiring defaults

- I2C is unchanged: SDA `21`, SCL `22`.
- INA219 default address: `0x40`.
- MAX17048 uses its fixed I2C address through the SparkFun library.
- SIM7080G UART defaults to ESP32 UART2 on RX `26`, TX `25`.
- If your SIM7080G board exposes `PWRKEY` or a regulator enable pin, set the GPIOs in `secrets.h`; otherwise leave them as `-1`.

## Transport behavior

When `ENABLE_SIM7080G` is `1`:

- measurement upload uses SIM7080G
- command/config polling uses SIM7080G
- time sync uses cellular network time
- WiFi station mode is not used for normal operation
- provisioning AP mode still uses WiFi
- firmware OTA over cellular is disabled by default (`CELLULAR_OTA_ENABLED 0`)

## Measurement payload additions

When the matching modules are enabled, the firmware adds:

- `network_transport`, `cellular_ok`, `cellular_csq`
- `solar_monitor_ok`, `solar_bus_voltage_v`, `solar_shunt_voltage_mv`, `solar_load_voltage_v`, `solar_current_ma`, `solar_power_mw`
- `battery_monitor_ok`, `battery_voltage`, `battery_voltage_v`, `battery_soc_percent`, `battery_alert`

The server model now preserves optional off-grid fields in `raw_json`. Add dedicated database columns or HivePal UI fields later if you want them indexed or displayed directly.
