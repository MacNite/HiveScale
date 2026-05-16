###the V0 has major issues in regard to power delivery, battery and solar charging as well as monitoring. Do not order these pcbs. a new version should be available around 07/2026

# HiveScale V0 breakout PCB - known Issues

- The V0 PCB wires VIN on the ESP32. This does not work for most cheap boards as the VIN is directly connected to the DC/DC converter and meant for 5V in only.
- [ ] Preliminary fix: Remove the VIN PIN on the ESP32 and solder a wire from on the back of the PCB from VIN to 3V3.

<img src="https://github.com/MacNite/HiveScale/blob/main/pcb-design/pictures/PCB_V0_Vin_fix_A.jpg" alt="ESP32 front" height="200"/><img src="https://github.com/MacNite/HiveScale/blob/main/pcb-design/pictures/PCB_V0_Vin_fix_B.jpg" alt="ESP32 back" height="200"/><img src="https://github.com/MacNite/HiveScale/blob/main/pcb-design/pictures/PCB_V0_Vin_fix_C.jpg" alt="PCB back" height="200"/>


- tp63020 does Not fit properly, footprint wrong
- output of battery wired wrong (should go to VIN on TP63020 but goes to 3V3)
- [ ] Preliminary fix: solder a wire between the positive pole of the battery (after the MAX17048) and connect it directly to VIN on the TP63020