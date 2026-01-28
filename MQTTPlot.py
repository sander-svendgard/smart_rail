import paho.mqtt.client as mqtt
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import re

# --- Innstillinger ---
MQTT_BROKER = "localhost"
MQTT_TOPIC = "sensor/distance"

# Data-lister
x_vals = [] # Tidspunkter/teller
y_vals = [] # Avstand i cm
counter = 0

# --- MQTT Logikk ---
def on_connect(client, userdata, flags, rc):
    print(f"Koblet til broker (kode {rc})")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    global counter
    payload = msg.payload.decode()
    
    # Trekker ut tallet fra strengen "Distance (cm): XX.XX..."
    numbers = re.findall(r"[-+]?\d*\.\d+|\d+", payload)
    if numbers:
        distance = float(numbers[0])
        y_vals.append(distance)
        x_vals.append(counter)
        counter += 1
        
        # Hold grafen ren: vis kun de siste 50 målingene
        if len(y_vals) > 50:
            y_vals.pop(0)
            x_vals.pop(0)

# --- Plotting Logikk ---
fig, ax = plt.subplots()
line, = ax.plot([], [], 'r-') # 'r-' betyr rød linje

def init():
    ax.set_xlim(0, 50)
    ax.set_ylim(0, 100) # Juster denne etter hvor lang avstand du måler (0-100cm)
    ax.set_title("Live Avstand fra ESP32")
    ax.set_xlabel("Måling nr.")
    ax.set_ylabel("Avstand (cm)")
    return line,

def update(frame):
    if len(x_vals) > 0:
        line.set_data(x_vals, y_vals)
        # Oppdaterer X-aksen slik at den "ruller"
        ax.set_xlim(x_vals[0], x_vals[-1] + 1)
    return line,

# --- Start alt ---
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_BROKER, 1883, 60)
client.loop_start() # Kjører MQTT i bakgrunnen

ani = FuncAnimation(fig, update, init_func=init, interval=100)
plt.show()