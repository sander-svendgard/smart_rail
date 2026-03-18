# Bugs og endringer — smart_rail

Dokument for å spore kjente feil, endringer og mulige problemer i prosjektet.

---

## Endringer

### 2026-03-18 — MQTT-flooding og sensorhopping (main.cpp)

**Problem 1: MQTT-flooding**
Hver loop-iterasjon (100ms) publiserte distanse + alle sensorstatuser uavhengig av om noe hadde endret seg. Med 3 ESP32-kort ga dette ~90 MQTT-meldinger/sekund. PubSubClient har liten intern buffer og droppet meldinger under last.

**Løsning:**
- Lagt til `lastSensorStatus[3]`-array og `setSensorStatus()` som kun publiserer ved faktisk statusendring.
- Distansepublisering throttlet til maks hvert 300ms (`DISTANCE_PUBLISH_INTERVAL`).

---

**Problem 2: ACTIVE-status ble umiddelbart overskrevet**
`publishAllSensorStatuses(activeSensor, "WAITING")` ble kalt øverst i loopen hver 100ms. Når en sensor ble aktivert, ble `ACTIVE` publisert — men neste loop-iterasjon overskrev det med `WAITING` igjen. Unity kunne miste `ACTIVE`-meldingen helt.

**Løsning:**
- `ACTIVE` publiseres via `setSensorStatus()` som sikrer én unik publisering og ikke overskrives av `updateSensorStatuses()` før neste tilstandsendring.

---

**Problem 3: Sensorhopping ved raskt tog**
`ACTIVATION_DELAY` på 800ms blokkerte opptellingen (`triggerCount`) i stedet for bare selve triggeren. Toget kunne passere en sensor mens cooldownen var aktiv, uten at noe ble telt opp — sensoren ble hoppet over.

**Løsning:**
- `ACTIVATION_DELAY` redusert fra 800ms til 200ms.
- Opptelling starter umiddelbart når toget er under terskel. Cooldown-sjekken (`delayPassed`) flyttes til selve trigger-betingelsen, ikke opptellingen:
  ```cpp
  // Før
  if (distance < TRIGGER_DISTANCE && distance > 0.5 && delayPassed) { triggerCount++; }

  // Etter
  if (distance < TRIGGER_DISTANCE && distance > 0.5) {
      triggerCount++;
      if (triggerCount >= REQUIRED_READINGS && delayPassed) { /* trigger */ }
  }
  ```

---

---

### 2026-03-18 — Sensor hopper mellom grupper (1→3 uten at sekvensen fullføres)

**Problem: Stale MQTT-melding fra forrige kjøring**
Brokeren lagrer siste publiserte melding på `togbane/gruppe1/ferdig`. Når ESP32 #2 starter opp og abonnerer, leverer brokeren den lagrede `TOG_PASSERT`-meldingen umiddelbart — selv om ESP32 #1 ikke har trigget noe denne runden. ESP32 #2 trodde det var klarsignal og aktiverte sensor 3 uten at 1 og 2 var trigget.

Symptom i MQTT-output:
```
togbane/sensor/1/distanse 20.0   ← ESP32 #1 aktiv, toget ikke detektert
togbane/sensor/3/distanse 20.0   ← ESP32 #2 aktiv samtidig — feil!
togbane/sensor/3/status SLEEP    ← ESP32 #2 timer ut og resetter
```

**Løsning:**
- Lagt til `STARTUP_GRACE = 5000ms` — gruppe-signaler ignoreres de første 5 sekundene etter oppstart.
- ESP32 #1 publiserer `RESET` på `togbane/gruppe1/ferdig` og `togbane/gruppe2/ferdig` ved oppstart for å overskrive gammel `TOG_PASSERT` på brokeren.
- `publishAllSleep` i waiting-loopen tømmer nå cache-arrayen slik at SLEEP faktisk sendes som heartbeat hvert 5. sekund til Unity.

**Fikset i:** `src/main.cpp`

---

### 2026-03-18 — Ugyldig MAC-adresse for ESP32 #3 (linje 142)

**Problem:** MAC-adressen er skrevet som `"8O:F3:DA:BC:1F:64"` med bokstaven **O** i stedet for sifferet **0**. En MAC-adresse kan bare inneholde hex-tegn (0–9, A–F). ESP32 #3 vil aldri matche og havne i `while (true)` — den henger ved oppstart.

**Løsning:** Bytt ut `O` med `0` i MAC-strengen, eller sjekk aktuell MAC via Serial-monitor (`MAC: ...` printes ved oppstart).

**Status:** Ikke rettet — avventer korrekt MAC-adresse fra bruker.

---

---

### 2026-03-18 — Arkitekturbytte: distribuert logikk → sentralisert koordinator

**Problem:** Sekvenslogikken distribuert over 3 ESP32-er via MQTT er for skjør. Stale meldinger, reconnects og timing-forskjeller mellom boardene fører til at sensorer hoppes over eller aktiveres i feil rekkefølge, selv etter tidligere fikser.

**Løsning — ny arkitektur:**
- ESP32-ene sender kun rådata (`togbane/sensor/<N>/distanse`) uten noen sekvenslogikk
- Python-filen `coordinator.py` abonnerer på alle 6 sensorer og håndterer hele tilstandsmaskinen
- Koordinatoren publiserer `togbane/sensor/<N>/status`, `togbane/tog/posisjon` og `togbane/status`
- Unity abonnerer på koordinatorens output — ingenting endres i Unity-integrasjonen

**Fordeler:**
- Ingen inter-ESP32 kommunikasjon → ingen synkroniseringsproblemer
- All logikk på ett sted, enkelt å debugge
- ESP32-koden er trivielt enkel og pålitelig

**Kjør koordinatoren med:**
```bash
pip install paho-mqtt
python coordinator.py
```

---

---

### 2026-03-18 — Sensorstøy nullstiller opptelling, sensor trigges aldri

**Problem:** Ultrasoniske sensorer gir sporadiske høye målinger (f.eks. 10.6, 12.3cm) selv når toget er rett under. Med krav om 5 *påfølgende* treff under terskel resetter én dårlig måling hele telleren til 0. Sensor trigges aldri.

Eksempel fra logg:
```
Sensor 1: 8.0cm  treff 3/5
Sensor 1: 8.0cm  treff 4/5
Sensor 1: 9.1cm  treff 1/5  ← ett målesprang nullstilte alt
```

**Løsning — sliding window:**
- Byttet fra `hit_counts` (påfølgende treff) til `deque(maxlen=8)` per sensor
- Teller treff innenfor vindu av siste 8 målinger. Trigger når 5 av 8 er under terskel
- Noen dårlige målinger stopper ikke lenger sekvensen
- `TRIGGER_DISTANCE` økt fra 10cm til 12cm for å absorbere mer av støyen

**Fikset i:** `coordinator.py`

---

## Mulige bugs / kjente svakheter

### B-01 — `gruppe1/ferdig` kan mistes ved MQTT-frakobling
**Status:** Delvis løst (se 2026-03-18 nedenfor)
**Beskrivelse:** Signalet `togbane/gruppe1/ferdig` sendes én gang uten `retain`-flagg. Hvis ESP32 #2 er frakoblet MQTT i det øyeblikket meldingen sendes, mottar den aldri signalet og forblir i `waitingForStart = true` for alltid — frem til neste reset.
**Forslag:** Sett `retain = true` på `gruppe1/ferdig` og `gruppe2/ferdig`-topics, eller implementer en retry-mekanisme der ESP32 #1 republiserer signalet periodisk til den får bekreftelse.

---

### B-02 — `pulseIn` på ultrasonisk sensor blokkerer loopen
**Status:** Uløst
**Beskrivelse:** `pulseIn(echopin, HIGH)` i `ultrasonic.cpp` har en standard timeout på 1 sekund hvis ingen ekko mottas. Hvis en sensor feiler eller er feilkoblet, blokkerer hele ESP32-loopen i 1 sekund per måling. Dette forsinker MQTT-behandling og kan føre til at `gruppe1/ferdig`-meldinger mistes.
**Forslag:** Legg til eksplisitt timeout i `pulseIn`-kallet, f.eks.:
```cpp
long duration = pulseIn(echopin, HIGH, 30000); // 30ms timeout
```

---

### B-03 — Én subscribedTopic per WiFiConnection
**Status:** Uløst
**Beskrivelse:** `WiFiConnection` lagrer kun én `subscribedTopic` (én `const char*`). Ved MQTT-reconnect re-abonneres det kun på denne ene topicen. Hvis man i fremtiden trenger å abonnere på flere topics (f.eks. en reset-kommando fra Unity), vil kun den siste `subscribe()`-kall huskes etter reconnect.
**Forslag:** Utvid til en liste av topics ved behov.

---

### B-04 — `triggerCount` kan overstige `REQUIRED_READINGS` uten å triggere
**Status:** Lav prioritet
**Beskrivelse:** Hvis toget stopper akkurat over en sensor mens `delayPassed = false`, vil `triggerCount` fortsette å telle langt over `REQUIRED_READINGS`. Når `delayPassed` til slutt blir `true`, trigges sensoren umiddelbart — som forventet — men `triggerCount` er da unødvendig høy. Funksjonelt sett er dette ufarlig, men kan skape forvirring i Serial-loggen.
**Forslag:** Begrens `triggerCount` til maks `REQUIRED_READINGS` eller reset ved cooldown-start.

---

### B-05 — Hardkodede MAC-adresser
**Status:** Kjent begrensning
**Beskrivelse:** Boardtildeling (sensor-offset, pin-konfigurasjon) er hardkodet mot spesifikke MAC-adresser. Hvis et ESP32-kort byttes ut, må MAC-adressen oppdateres manuelt i koden og koden kompileres på nytt.
**Forslag:** Vurder konfigurasjon via EEPROM, én config-header per board, eller automatisk konfigurasjon via MQTT ved første oppstart.
