# 🏥 Health Monitor — Vibration & Fall Detection Wearable

> IoT wearable system for real-time monitoring of heart rate, blood oxygen saturation (SpO₂), and fall/vibration detection using ESP32, MAX30100, and SW1801P sensors.

---

## 📋 Table of Contents

- [About the Project](#about-the-project)
- [System Architecture](#system-architecture)
- [Hardware Requirements](#hardware-requirements)
- [Wiring](#wiring)
- [Software & Libraries](#software--libraries)
- [MQTT Topics](#mqtt-topics)
- [Setup & Installation](#setup--installation)
- [Node-RED Integration](#node-red-integration)
- [How It Works](#how-it-works)
- [Author](#author)

---

## 📖 About the Project

This project implements a low-cost IoT wearable health monitoring system designed for elderly patients with cardiac conditions who wish to exercise safely.

The system continuously monitors:
- **Heart Rate (BPM)** — via MAX30100 pulse oximeter
- **Blood Oxygen Saturation (SpO₂)** — via MAX30100
- **Falls & Vibration Events** — via SW1801P vibration sensor

Data is transmitted in real time via **MQTT** to a **Raspberry Pi** running **Node-RED**, which forwards it to a **Node.js REST API** for storage in **SQLite** and visualization on a **web dashboard**.

A local **buzzer alert** is triggered when heart rate exceeds the configured limit (100 BPM).

---

## 🏗 System Architecture

```
┌──────────────────────────────┐
│         Sensing Layer        │
│  MAX30100 · SW1801P · Buzzer │
└──────────────┬───────────────┘
               │ GPIO / I2C
               ▼
┌──────────────────────────────┐
│      ESP32 (Wearable)        │
│  Read · Process · Publish    │
└──────────────┬───────────────┘
               │ Wi-Fi / MQTT
               ▼
┌──────────────────────────────┐
│       MQTT Broker            │
│       Mosquitto              │
└──────────────┬───────────────┘
               │ MQTT
               ▼
┌──────────────────────────────┐
│   Raspberry Pi + Node-RED    │
│       (IoT Gateway)          │
└──────────────┬───────────────┘
               │ HTTP POST
               ▼
┌──────────────────────────────┐
│    Node.js Server + SQLite   │
│         REST API             │
└──────────────┬───────────────┘
               │ HTTP GET
               ▼
┌──────────────────────────────┐
│        Web Dashboard         │
│  Real-time · History · Alerts│
└──────────────────────────────┘
```

---

## 🔧 Hardware Requirements

| Component | Description |
|---|---|
| ESP32 | Main microcontroller (Wi-Fi + GPIO) |
| MAX30100 | Pulse oximeter — Heart Rate + SpO₂ |
| SW1801P | Vibration sensor — Fall & tremor detection |
| Buzzer | Local audio alert |
| Breadboard + Jumper wires | Prototyping |

---

## 🔌 Wiring

### MAX30100 → ESP32

| MAX30100 Pin | ESP32 Pin | Function |
|---|---|---|
| VCC | 3V3 | Power (3.3V) |
| GND | GND | Ground |
| SCL | GPIO 27 | I2C Clock |
| SDA | GPIO 32 | I2C Data |

### SW1801P → ESP32

| SW1801P Pin | ESP32 Pin | Function |
|---|---|---|
| VCC | 3V3 | Power (3.3V) |
| GND | GND | Ground (shared rail) |
| DO | GPIO 26 | Digital output |

### Buzzer → ESP32

| Buzzer Pin | ESP32 Pin | Function |
|---|---|---|
| Signal | GPIO 25 | Control (HIGH = active) |
| GND | GND | Ground (shared rail) |

> ⚠️ **Note:** All GND pins share the breadboard negative rail — only one GND pin from the ESP32 is needed.

---

## 📦 Software & Libraries

### Arduino IDE Libraries

| Library | Author | Purpose |
|---|---|---|
| `Wire.h` | Built-in | I2C communication |
| `WiFi.h` | Built-in | Wi-Fi connectivity |
| `PubSubClient` | Nick O'Leary (v2.8+) | MQTT client |
| `MAX30100_PulseOximeter` | OXullo Intersecation | MAX30100 sensor control |

> ⚠️ **PubSubClient must be version 2.8 or higher.**

### Raspberry Pi

- **Mosquitto** — MQTT broker
- **Node-RED** — IoT gateway and flow editor
- **Node.js** — REST API server
- **SQLite** — Data persistence

---

## 📡 MQTT Topics

| Topic | Publisher | Description |
|---|---|---|
| `healthsensor` | ESP32 | JSON with heartRate and spO2 |
| `healthsensor/beat` | ESP32 | Beat detected event |
| `healthsensor/queda` | ESP32 | Vibration level and count |

### Message Format

**`healthsensor`**
```json
{"heartRate": 72.5, "spO2": 98.0}
```

**`healthsensor/queda`**
```json
{"nivel": "FORTE", "contagem": 95}
```

### Vibration Levels

| Count (pulses/s) | Level |
|---|---|
| 0 | No vibration |
| 1 – 19 | WEAK |
| 20 – 79 | MEDIUM |
| 80 – 149 | STRONG |
| ≥ 150 | VERY STRONG |

---

## ⚙️ Setup & Installation

### 1. Configure Wi-Fi and MQTT in the firmware

Edit the following lines in the `.ino` file:

```cpp
const char* ssid        = "YOUR_WIFI_NAME";
const char* password    = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "RASPBERRY_PI_IP";
const int   mqtt_port   = 1883;
```

### 2. Install Mosquitto on Raspberry Pi

```bash
sudo apt install mosquitto mosquitto-clients -y
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

Configure to accept external connections:

```bash
sudo nano /etc/mosquitto/mosquitto.conf
```

Add at the end:
```
listener 1883 0.0.0.0
allow_anonymous true
```

Restart:
```bash
sudo systemctl restart mosquitto
```

### 3. Upload firmware to ESP32

1. Open the `.ino` file in Arduino IDE
2. Select board: **NodeMCU-32S**
3. Select the correct COM port
4. Click **Upload**

### 4. Monitor serial output

Open Serial Monitor at **115200 baud** — you should see:

```
Conectando WiFi... OK
Oximetro... OK
MQTT... OK
HR: 72.50 bpm | SpO2: 98.00 %
```

---

## 🔴 Node-RED Integration

### Access Node-RED

```
http://RASPBERRY_PI_IP:1880
```

### Configure MQTT Input Node

1. Drag an **mqtt in** node to the canvas
2. Set **Server:** `localhost` / **Port:** `1883`
3. Set **Topic:** `healthsensor`
4. Connect to a **debug** node and **Deploy**

### Test Mosquitto from terminal

Subscribe to all topics:
```bash
mosquitto_sub -h localhost -p 1883 -t "#" -v
```

---

## ⚙️ How It Works

1. The ESP32 reads heart rate and SpO₂ from the MAX30100 every second using a dedicated **FreeRTOS task** at ~1kHz
2. The SW1801P vibration sensor counts pulses in 1-second windows
3. Data is published via MQTT to the Mosquitto broker on the Raspberry Pi
4. Node-RED subscribes to MQTT topics, validates messages and forwards to the REST API via HTTP POST
5. Node.js API persists data in SQLite
6. The web dashboard displays real-time data and historical records
7. If heart rate exceeds **100 BPM**, the local buzzer is activated

---

## 👩‍💻 Author

**Michelle Bastos Silva**
Mestrado em Internet das Coisas
Escola Superior de Tecnologia e Gestão de Beja — IPBeja
2025/2026

---

## 📄 License

This project was developed for academic purposes at Instituto Politécnico de Beja.
