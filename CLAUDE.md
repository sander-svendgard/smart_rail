# smart_rail — CLAUDE.md

## Prosjektbeskrivelse

Utvikling av en **digital tvilling** for en fysisk modelljernbane. Systemet kombinerer sanntids sensordata med en 3D-modell i Unity for å overvåke togdrift og detektere avvik i skinnene (prediktivt vedlikehold).

## Arkitektur

- **3 ESP32-kort**, hvert med 2 HC-SR04 ultrasoniske sensorer (6 sensorer totalt)
- **1 ICM-20948 IMU** (9-akse: akselerometer + gyroskop + magnetometer) koblet via I2C — brukes til å detektere avvik langs skinnene og fartsendringer
- Sensor data sendes via **MQTT** til en broker som Unity leser fra
- Unity bruker dataen til å oppdatere en **3D-modell av togbanen i sanntid**
- Formålet er posisjonssporing av toget og prediktivt vedlikehold (avviksdeteksjon i skinnene)

## Instruksjoner for sensorlogikk

Sensorene skal aktiveres **sekvensielt i rekkefølge 1 → 2 → 3 → 4 → 5 → 6**.

- Sensor 2 skal **ikke** kunne aktiveres før sensor 1 er bekreftet aktivert
- Sensor 3 skal **ikke** kunne aktiveres før sensor 2 er bekreftet aktivert
- Osv. for alle 6 sensorer

Dette sikrer at systemet vet **nøyaktig hvor toget befinner seg** på banen til enhver tid, og at MQTT-meldingene til Unity reflekterer korrekt posisjon.

## Boardtildeling (identifisert via MAC-adresse ved oppstart)

| ESP32 | MAC-adresse         | Sensorer | sensorOffset | Rolle        |
|-------|---------------------|----------|--------------|--------------|
| #1    | 10:97:BD:D4:DE:B0   | 1–2      | 0            | Første, starter alltid aktiv |
| #2    | 4C:C3:82:CC:E0:EC   | 3–4      | 2            | Venter på `togbane/gruppe1/ferdig` |
| #3    | A4:F0:0F:67:19:EC   | 5–6      | 4            | Venter på `togbane/gruppe2/ferdig` |

## MQTT-topics

| Topic                          | Innhold                                      |
|-------------------------------|----------------------------------------------|
| `togbane/sensor/<N>/distanse` | Avstandsmåling i cm (float)                  |
| `togbane/sensor/<N>/status`   | `WAITING` / `ACTIVE` / `DONE` / `SLEEP`      |
| `togbane/gruppe1/ferdig`      | ESP32 #1 signalerer til #2 (`TOG_PASSERT`)   |
| `togbane/gruppe2/ferdig`      | ESP32 #2 signalerer til #3 (`TOG_PASSERT`)   |
| `togbane/status`              | Global status (`RESET` / `GRUPPE1_FERDIG` / `GRUPPE2_FERDIG` / `TOG_PASSERT`) |
| `togbane/gyro/x`              | Rotasjonshastighet X-akse i °/s (float)      |
| `togbane/gyro/y`              | Rotasjonshastighet Y-akse i °/s (float)      |
| `togbane/gyro/z`              | Rotasjonshastighet Z-akse i °/s (float)      |

## Deteksjonsparametere

| Parameter          | Verdi  | Beskrivelse                                      |
|--------------------|--------|--------------------------------------------------|
| `TRIGGER_DISTANCE` | 10 cm  | Avstand som regnes som deteksjon                 |
| `MAX_DISTANCE`     | 20 cm  | Avstand over dette ignoreres                     |
| `REQUIRED_READINGS`| 5      | Antall påfølgende målinger under terskel (500ms) |
| `ACTIVATION_DELAY` | 800 ms | Minimum tid mellom sensoraktiveringer            |
| `RESET_TIMEOUT`    | 15 s   | Tilbakestill hvis ingen aktivitet                |

## Filstruktur

```
src/
  main.cpp            — Hovedlogikk: sensorsekvens, MQTT-publisering, reset
  wificonnection.cpp  — WiFi-tilkobling, MQTT-klient, enkel webserver
  ultrasonic.cpp      — HC-SR04 driver (trigger/echo)
  gyro.cpp            — ICM-20948 gyroskop-driver (I2C, bank-switching)
include/
  wificonnection.h
  ultrasonic.h
  gyro.h              — Klasse-definisjon for ICM20948Gyro
MQTTPlot.py           — Python-skript for visualisering av MQTT-data
platformio.ini        — PlatformIO build-konfig (esp32doit-devkit-v1, Arduino)
```

## Nettverkskonfigurasjon

- **WiFi:** NTNU-IOT
- **MQTT-broker:** `10.22.130.110:1883`

## ICM-20948 gyro

- Kommuniserer via I2C (SDA = GPIO 21, SCL = GPIO 22 på ESP32)
- Standard I2C-adresse: `0x69` (ADR-pinne til GND → `0x68`)
- Konfigurert med full-scale `±250 dps` og DLPF 120 Hz
- Samplingsrate: ~100 Hz (divisor = 10)
- Brukes til å detektere skjevheter/avvik i skinnene og fartsendringer langs banen
