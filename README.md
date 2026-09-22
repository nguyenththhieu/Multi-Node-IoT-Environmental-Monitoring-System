# Multi-Node IoT Environmental Monitoring System
ESP32-based multi-node system for remote power and environmental monitoring using 433 MHz LoRa and Wi-Fi.
---

## 📌 Overview
An IoT-based Wireless Sensor Network (WSN) system for remote monitoring of electricity consumption and environmental conditions, using LoRa RF (433MHz) instead of WiFi/Bluetooth/Zigbee to overcome range limits, interference, and poor wall penetration. Built as a Master–Slave architecture with 1 Gateway (Master) and 1 measurement/actuation node (Slave), allowing users to monitor and control devices remotely via a web interface.
## ✨ Features
* Real-time power metering: voltage, current, power, energy (kWh)

* Environmental monitoring: temperature, humidity, light intensity

* Gas leak/smoke detection with local alarm

* Overload protection with automatic full relay cutoff

* Auto (light-based) and Manual lighting control

* Centralized web dashboard for monitoring, alerts, and remote control

## 🏗️ System Architecture
Star-topology Master–Slave WSN over LoRa:

* Slave node: reads sensors, executes local safety actions immediately (no need to wait for Master), sends data packets, receives/executes control commands

* Master node (Gateway): listens for LoRa packets, decodes and pushes data to the Web Server, relays user commands from the web UI back to the Slave via LoRa

* Master-controlled data flow prevents RF collisions and allows future scaling to multiple Slaves without redesigning the Master

<img width="1299" height="1091" alt="block_diagram" src="https://github.com/user-attachments/assets/3100aa8f-67f4-4ff1-84d1-46efa9881f00" />


## 🔧 Hardware
* MCU: ESP32 (NodeMCU) on both nodes — dual-core, WiFi built-in, 3 hardware UARTs

* RF module: Ebyte E32-433T20D (SX1278 chip, 433MHz, 100mW/20dBm, UART interface)

* Power meter: PZEM-004T + CT clamp, optically isolated, UART/Modbus-RTU

* Sensors: DHT11 (temp/humidity), MQ-2 (gas), LDR (ambient light)

* Actuators: 2-channel relay module, buzzer (driven via C1815 NPN transistor), status LEDs

* Power supply: dual LM2576 buck converters (5V/3.3V), sized for ~990mA slave load with >3x safety margin
## 💻 Software & Technologies
* Embedded C/C++ firmware on ESP32, modular design

* Slave main loop: read sensors → safety check (highest priority) → Auto/Manual light logic → parse incoming LoRa commands → pack data into a single comma-delimited frame (terminated with \n) → transmit

* Master main loop: listen on LoRa → decode & push to Web Server → serve web UI/handle button events → send commands via LoRa; tracks last-received timestamp to flag Slave disconnection (>10s timeout)

* Lightweight Web Server hosted directly on the ESP32 Master, with login authentication
## 📡 Communication
* UART-based throughout: LoRa module on UART2, PZEM-004T on UART1 — avoids SoftwareSerial delays, runs in parallel without blocking

* Line-of-sight range tested stable beyond 100m with no packet loss

* Non-line-of-sight test through 2 concrete walls: still reliable, no dropouts

* Simple text-based command/data protocol between Master and Slave over LoRa
## 🎛️ Control Logic
* AUTO mode: relay for lighting is switched automatically based on LDR threshold

* MANUAL mode: user directly toggles relay via web buttons, command sent over LoRa

* Safety override (independent of mode, highest priority): if gas concentration or power draw exceeds a set threshold, the Slave immediately sounds the buzzer, cuts all relays, and reports the alert to the Master
## 🌐 Web Dashboard
* Served directly from ESP32 (Master) over WiFi — no external cloud required

* Pages: Login, Home (system status), Overview (environmental data with live line charts), Power (voltage/current/power charts), Control (Auto/Manual toggle + relay buttons)

* Auto-refreshing data without full page reloads; "Optimistic UI" for instant button feedback

* Full-screen red alert banner shown when a safety threshold is breached
## 📊 Results
* Both nodes built and validated as a working prototype

* LoRa link: stable, no packet loss at >100m LOS and through 2 concrete walls (NLOS)

* Measurement accuracy: PZEM-004T within ~2% of a reference multimeter; DHT11 within ~±3% of a reference thermometer

* Control latency: ~1s from web button press to relay actuation

* Safety cutoff verified with a live smoke test at the 250ppm threshold

* Web server stable with 1–2 concurrent clients; no cloud/history database yet (real-time only)
