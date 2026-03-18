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

## Mulige bugs / kjente svakheter

### B-01 — `gruppe1/ferdig` kan mistes ved MQTT-frakobling
**Status:** Uløst
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
