# BLE notification throughput client (temporary)

Bench tool for [HiveInside issue #71](https://github.com/MacNite/HiveInside/issues/71),
phase 0e: measure how fast a HiveInside node can push GATT **notifications** to
an ESP32, which is the transport an on-request audio recording would use.

This is the measurement that decides the design. The OTA relay's ≈1–1.5 kB/s is
not a usable proxy — it measures writes *with response* whose ATT callback does a
synchronous RRAM write, with a TLS download competing for the same radio. See
`docs/ble-throughput-spike.md` in the HiveInside repository for the full
rationale, the protocol, and how to read the result.

**A standalone PlatformIO project on purpose.** It is not under `firmware/`, so
nothing here can be linked into a hub image by accident. Flash it to a *spare*
ESP32 — not to a deployed hub.

## Prerequisites

The HiveInside node must be running an image built with
`throughput-spike.conf`; a normal image has no throughput service and the client
will say so.

## Run

```sh
cd tools/ble-throughput
pio run -e esp32dev     -t upload -t monitor    # classic ESP32: BLE 4.2, 1M PHY only
pio run -e xiao_esp32c6 -t upload -t monitor    # C6: 2M PHY available
```

Run both. The two boards can legitimately differ by about a factor of two, and
the hub fleet has both.

## Knobs

All in `platformio.ini` `build_flags`:

| Flag | Meaning |
|---|---|
| `TP_TARGET_MAC` | address of the node; empty scans for `TP_NAME_PREFIX` |
| `TP_DURATION_S` | seconds to blast (1–60) |
| `TP_PAYLOAD` | notification payload, 20–244 bytes |
| `TP_INTERVAL_UNITS` | connection interval in 1.25 ms units; 12 = 15 ms, 0 = leave alone |
| `TP_WIFI_LOAD` | 1 to run a concurrent WiFi download — the coexistence case |

The other knob lives on the node: `CONFIG_BT_BUF_ACL_TX_COUNT` in
`throughput-spike.conf` controls how many notifications can be queued into one
connection event.

## Delete when done

Record the numbers in issue #71, then remove this directory along with the
HiveInside side of the spike.
