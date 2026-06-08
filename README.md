# IOT-based-smart-security-lock-system
An ESP32 smart security lock system featuring multi-modal access control via a 4x4 keypad, Bluetooth Serial, and a custom WiFi Web Dashboard. Built in C++, it integrates an I2C 16x2 LCD, dual status LEDs, an optoisolated relay driving a 12V solenoid lock, brute-force protection lockout logic, and local NTP event logging.
# IoT-Based Smart Security and Automation Node

An advanced, multi-modal smart access control system built on the dual-core **ESP32 development platform**. This project implements a hardware-software co-design providing local physical authentication alongside long-range and short-range wireless security layers. 

Developed as the Final Project for the **Introduction to Embedded Systems (IES)** course under the **Department of Computer Engineering** at **Pak-Austria Fachhochschule: Institute of Applied Sciences and Technology (PAF-IAST)**.

### 👥 Authors
- **Hoor Aman** (Reg No: `B24F0225CE039`)
- **Submitted To:** Engr. Rafiullah

---

## 🌟 Features
- **Multi-Channel Access Control:** Physical 4x4 Matrix Keypad input, Classic Bluetooth Serial interface, and a local WiFi-hosted web dashboard.
- **Visual Feedback System:** Real-time updates delivered via an I2C 16x2 LCD display and automated status LEDs (Green = Unlocked, Red = Locked).
- **Brute-Force Protection:** Progressive rate-limiting cooldown periods (10s, 20s, and 30s delays) upon consecutive failed password entries.
- **Permanent Software Lockout:** Triggers an automatic system block after 4 failed attempts, requiring remote administrator clearance via Web or Bluetooth.
- **Persistent Localized Event Logging:** Maintains a scrolling cache of the last 20 events with live timestamps synchronized over Network Time Protocol (NTP).

---

## 🛠️ Hardware Specifications & Pin Mapping

### Component Stack:
- NodeMCU ESP-WROOM-32 
- 16x2 LCD Character Display (with PCF8574 I2C adapter)
- 4x4 Membrane Matrix Keypad
- 5V SPDT Relay Module & 12V DC Solenoid Door Lock
- LED Indicators (Red & Green)

### Connection Matrix:
| Source Component | Pin Name | ESP32 GPIO | Notes / Description |
| :--- | :--- | :--- | :--- |
| **I2C LCD Module** | SDA / SCL | **GPIO 21 / GPIO 22** | I2C Data & Clock Lines |
| **LED Indicators** | Green / Red Anode | **GPIO 18 / GPIO 19** | Output status lines |
| **Relay Module** | IN | **GPIO 4** | Logic LOW activates solenoid |
| **4x4 Keypad** | Row 1 - 4 | **GPIO 25, 26, 32, 33** | Matrix Row Output Controls |
| | Col 1 - 4 | **GPIO 13, 12, 14, 27** | Matrix Column Input Sensing |

---

## 💻 Software Architecture & Bluetooth Protocols

The application utilizes object-oriented C++ modules within the Arduino IDE. It manages asynchronous web server listening, Bluetooth polling, and matrix-scanning routines simultaneously.

### 📲 Wireless Interface Instructions:
- **Cyberpunk Local Web Dashboard (WiFi):** Navigate to the ESP32's assigned IP address on your browser to access the custom dark-themed remote dashboard UI.
- **Serial Bluetooth App Terminal:** Connect via a mobile Bluetooth terminal app to the node **"SmartDoorLock"**.
  - `U` / `u` : Remote Unlock Command
  - `L` / `l` : Remote Lock Command
  - `S` / `s` : Stream Live Node Status Metrics
  - `R` / `r` : Overwrite Error Cache & Unblock System
