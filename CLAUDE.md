# smart_rail — CLAUDE.md

## Prosjektbeskrivelse

Utvikling av en **digital tvilling** for en fysisk modelljernbane. Systemet kombinerer sanntids sensordata med en 3D-modell i Unity for å overvåke togdrift og detektere avvik i skinnene (prediktivt vedlikehold).

## Arkitektur

- **3 ESP32-kort**, hvert med 2 HC-SR04 ultrasoniske sensorer (6 sensorer totalt)
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
include/
  wificonnection.h
  ultrasonic.h
MQTTPlot.py           — Python-skript for visualisering av MQTT-data
platformio.ini        — PlatformIO build-konfig (esp32doit-devkit-v1, Arduino)
```

## Nettverkskonfigurasjon

- **WiFi:** NTNU-IOT
- **MQTT-broker:** `10.22.128.83:1883`
