import json
import sqlite3
import threading
import time
import queue
from datetime import datetime
from pathlib import Path

import paho.mqtt.client as mqtt
from flask import Flask, jsonify, render_template, request, Response

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = BASE_DIR / "smart_farm.db"

MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
MQTT_DATA_TOPIC = "esp8266/data"
MQTT_CONTROL_TOPIC = "esp8266/control"

AUTO_CHECK_INTERVAL_SECONDS = 10
SCHEDULE_CHECK_INTERVAL_SECONDS = 20
WATERING_COOLDOWN_SECONDS = 300
CLEANUP_INTERVAL_SECONDS = 3600

app = Flask(__name__)
app.config["JSON_AS_ASCII"] = False

mqtt_client: mqtt.Client | None = None
state_lock = threading.Lock()
last_auto_trigger_at = 0.0

device_state = {
    "pump": False,
    "fan": False,
    "light": False,
    "mode": "manual",
}

sse_clients = []

def notify_clients(event_type: str, data: dict) -> None:
    payload = json.dumps({"type": event_type, "data": data})
    for q in sse_clients:
        try:
            q.put(payload, block=False)
        except queue.Full:
            pass

def get_db() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH, timeout=15)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    return conn

def init_db() -> None:
    with get_db() as conn:
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS sensor_data (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                temperature REAL NOT NULL,
                air_humidity REAL NOT NULL,
                soil_moisture REAL NOT NULL,
                created_at TEXT NOT NULL
            )
            """
        )
        conn.execute("CREATE INDEX IF NOT EXISTS idx_sensor_created_at ON sensor_data(created_at)")
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS irrigation_settings (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                threshold_enabled INTEGER NOT NULL DEFAULT 0,
                soil_moisture_threshold REAL NOT NULL DEFAULT 30,
                default_duration_sec INTEGER NOT NULL DEFAULT 8
            )
            """
        )
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS irrigation_schedules (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                time_of_day TEXT NOT NULL,
                duration_sec INTEGER NOT NULL,
                enabled INTEGER NOT NULL DEFAULT 1,
                last_run_date TEXT
            )
            """
        )
        conn.execute(
            """
            INSERT OR IGNORE INTO irrigation_settings (
                id, threshold_enabled, soil_moisture_threshold, default_duration_sec
            ) VALUES (1, 0, 30, 8)
            """
        )

def insert_sensor_data(temperature: float, air_humidity: float, soil_moisture: float) -> dict:
    created_at = datetime.now().isoformat(timespec="seconds")
    with get_db() as conn:
        conn.execute(
            """
            INSERT INTO sensor_data (temperature, air_humidity, soil_moisture, created_at)
            VALUES (?, ?, ?, ?)
            """,
            (temperature, air_humidity, soil_moisture, created_at),
        )
    return {
        "temperature": temperature,
        "air_humidity": air_humidity,
        "soil_moisture": soil_moisture,
        "created_at": created_at
    }

def get_latest_data() -> sqlite3.Row | None:
    with get_db() as conn:
        return conn.execute(
            "SELECT id, temperature, air_humidity, soil_moisture, created_at FROM sensor_data ORDER BY id DESC LIMIT 1"
        ).fetchone()

def get_recent_data(limit: int = 20) -> list[sqlite3.Row]:
    with get_db() as conn:
        return conn.execute(
            "SELECT temperature, air_humidity, soil_moisture, created_at FROM sensor_data ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()

def get_settings() -> sqlite3.Row:
    with get_db() as conn:
        return conn.execute(
            "SELECT threshold_enabled, soil_moisture_threshold, default_duration_sec FROM irrigation_settings WHERE id = 1"
        ).fetchone()

def update_settings(threshold_enabled: bool, soil_threshold: float, default_duration_sec: int) -> None:
    with get_db() as conn:
        conn.execute(
            "UPDATE irrigation_settings SET threshold_enabled = ?, soil_moisture_threshold = ?, default_duration_sec = ? WHERE id = 1",
            (1 if threshold_enabled else 0, soil_threshold, default_duration_sec),
        )

def list_schedules() -> list[sqlite3.Row]:
    with get_db() as conn:
        return conn.execute(
            "SELECT id, time_of_day, duration_sec, enabled, last_run_date FROM irrigation_schedules ORDER BY time_of_day ASC"
        ).fetchall()

def add_schedule(time_of_day: str, duration_sec: int) -> None:
    with get_db() as conn:
        conn.execute(
            "INSERT INTO irrigation_schedules (time_of_day, duration_sec, enabled, last_run_date) VALUES (?, ?, 1, NULL)",
            (time_of_day, duration_sec),
        )

def delete_schedule(schedule_id: int) -> None:
    with get_db() as conn:
        conn.execute("DELETE FROM irrigation_schedules WHERE id = ?", (schedule_id,))

def toggle_schedule(schedule_id: int, enabled: bool) -> None:
    with get_db() as conn:
        conn.execute("UPDATE irrigation_schedules SET enabled = ? WHERE id = ?", (1 if enabled else 0, schedule_id))

def mark_schedule_run(schedule_id: int, today: str) -> None:
    with get_db() as conn:
        conn.execute("UPDATE irrigation_schedules SET last_run_date = ? WHERE id = ?", (today, schedule_id))

def publish_control(action: str, duration_sec: int = 0, reason: str = "manual") -> None:
    payload = {
        "action": action,
        "duration_sec": int(duration_sec),
        "reason": reason,
        "sent_at": datetime.now().isoformat(timespec="seconds"),
    }
    if mqtt_client is not None:
        mqtt_client.publish(MQTT_CONTROL_TOPIC, json.dumps(payload))

def on_mqtt_connect(client, userdata, flags, rc) -> None:
    if rc == 0:
        client.subscribe(MQTT_DATA_TOPIC)
        print(f"Connected MQTT and subscribed: {MQTT_DATA_TOPIC}")
        notify_clients("status", {"mqtt_connected": True})
    else:
        print(f"MQTT connection failed with code {rc}")
        notify_clients("status", {"mqtt_connected": False})

def on_mqtt_disconnect(client, userdata, rc):
    print("MQTT Disconnected")
    notify_clients("status", {"mqtt_connected": False})

def on_mqtt_message(client, userdata, msg) -> None:
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
        temperature = float(payload["temperature"])
        air_humidity = float(payload["air_humidity"])
        soil_moisture = float(payload["soil_moisture"])

        saved_data = insert_sensor_data(temperature, air_humidity, soil_moisture)
        notify_clients("sensor", saved_data)

        if "pump" in payload:
            with state_lock:
                device_state["pump"]  = bool(payload.get("pump", False))
                device_state["fan"]   = bool(payload.get("fan", False))
                device_state["light"] = bool(payload.get("light", False))
                device_state["mode"]  = payload.get("mode", "manual")
                current = dict(device_state)
            notify_clients("device_state", current)
    except (KeyError, ValueError, TypeError, json.JSONDecodeError) as exc:
        print(f"Invalid sensor payload: {exc}")

def start_mqtt() -> None:
    global mqtt_client
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_disconnect = on_mqtt_disconnect
    mqtt_client.on_message = on_mqtt_message
    
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"Failed to connect MQTT: {e}")

def threshold_worker() -> None:
    global last_auto_trigger_at
    while True:
        try:
            with state_lock:
                mode = device_state["mode"]
            if mode != "schedule":
                time.sleep(AUTO_CHECK_INTERVAL_SECONDS)
                continue

            settings = get_settings()
            latest = get_latest_data()
            if settings and latest and settings["threshold_enabled"] == 1:
                soil = float(latest["soil_moisture"])
                threshold = float(settings["soil_moisture_threshold"])
                now = time.time()
                with state_lock:
                    can_trigger = now - last_auto_trigger_at >= WATERING_COOLDOWN_SECONDS
                if soil < threshold and can_trigger:
                    duration = int(settings["default_duration_sec"])
                    publish_control("pump_on", duration_sec=duration, reason="threshold")
                    with state_lock:
                        last_auto_trigger_at = now
        except Exception as exc:
            print(f"Threshold worker error: {exc}")
        time.sleep(AUTO_CHECK_INTERVAL_SECONDS)

def schedule_worker() -> None:
    while True:
        try:
            with state_lock:
                mode = device_state["mode"]
            if mode != "schedule":
                time.sleep(SCHEDULE_CHECK_INTERVAL_SECONDS)
                continue

            now = datetime.now()
            current_hhmm = now.strftime("%H:%M")
            today = now.strftime("%Y-%m-%d")
            schedules = list_schedules()
            for item in schedules:
                if (
                    item["enabled"] == 1
                    and item["time_of_day"] == current_hhmm
                    and item["last_run_date"] != today
                ):
                    publish_control("pump_on", duration_sec=int(item["duration_sec"]), reason="schedule")
                    mark_schedule_run(int(item["id"]), today)
        except Exception as exc:
            print(f"Schedule worker error: {exc}")
        time.sleep(SCHEDULE_CHECK_INTERVAL_SECONDS)

def cleanup_worker() -> None:
    while True:
        try:
            with get_db() as conn:
                conn.execute("DELETE FROM sensor_data WHERE created_at < datetime('now', '-7 days')")
        except Exception as exc:
            print(f"Cleanup worker error: {exc}")
        time.sleep(CLEANUP_INTERVAL_SECONDS)

@app.route("/")
def dashboard() -> str:
    return render_template("dashboard.html")

@app.route("/api/stream")
def api_stream():
    def event_stream():
        q = queue.Queue(maxsize=20)
        sse_clients.append(q)
        try:
            is_connected = mqtt_client.is_connected() if mqtt_client else False
            yield f"data: {json.dumps({'type': 'status', 'data': {'mqtt_connected': is_connected}})}\n\n"

            with state_lock:
                current_state = dict(device_state)
            yield f"data: {json.dumps({'type': 'device_state', 'data': current_state})}\n\n"

            latest = get_latest_data()
            if latest:
                yield f"data: {json.dumps({'type': 'sensor', 'data': dict(latest)})}\n\n"
            
            while True:
                message = q.get()
                yield f"data: {message}\n\n"
        except GeneratorExit:
            if q in sse_clients:
                sse_clients.remove(q)
    
    return Response(event_stream(), mimetype="text/event-stream")

@app.route("/api/history")
def api_history():
    limit = int(request.args.get("limit", 20))
    history_rows = get_recent_data(limit=limit)
    history = [
        {
            "temperature": row["temperature"],
            "air_humidity": row["air_humidity"],
            "soil_moisture": row["soil_moisture"],
            "created_at": row["created_at"],
        }
        for row in reversed(history_rows)
    ]
    return jsonify(history)

@app.route("/api/control", methods=["POST"])
def api_control():
    body = request.get_json(silent=True) or {}
    action = body.get("action")
    duration_sec = int(body.get("duration_sec", 0))
    valid_actions = {"pump_on", "pump_off", "fan_on", "fan_off", "light_on", "light_off"}
    if action not in valid_actions:
        return jsonify({"error": "Invalid action"}), 400
    publish_control(action, duration_sec=duration_sec, reason="manual")
    return jsonify({"status": "ok"})

@app.route("/api/device-state")
def api_device_state():
    with state_lock:
        current = dict(device_state)
    return jsonify(current)

@app.route("/api/settings", methods=["GET", "POST"])
def api_settings():
    if request.method == "POST":
        body = request.get_json(silent=True) or {}
        try:
            enabled = bool(body.get("threshold_enabled", False))
            threshold = float(body.get("soil_moisture_threshold", 30))
            duration_sec = int(body.get("default_duration_sec", 8))
        except (ValueError, TypeError):
            return jsonify({"error": "Invalid settings payload"}), 400
        update_settings(enabled, threshold, duration_sec)
    settings = get_settings()
    return jsonify(
        {
            "threshold_enabled": bool(settings["threshold_enabled"]),
            "soil_moisture_threshold": settings["soil_moisture_threshold"],
            "default_duration_sec": settings["default_duration_sec"],
        }
    )

@app.route("/api/schedules", methods=["GET", "POST", "DELETE"])
def api_schedules():
    if request.method == "POST":
        body = request.get_json(silent=True) or {}
        time_of_day = str(body.get("time_of_day", "")).strip()
        duration_sec = int(body.get("duration_sec", 8))
        if len(time_of_day) != 5 or ":" not in time_of_day:
            return jsonify({"error": "time_of_day must be HH:MM"}), 400
        add_schedule(time_of_day, duration_sec)
    elif request.method == "DELETE":
        body = request.get_json(silent=True) or {}
        schedule_id = int(body.get("id", 0))
        delete_schedule(schedule_id)
    schedules = list_schedules()
    return jsonify(
        [
            {
                "id": s["id"],
                "time_of_day": s["time_of_day"],
                "duration_sec": s["duration_sec"],
                "enabled": bool(s["enabled"]),
                "last_run_date": s["last_run_date"],
            }
            for s in schedules
        ]
    )

@app.route("/api/schedules/<int:schedule_id>/toggle", methods=["POST"])
def api_toggle_schedule(schedule_id: int):
    body = request.get_json(silent=True) or {}
    enabled = bool(body.get("enabled", True))
    toggle_schedule(schedule_id, enabled)
    return jsonify({"status": "ok"})

def bootstrap() -> None:
    init_db()
    start_mqtt()
    threading.Thread(target=threshold_worker, daemon=True).start()
    threading.Thread(target=schedule_worker, daemon=True).start()
    threading.Thread(target=cleanup_worker, daemon=True).start()

bootstrap()

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
