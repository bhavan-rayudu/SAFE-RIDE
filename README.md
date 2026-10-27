# SAFE-RIDE
ESP32-based accident detection and emergency alert system with GPS location tracking and LTE communication.
# SAFE-RIDE – Accident Detection and Emergency Alert System

## Overview
SAFE-RIDE is an ECE student project prototype designed to detect potential vehicle accidents and notify emergency contacts with location information.

## Problem Statement
After a road accident, delays in notifying emergency responders can affect how quickly help arrives. SAFE-RIDE explores an embedded-system approach to sending alerts automatically.

## Main Features
- Accident-like impact detection using an MPU6050 sensor
- GPS-based location acquisition using a NEO-6M module
- LTE communication using an A7670C modem
- Emergency SMS alerts with location information, when GPS and network are available
- ESP32-based system control

## Hardware Components
- ESP32 development board
- MPU6050 accelerometer and gyroscope
- NEO-6M GPS module
- A7670C LTE modem
- OLED SSD1306 display
- Buzzer, LED and push button
- Suitable power supply and wiring

## Working Principle
1. The MPU6050 provides motion and acceleration readings.
2. The ESP32 evaluates sensor readings against a configured threshold.
3. The GPS module provides coordinates when a valid fix is available.
4. The LTE modem can send an emergency message to configured contacts when cellular service is available.
5. The display, buzzer and LED provide local status indications.

## Project Status
Prototype developed and demonstrated at a project expo. The project received 3rd prize. Features and performance depend on testing conditions, GPS reception, cellular coverage and power stability.

## Safety Note
SAFE-RIDE is an educational prototype, not a certified vehicle safety device. It must not replace emergency services or a certified automotive system. Test responsibly and do not rely on it as the only way to request help.

## Future Improvements
- Improve false-trigger handling and accident detection validation
- Add reliable backup power monitoring
- Improve GPS and cellular connection status reporting
- Conduct more field tests and document measured results

## Author
Bhavan Rayudu – ECE Diploma Student

## Disclaimer
This repository documents an educational engineering project. No guarantee of emergency message delivery or rescue response is provided.
