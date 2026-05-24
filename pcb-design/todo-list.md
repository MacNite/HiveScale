# HiveScale V0 breakout PCB TODO list

## Prototype validation

- [x] Make first prototype to test the design.
- [x] Bring up the board on the bench before connecting sensors or a modem.
- [x] Verify 3.3 V and 5 V rails under expected load.
- [x] Verify ESP32 boot and serial monitor access.
- [x] Verify I2C bus with RTC, SHT40/SHT4x, INA219, and MAX17048 installed.
- [x] Verify SD card initialization with the current firmware mapping: MISO GPIO23, MOSI GPIO19.
- [x] Verify HX711 channel 1 and channel 2 raw readings and calibration.
~~- [ ] Verify SIM7080G UART, APN attach, measurement upload, and power shutdown.~~
- [ ] Measure sleep current with and without SIM7080G installed.

## Power and LTE modem reliability

~~- [ ] Add capacitors for LTE modem peak-current support.~~
~~- [ ] Choose bulk capacitor value and footprint near the SIM7080G connector.~~
~~- [ ] Add small ceramic decoupling close to modem supply pins.~~
~~- [ ] Verify modem power path can handle transmit current pulses without brownouts.~~
~~- [ ] Decide whether GPIO14 should be documented as PWRKEY, regulator enable, or selectable by jumper.~~
~~- [ ] Add or improve test points for modem power, PWRKEY/enable, TX, RX, and GND.~~

## Layout and grounding

- [ ] Optimize GND vias.
- [ ] Improve high-current return paths for solar, battery, regulator, and modem sections.
- [ ] Keep HX711 and load-cell signal routing away from LTE and switching-regulator noise.
- [ ] Review trace widths for solar input, charger path, LiPo path, and modem supply.
- [ ] Add clear net labels and silkscreen labels for all external terminal blocks.
- [ ] Optimize layout after first prototype test results.

## Mechanical design

- [ ] Optimize mounting.
- [ ] Define mounting-hole positions for the target enclosure.
- [ ] Check clearance for terminal blocks, SD module, ESP32 USB port, antenna, and cable glands.
- [ ] Add silkscreen orientation marks for pluggable modules.
- [ ] Confirm connector spacing for ferrules and field wiring.

## Charging path improvements

- [ ] Add the possibility to charge the LiPo from USB/5 V instead of solar only.
- [ ] Decide whether USB/5 V charging should be jumper-selectable, OR-ed, or use a dedicated charger/power-path IC.
- [ ] Add reverse-current protection where needed.
- [ ] Document allowed charging sources and maximum current.
- [ ] Validate thermal behavior in an IP-rated enclosure.

## Documentation before release

- [ ] Add photos of the assembled prototype.
- [ ] Add KiCad screenshots or exported PDF schematic.
- [ ] Add a BOM with exact connector/module part numbers.
- [ ] Add a field wiring diagram for load-cell terminals.
- [ ] Add notes for the chosen SIM7080G breakout board pin order.
- [ ] Update `README.md` and `docs/wiring.md` if prototype testing changes any pin assignments.
