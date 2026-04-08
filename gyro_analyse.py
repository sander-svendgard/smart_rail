"""
gyro_analyse.py — Sanntids avviksdeteksjon for smart_rail

Flyt:
  1. Abonnerer på MQTT (gyrodata + sensorstatus)
  2. Bygger opp en normalbaseline per skinnesegment (1–6)
  3. Når nok data er samlet: sjekker statistisk om nye målinger er avvik
  4. Ved avvik: kaller Claude API for å resonnere om årsak og alvorlighet
  5. Publiserer strukturert avvik-event til togbane/avvik (Unity leser dette)

Krav:
  pip install anthropic paho-mqtt

Miljøvariabel:
  ANTHROPIC_API_KEY må være satt
"""

import json
import re
import statistics
import threading
import time
from collections import deque

import anthropic
import paho.mqtt.client as mqtt
from pydantic import BaseModel

#  Innstillinger 

MQTT_BROKER = "10.22.130.110"
MQTT_PORT   = 1883

# Antall målinger som kreves per segment før vi begynner å detektere avvik.
# Toget må kjøre noen runder først for å etablere "normal" oppførsel.
BASELINE_MIN_SAMPLES = 40

# Hvor mange sigma fra gjennomsnittet som regnes som avvik.
# 2.5 betyr at verdier utenfor 99.4% av normalfordelingen flagges.
ANOMALY_SIGMA_THRESHOLD = 2.5

# Minimum sekunder mellom to avviksmeldinger for samme posisjon og akse.
# Hindrer at ett reelt avvik sender 100 meldinger til Claude.
ANOMALY_COOLDOWN_SEC = 15

#Delt tilstand  

state_lock = threading.Lock()

# Hvilken sensor (1–6) som sist ble rapportert ACTIVE
current_position = 0

# Rullende buffer med de siste 50 råmålingene per akse (for kontekst til Claude)
gyro_recent = {
    "x": deque(maxlen=50),
    "y": deque(maxlen=50),
    "z": deque(maxlen=50),
}

# Baseline-data per posisjon og akse.
# baseline_samples[posisjon][akse] = liste med målinger
# Etter BASELINE_MIN_SAMPLES fylles ikke listen mer — den er "låst"
baseline_samples: dict[int, dict[str, list[float]]] = {}

# Tidspunkt for siste avvarsmel per (posisjon, akse) — for cooldown
last_anomaly_time: dict[str, float] = {}

#  Klienter 

mqtt_client      = mqtt.Client()
anthropic_client = anthropic.Anthropic()

#  Pydantic-modell for strukturert svar fra Claude

class AvvikAnalyse(BaseModel):
    er_avvik:     bool   # True hvis Claude bekrefter at dette er et reelt avvik
    beskrivelse:  str    # Hva som skjedde, forklart på norsk
    mulig_aarsak: str    # Fysisk forklaring (f.eks. løs skjøt, dump i skinnene)
    alvorlighet:  str    # "LAV", "MIDDELS" eller "HØY"
    anbefaling:   str    # Hva bør gjøres (inspeksjon, ignorere, logg videre)

#  MQTT-callbacks 

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Koblet til MQTT-broker")
    else:
        print(f"MQTT-tilkobling feilet (kode {rc})")
        return

    # Abonnér på gyrodata fra alle tre akser
    client.subscribe("togbane/gyro/x")
    client.subscribe("togbane/gyro/y")
    client.subscribe("togbane/gyro/z")

    # Abonnér på statustopic for alle sensorer (+ er wildcard for sensornum)
    client.subscribe("togbane/sensor/+/status")

    print("Abonnerer på togbane/gyro/* og togbane/sensor/+/status")


def on_message(client, userdata, msg):
    global current_position
    topic   = msg.topic
    payload = msg.payload.decode().strip()

    #  Posisjonssporing via ultrasoniske sensorer 
    # Når en sensor rapporterer ACTIVE vet vi at toget passerte det segmentet
    if "/status" in topic:
        if payload == "ACTIVE":
            parts = topic.split("/")  # ["togbane", "sensor", "N", "status"]
            try:
                sensor_num = int(parts[2])
                with state_lock:
                    current_position = sensor_num
                print(f"Posisjon oppdatert → segment {sensor_num}")
            except (IndexError, ValueError):
                pass
        return

    #  Gyrodata 
    if "gyro" in topic:
        axis = topic.split("/")[-1]   # "x", "y" eller "z"
        if axis not in ("x", "y", "z"):
            return

        try:
            value = float(payload)
        except ValueError:
            return

        with state_lock:
            pos = current_position

            # Legg til i rullende buffer for kontekst
            gyro_recent[axis].append(value)

            # Legg til i baseline hvis vi ikke har nok data ennå
            if pos not in baseline_samples:
                baseline_samples[pos] = {"x": [], "y": [], "z": []}

            samples = baseline_samples[pos][axis]
            if len(samples) < BASELINE_MIN_SAMPLES * 2:
                samples.append(value)

                # Vis fremgang i baseline-innsamling (kun ved 10, 20, 40 samples)
                n = len(samples)
                if n in (10, 20, BASELINE_MIN_SAMPLES):
                    print(f"Baseline segment {pos} akse {axis}: {n} samples")

            # Ikke sjekk avvik ennå hvis vi ikke har nok baseline-data
            if len(samples) < BASELINE_MIN_SAMPLES:
                return

            # Lag kopier for bruk utenfor lock
            samples_copy    = list(samples)
            recent_x_copy   = list(gyro_recent["x"])
            recent_y_copy   = list(gyro_recent["y"])
            recent_z_copy   = list(gyro_recent["z"])
            cooldown_key    = f"{pos}_{axis}"
            last_time       = last_anomaly_time.get(cooldown_key, 0)

        # Statistisk avvikssjekk (utenfor lock) 
        mean  = statistics.mean(samples_copy)
        stdev = statistics.stdev(samples_copy)

        # Unngå divisjon på nær-null standardavvik (sensor i ro)
        if stdev < 0.05:
            return

        z_score = abs(value - mean) / stdev

        if z_score <= ANOMALY_SIGMA_THRESHOLD:
            return  # Normal måling

        # Cooldown-sjekk 
        now = time.time()
        if now - last_time < ANOMALY_COOLDOWN_SEC:
            return  # For nylig siden siste avvarsmel

        with state_lock:
            last_anomaly_time[cooldown_key] = now

        print(f"\nMulig avvik — segment {pos}, akse {axis.upper()}: "
              f"{value:.2f} °/s (z={z_score:.1f}σ) — sender til Claude...")

        # Kjør Claude-analyse i bakgrunnstråd for å ikke blokkere MQTT
        threading.Thread(
            target=analyse_with_claude,
            args=(axis, value, mean, stdev, z_score, pos,
                  recent_x_copy, recent_y_copy, recent_z_copy),
            daemon=True,
        ).start()


# Claude API-analyse 

def analyse_with_claude(
    axis: str,
    value: float,
    mean: float,
    stdev: float,
    z_score: float,
    position: int,
    recent_x: list,
    recent_y: list,
    recent_z: list,
):
    """
    Sender avviksdata til Claude og ber om en strukturert vurdering.
    Publiserer resultatet til togbane/avvik hvis Claude bekrefter avviket.
    """

    # Beskriv segmenttype basert på posisjon (tilpass til faktisk banegeografi)
    segment_info = {
        1: "start av banen, rett strekning",
        2: "første kurve",
        3: "rett midtseksjon",
        4: "andre kurve",
        5: "rett strekning mot slutten",
        6: "siste seksjon, nær start",
    }
    segment_type = segment_info.get(position, "ukjent segment")

    # Forklar hvilken bevegelse aksen representerer
    akse_forklaring = {
        "x": "X-aksen (roll — sidelengs vipping av toget)",
        "y": "Y-aksen (pitch — opp/ned-bevegelse av toget)",
        "z": "Z-aksen (yaw — horisontal vriing av toget)",
    }

    prompt = f"""Du er et system for prediktivt vedlikehold på en modelljernbane.
Du skal vurdere om måledata fra et gyroskop montert på toget indikerer en fysisk feil i skinnene.

SEGMENT: {position} av 6 ({segment_type})
AVVIKENDE AKSE: {akse_forklaring.get(axis, axis)}

NORMALVERDIER for dette segmentet (basert på {BASELINE_MIN_SAMPLES}+ målinger):
  Gjennomsnitt : {mean:.3f} °/s
  Standardavvik: {stdev:.3f} °/s

AVVIKENDE MÅLING:
  Verdi  : {value:.3f} °/s
  Z-score: {z_score:.1f}σ  (avviket er {z_score:.1f} ganger standardavviket fra normalen)

SISTE 10 MÅLINGER PER AKSE (nyeste sist):
  X: {[round(v, 2) for v in recent_x[-10:]]}
  Y: {[round(v, 2) for v in recent_y[-10:]]}
  Z: {[round(v, 2) for v in recent_z[-10:]]}

Vurder om dette er et reelt avvik og hva det kan indikere fysisk.
Svar KUN med gyldig JSON i dette formatet (ingen forklaring utenfor JSON):

{{
  "er_avvik": true,
  "beskrivelse": "Kort beskrivelse av hva som ble observert",
  "mulig_aarsak": "Sannsynlig fysisk årsak i skinnene eller på toget",
  "alvorlighet": "LAV",
  "anbefaling": "Hva bør gjøres videre"
}}

Alvorlighetsgrader:
  LAV    = Usikkert, kan være støy, bør logges
  MIDDELS = Tydelig avvik, bør inspiseres
  HØY    = Klart signal på skinnefeil, bør stoppes og inspiseres nå
"""

    try:
        # Bruker adaptive thinking for bedre resonnementsevne
        response = anthropic_client.messages.create(
            model="claude-opus-4-6",
            max_tokens=1024,
            thinking={"type": "adaptive"},
            messages=[{"role": "user", "content": prompt}],
        )

        # Hent tekstsvaret (hopp over thinking-blokker)
        result_text = next(
            (b.text for b in response.content if b.type == "text"), ""
        )

        # Trekk ut JSON fra svaret
        json_match = re.search(r"\{.*\}", result_text, re.DOTALL)
        if not json_match:
            print(f"Claude returnerte ikke gyldig JSON: {result_text[:200]}")
            return

        analyse_data = json.loads(json_match.group())

        # Valider med Pydantic
        analyse = AvvikAnalyse(**analyse_data)

        print(f"\n{'='*50}")
        print(f"Claude-analyse — segment {position}, akse {axis.upper()}")
        print(f"  Avvik bekreftet : {analyse.er_avvik}")
        print(f"  Beskrivelse     : {analyse.beskrivelse}")
        print(f"  Mulig årsak     : {analyse.mulig_aarsak}")
        print(f"  Alvorlighet     : {analyse.alvorlighet}")
        print(f"  Anbefaling      : {analyse.anbefaling}")
        print(f"{'='*50}\n")

        # Publiser til MQTT kun hvis Claude bekrefter avviket
        if analyse.er_avvik:
            avvik_payload = {
                "posisjon"  : position,
                "akse"      : axis,
                "verdi"     : round(value, 3),
                "z_score"   : round(z_score, 2),
                "beskrivelse": analyse.beskrivelse,
                "mulig_aarsak": analyse.mulig_aarsak,
                "alvorlighet": analyse.alvorlighet,
                "anbefaling": analyse.anbefaling,
                "tidspunkt" : time.strftime("%Y-%m-%dT%H:%M:%S"),
            }
            mqtt_client.publish("togbane/avvik", json.dumps(avvik_payload))
            print(f"Avvik publisert til togbane/avvik")

    except anthropic.APIError as e:
        print(f"Claude API-feil: {e}")
    except (json.JSONDecodeError, ValueError) as e:
        print(f"Feil ved parsing av Claude-svar: {e}")


# Oppstart 

def main():
    print("=== smart_rail gyro-analyse ===")
    print(f"Kobler til MQTT-broker {MQTT_BROKER}:{MQTT_PORT}...")
    print(f"Baseline: krever {BASELINE_MIN_SAMPLES} målinger per segment før deteksjon starter")
    print(f"Terskel  : {ANOMALY_SIGMA_THRESHOLD}σ fra normalen")
    print()

    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)

    # loop_forever() blokkerer og håndterer reconnect automatisk
    mqtt_client.loop_forever()


if __name__ == "__main__":
    main()
