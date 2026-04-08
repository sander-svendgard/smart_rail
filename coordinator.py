"""
Togbane-koordinator
-------------------
Abonnerer på rådata fra alle 6 sensorer, avgjør sekvensiell togposisjon,
og publiserer strukturert status til Unity via MQTT.

Topics inn:  togbane/sensor/<1-6>/distanse
Topics ut:
  togbane/sensor/<1-6>/status   (WAITING / ACTIVE / DONE)
  togbane/tog/posisjon          (f.eks. "3" = toget er ved sensor 3)
  togbane/status                (RESET / TOG_PASSERT)
"""

import paho.mqtt.client as mqtt
import time
from collections import deque

BROKER = "10.22.128.115" # Sjekk at IP-adressen er rikitg 
PORT = 1883

TRIGGER_DISTANCE = 10.0   # cm — sett lavere enn alle sensorers bakgrunnsmåling
WINDOW_SIZE = 8           # antall siste målinger som vurderes
REQUIRED_HITS = 5         # antall av WINDOW_SIZE som må være under terskel
ACTIVATION_COOLDOWN = 0.3 # sekunder mellom sensoraktiveringer
RESET_TIMEOUT = 15.0      # sekunder uten aktivitet → reset

NUM_SENSORS = 6

# --- Tilstand ---
distances = [999.0] * (NUM_SENSORS + 1)   # index 1-6
# Sliding window per sensor — True = under terskel, False = over
windows = [deque(maxlen=WINDOW_SIZE) for _ in range(NUM_SENSORS + 1)]
sensor_status = ["WAITING"] * (NUM_SENSORS + 1)  # WAITING / ACTIVE / DONE

active_sensor = 1          # neste sensor vi venter på
last_trigger_time = time.time()
last_activity_time = time.time()

client = mqtt.Client(client_id="togbane_coordinator")


def publish_status(sensor_num, status):
    global sensor_status
    if sensor_status[sensor_num] == status:
        return
    sensor_status[sensor_num] = status
    topic = f"togbane/sensor/{sensor_num}/status"
    client.publish(topic, status)
    print(f"  [{topic}] {status}")


def do_reset():
    global active_sensor, last_trigger_time, last_activity_time
    active_sensor = 1
    for i in range(1, NUM_SENSORS + 1):
        windows[i].clear()
        publish_status(i, "WAITING")
    client.publish("togbane/status", "RESET")
    client.publish("togbane/tog/posisjon", "0")
    last_trigger_time = time.time()
    last_activity_time = time.time()
    print("=== RESET ===")


def on_connect(c, userdata, flags, rc):
    if rc == 0:
        print(f"Tilkoblet MQTT-broker {BROKER}")
        c.subscribe("togbane/sensor/+/distanse")
    else:
        print(f"Tilkobling feilet, rc={rc}")


def on_message(c, userdata, msg):
    global active_sensor, last_trigger_time, last_activity_time

    # Parse sensor-nummer fra topic: togbane/sensor/<N>/distanse
    parts = msg.topic.split("/")
    if len(parts) != 4:
        return
    try:
        sensor_num = int(parts[2])
        distance = float(msg.payload.decode())
    except (ValueError, IndexError):
        return

    if sensor_num < 1 or sensor_num > NUM_SENSORS:
        return

    distances[sensor_num] = distance

    # Bare behandle aktiv sensor
    if sensor_num != active_sensor:
        return

    last_activity_time = time.time()

    now = time.time()
    cooldown_ok = (now - last_trigger_time) >= ACTIVATION_COOLDOWN

    # Legg måling inn i vinduet: True = under terskel (toget er der)
    is_hit = (0.5 < distance < TRIGGER_DISTANCE)
    windows[sensor_num].append(is_hit)

    hits_in_window = sum(windows[sensor_num])
    window_full = len(windows[sensor_num]) == WINDOW_SIZE

    print(f"Sensor {sensor_num}: {distance:.1f}cm  "
          f"treff {hits_in_window}/{REQUIRED_HITS} i vindu {len(windows[sensor_num])}/{WINDOW_SIZE}")

    if hits_in_window >= REQUIRED_HITS and window_full and cooldown_ok:
        # Sensor aktivert
        publish_status(sensor_num, "ACTIVE")
        client.publish("togbane/tog/posisjon", str(sensor_num))
        print(f">>> Sensor {sensor_num} AKTIVERT! <<<")

        time.sleep(0.1)  # gi Unity tid til å lese ACTIVE

        publish_status(sensor_num, "DONE")
        last_trigger_time = now
        windows[sensor_num].clear()

        if sensor_num < NUM_SENSORS:
            active_sensor = sensor_num + 1
            publish_status(active_sensor, "WAITING")
            print(f"Venter på sensor {active_sensor}...")
        else:
            # Alle sensorer fullført
            client.publish("togbane/status", "TOG_PASSERT")
            print("=== TOG PASSERT ALLE SENSORER ===")
            time.sleep(2.0)
            do_reset()


def check_timeout():
    if time.time() - last_activity_time > RESET_TIMEOUT:
        if active_sensor > 1:  # bare reset hvis vi er midt i en sekvens
            print("Timeout — ingen aktivitet. Resetter.")
            do_reset()


client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, keepalive=60)
client.loop_start()

do_reset()

print("Koordinator kjører. Ctrl+C for å stoppe.")
try:
    while True:
        check_timeout()
        time.sleep(0.5)
except KeyboardInterrupt:
    print("Stopper.")
    client.loop_stop()
    client.disconnect()
