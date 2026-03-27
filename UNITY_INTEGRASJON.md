# Unity-integrasjon — smart_rail digital tvilling

Dette dokumentet beskriver hvordan Unity-siden av systemet skal settes opp for å motta sanntidsdata fra togbanen og visualisere avvik på 3D-modellen.

---

## Systemoversikt

```
[Tog med ICM-20948 gyro]
        │ I2C
[ESP32 #1/#2/#3]  ←── HC-SR04 ultrasoniske sensorer
        │ WiFi / MQTT
[MQTT-broker  10.22.130.110:1883]
        │
        ├── [gyro_analyse.py]  ← Claude AI-analyse av avvik
        │         │ MQTT publish
        │         ▼
        └── [Unity C# MQTT-klient]
                  │
            [3D-modell av togbane]
            - Togposisjon oppdateres live
            - Avvikssegmenter farges rødt/gult
```

---

## MQTT-topics Unity skal abonnere på

| Topic | Innhold | Format | Frekvens |
|-------|---------|--------|----------|
| `togbane/sensor/<N>/status` | Status per sensor | String: `WAITING` / `ACTIVE` / `DONE` / `SLEEP` | Ved endring |
| `togbane/gyro/x` | Rotasjon X-akse | Float som string, °/s | ~5 Hz |
| `togbane/gyro/y` | Rotasjon Y-akse | Float som string, °/s | ~5 Hz |
| `togbane/gyro/z` | Rotasjon Z-akse | Float som string, °/s | ~5 Hz |
| `togbane/avvik`  | Avviksanalyse fra Claude | JSON (se under) | Ved avvik |
| `togbane/status` | Global banestatus | String | Ved endring |

---

## JSON-format for `togbane/avvik`

Dette er det viktigste topicet for Unity. Det publiseres av `gyro_analyse.py` når Claude bekrefter et avvik.

```json
{
  "posisjon":     3,
  "akse":         "y",
  "verdi":        18.7,
  "z_score":      4.2,
  "beskrivelse":  "Kraftig pitch-spike på rett strekning",
  "mulig_aarsak": "Vertikal ujevnhet, mulig løs skjøt mellom skinnebit 2 og 3",
  "alvorlighet":  "HØY",
  "anbefaling":   "Inspiser segment 3 umiddelbart",
  "tidspunkt":    "2026-03-27T14:22:01"
}
```

### Feltbeskrivelse

| Felt | Type | Beskrivelse |
|------|------|-------------|
| `posisjon` | int (1–6) | Hvilket skinnesegment avviket ble detektert i |
| `akse` | string (`x`/`y`/`z`) | Hvilken gyroakse som slo ut |
| `verdi` | float | Målt gyroverdi i °/s |
| `z_score` | float | Hvor mange standardavvik verdien er fra normalen |
| `beskrivelse` | string | Menneskelig forklaring fra Claude |
| `mulig_aarsak` | string | Sannsynlig fysisk årsak |
| `alvorlighet` | string | `"LAV"` / `"MIDDELS"` / `"HØY"` |
| `anbefaling` | string | Handlingsanbefaling fra Claude |
| `tidspunkt` | string | ISO 8601 tidsstempel |

---

## Posisjonskart — sensor til skinnesegment

Togposisjon bestemmes av hvilken ultralyd-sensor som sist rapporterte `ACTIVE`.

```
Sensor 1 ──► Segment 1  (start, ESP32 #1)
Sensor 2 ──► Segment 2  (ESP32 #1)
Sensor 3 ──► Segment 3  (ESP32 #2)
Sensor 4 ──► Segment 4  (ESP32 #2)
Sensor 5 ──► Segment 5  (ESP32 #3)
Sensor 6 ──► Segment 6  (ESP32 #3)
```

I Unity: når `togbane/sensor/3/status` = `ACTIVE` → flytt toget til segment 3 på 3D-modellen.

---

## Foreslått Unity C#-struktur

### Pakker som trengs
- **M2MQTT** eller **HiveMQ Unity Client** — MQTT-klient for Unity
  - Anbefalt: [M2Mqtt NuGet](https://github.com/eclipse/paho.mqtt.m2mqtt) (fungerer i Unity)
  - Alternativ: [HiveMQ Unity MQTT Client](https://github.com/hivemq/hivemq-mqtt-client-dotnet)

### Klasser

```
Assets/
  Scripts/
    MQTT/
      MqttManager.cs          — Kobler til broker, distribuerer meldinger til abonnenter
    Train/
      TrainController.cs      — Flytter togmodellen til riktig segment basert på sensorstatus
      GyroVisualizer.cs       — Viser live gyrografi (valgfritt, for debugging)
    Track/
      TrackSegment.cs         — Én komponent per skinnesegment (1–6)
      AnomalyHighlighter.cs   — Farger et segment basert på alvorlighet
    Data/
      AvvikData.cs            — C# klasse som speiler JSON-strukturen fra togbane/avvik
```

---

## MqttManager.cs — skjelett

```csharp
using System;
using UnityEngine;
using uPLibrary.Networking.M2Mqtt;
using uPLibrary.Networking.M2Mqtt.Messages;

public class MqttManager : MonoBehaviour
{
    public static MqttManager Instance;

    private MqttClient client;
    private const string BrokerAddress = "10.22.130.110";
    private const int    BrokerPort    = 1883;

    // Andre scripts registrerer seg her for å motta meldinger
    public event Action<string, string> OnMessageReceived;

    void Awake()
    {
        Instance = this;
    }

    void Start()
    {
        client = new MqttClient(BrokerAddress, BrokerPort, false, null, null,
                                MqttSslProtocols.None);
        client.MqttMsgPublishReceived += OnMqttMessage;
        client.Connect("unity-client");

        client.Subscribe(new[]
        {
            "togbane/sensor/+/status",
            "togbane/gyro/x",
            "togbane/gyro/y",
            "togbane/gyro/z",
            "togbane/avvik",
            "togbane/status",
        },
        new[] {
            MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE,
            MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE,
            MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE,
            MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE,
            MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE,
            MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE,
        });

        Debug.Log("Koblet til MQTT-broker");
    }

    private void OnMqttMessage(object sender, MqttMsgPublishEventArgs e)
    {
        string topic   = e.Topic;
        string payload = System.Text.Encoding.UTF8.GetString(e.Message);

        // MQTT callbacks kjører ikke på Unity main thread — må dispatches
        UnityMainThreadDispatcher.Instance.Enqueue(() =>
            OnMessageReceived?.Invoke(topic, payload));
    }

    void OnDestroy()
    {
        if (client != null && client.IsConnected)
            client.Disconnect();
    }
}
```

---

## AvvikData.cs — dataobjekt

```csharp
[System.Serializable]
public class AvvikData
{
    public int    posisjon;
    public string akse;
    public float  verdi;
    public float  z_score;
    public string beskrivelse;
    public string mulig_aarsak;
    public string alvorlighet;   // "LAV", "MIDDELS", "HØY"
    public string anbefaling;
    public string tidspunkt;
}
```

---

## AnomalyHighlighter.cs — skjelett

```csharp
using UnityEngine;

public class AnomalyHighlighter : MonoBehaviour
{
    // Koble til i Inspector: én referanse per segment (1–6)
    public TrackSegment[] segments;

    void Start()
    {
        MqttManager.Instance.OnMessageReceived += HandleMessage;
    }

    private void HandleMessage(string topic, string payload)
    {
        if (topic != "togbane/avvik") return;

        AvvikData avvik = JsonUtility.FromJson<AvvikData>(payload);

        // Finn riktig segment-objekt (posisjon er 1-indeksert)
        int idx = avvik.posisjon - 1;
        if (idx < 0 || idx >= segments.Length) return;

        Color farge = avvik.alvorlighet switch
        {
            "HØY"    => Color.red,
            "MIDDELS" => Color.yellow,
            _         => Color.cyan,  // LAV
        };

        segments[idx].SetFarge(farge, avvik.beskrivelse);
    }
}
```

---

## TrackSegment.cs — skjelett

```csharp
using UnityEngine;

public class TrackSegment : MonoBehaviour
{
    private Renderer segmentRenderer;
    private Color    originalColor;

    void Start()
    {
        segmentRenderer = GetComponent<Renderer>();
        originalColor   = segmentRenderer.material.color;
    }

    public void SetFarge(Color farge, string tooltip)
    {
        segmentRenderer.material.color = farge;
        Debug.Log($"Segment {name}: {tooltip}");
        // Her kan du også vise tooltip i UI
    }

    public void Tilbakestill()
    {
        segmentRenderer.material.color = originalColor;
    }
}
```

---

## TrainController.cs — skjelett

```csharp
using UnityEngine;

public class TrainController : MonoBehaviour
{
    // Koble til i Inspector: posisjon i verden per segment (1–6)
    public Transform[] segmentPosisjoner;

    void Start()
    {
        MqttManager.Instance.OnMessageReceived += HandleMessage;
    }

    private void HandleMessage(string topic, string payload)
    {
        // togbane/sensor/3/status
        if (!topic.StartsWith("togbane/sensor/") || !topic.EndsWith("/status"))
            return;

        if (payload != "ACTIVE") return;

        string[] deler = topic.Split('/');
        if (!int.TryParse(deler[2], out int sensorNr)) return;

        int idx = sensorNr - 1;
        if (idx < 0 || idx >= segmentPosisjoner.Length) return;

        // Flytt toget til segmentets posisjon
        transform.position = segmentPosisjoner[idx].position;
        Debug.Log($"Tog på segment {sensorNr}");
    }
}
```

---

## Nødvendig Unity-tillegg: UnityMainThreadDispatcher

MQTT-callbacks kommer fra en bakgrunnstråd og kan ikke direkte kalle Unity API.
Last ned og legg til i prosjektet:

```
https://github.com/PimDeWitte/UnityMainThreadDispatcher
```

Legg `UnityMainThreadDispatcher.cs` i `Assets/Scripts/` og legg objektet i scenen.

---

## Steg-for-steg oppsett i Unity

1. Importer M2Mqtt-biblioteket (DLL) til `Assets/Plugins/`
2. Legg `UnityMainThreadDispatcher.cs` og `MqttManager.cs` på et tomt GameObject kalt **`[MQTT]`**
3. Legg til `TrackSegment.cs` på hvert skinne-objekt i 3D-scenen (6 stk)
4. Lag et GameObject kalt **`[TrackManager]`** med `AnomalyHighlighter.cs` og koble de 6 TrackSegment-referansene i Inspector
5. Legg `TrainController.cs` på togmodellen og koble 6 tomme GameObject-posisjoner langs banen i Inspector
6. Trykk Play — Unity kobler til broker og begynner å motta data

---

## Avviksfargekode

| Farge | Alvorlighet | Betydning |
|-------|-------------|-----------|
| Cyan | LAV | Usikkert, logget for historikk |
| Gul | MIDDELS | Tydelig avvik, planlegg inspeksjon |
| Rød | HØY | Klart skinnesignal, stopp og inspiser |
| Grønn | (ingen avvik) | Normal drift |
