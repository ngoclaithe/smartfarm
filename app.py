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

AUTO_CHECK_INTERVAL_SECONDS = 5
SCHEDULE_CHECK_INTERVAL_SECONDS = 1
WATERING_COOLDOWN_SECONDS = 300
CLEANUP_INTERVAL_SECONDS = 3600

app = Flask(__name__)
app.config["JSON_AS_ASCII"] = False

mqtt_client: mqtt.Client | None = None
state_lock = threading.Lock()
last_auto_triggers = {}

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
        
        conn.execute("DROP TABLE IF EXISTS automations")
        
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS automations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device TEXT NOT NULL,
                type TEXT NOT NULL,
                is_enabled INTEGER DEFAULT 1,
                sensor TEXT,
                condition TEXT,
                threshold_value REAL,
                time_of_day TEXT,
                end_time TEXT,
                action TEXT DEFAULT 'on',
                duration_sec INTEGER DEFAULT 0,
                last_run_date TEXT,
                last_end_date TEXT
            )
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
    global last_auto_triggers
    while True:
        try:
            with state_lock:
                mode = device_state["mode"]
            if mode != "schedule":
                time.sleep(AUTO_CHECK_INTERVAL_SECONDS)
                continue

            latest = get_latest_data()
            if not latest:
                time.sleep(AUTO_CHECK_INTERVAL_SECONDS)
                continue

            with get_db() as conn:
                rules = conn.execute("SELECT * FROM automations WHERE type='threshold' AND is_enabled=1").fetchall()
            
            now = time.time()
            for rule in rules:
                rule_id = rule["id"]
                device = rule["device"]
                sensor = rule["sensor"]
                condition = rule["condition"]
                thresh = float(rule["threshold_value"])
                action = rule["action"]
                duration = int(rule["duration_sec"])

                # Handle potentially missing sensors gracefully
                if sensor not in latest.keys(): continue
                val = float(latest[sensor])
                
                is_match = (condition == '<' and val < thresh) or (condition == '>' and val > thresh)
                
                with state_lock:
                    last_trig = last_auto_triggers.get(rule_id, 0.0)
                    can_trigger = (now - last_trig) >= WATERING_COOLDOWN_SECONDS

                if is_match and can_trigger:
                    action_str = f"{device}_{action}"
                    publish_control(action_str, duration_sec=duration, reason="threshold")
                    with state_lock:
                        last_auto_triggers[rule_id] = now

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
            current_time = now.strftime("%H:%M:%S")
            today = now.strftime("%Y-%m-%d")
            
            with get_db() as conn:
                rules = conn.execute("SELECT * FROM automations WHERE type='schedule' AND is_enabled=1").fetchall()

            for rule in rules:
                device = rule["device"]
                start_t = rule["time_of_day"]
                end_t = rule["end_time"]
                
                # Format to HH:MM:SS if missing seconds
                if start_t and len(start_t) == 5: start_t += ":00"
                if end_t and len(end_t) == 5: end_t += ":00"
                
                # Safely access last_run / last_end
                last_run = rule["last_run_date"]
                # sqlite3.Row might throw error if column doesn't exist, but we added it
                last_end = rule["last_end_date"]
                
                # Bật thiết bị khi đến giờ bắt đầu
                if start_t and current_time >= start_t and last_run != today:
                    publish_control(f"{device}_on", duration_sec=0, reason="schedule")
                    with get_db() as conn:
                        conn.execute("UPDATE automations SET last_run_date=? WHERE id=?", (today, rule["id"]))
                
                # Tắt thiết bị khi đến giờ kết thúc
                if end_t and current_time >= end_t and last_end != today:
                    publish_control(f"{device}_off", duration_sec=0, reason="schedule")
                    with get_db() as conn:
                        conn.execute("UPDATE automations SET last_end_date=? WHERE id=?", (today, rule["id"]))

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
    valid_actions = {"pump_on", "pump_off", "fan_on", "fan_off", "light_on", "light_off", "mode_auto", "mode_manual"}
    
    if action not in valid_actions:
        return jsonify({"error": "Invalid action"}), 400

    if action not in ["mode_auto", "mode_manual"]:
        with state_lock:
            mode = device_state["mode"]
        if mode != "manual":
            return jsonify({"error": "Dang o che do hen gio, khong the dieu khien thu cong"}), 403

    publish_control(action, duration_sec=duration_sec, reason="manual")
    return jsonify({"status": "ok"})

@app.route("/api/device-state")
def api_device_state():
    with state_lock:
        current = dict(device_state)
    return jsonify(current)

@app.route("/api/automations", methods=["GET", "POST"])
def api_automations():
    if request.method == "POST":
        body = request.get_json(silent=True) or {}
        device = body.get("device")
        type_ = body.get("type")
        
        with get_db() as conn:
            if type_ == "threshold":
                sensor = body.get("sensor", "soil_moisture")
                condition = body.get("condition", "<")
                thresh = float(body.get("threshold_value", 0))
                action = body.get("action", "on")
                dur = int(body.get("duration_sec", 0))
                conn.execute(
                    "INSERT INTO automations (device, type, sensor, condition, threshold_value, action, duration_sec) VALUES (?, ?, ?, ?, ?, ?, ?)",
                    (device, type_, sensor, condition, thresh, action, dur)
                )
            elif type_ == "schedule":
                time_of_day = body.get("time_of_day")
                end_time = body.get("end_time")
                conn.execute(
                    "INSERT INTO automations (device, type, time_of_day, end_time) VALUES (?, ?, ?, ?)",
                    (device, type_, time_of_day, end_time)
                )
        return jsonify({"status": "ok"})
    
    device = request.args.get("device")
    with get_db() as conn:
        if device:
            rows = conn.execute("SELECT * FROM automations WHERE device = ? ORDER BY id DESC", (device,)).fetchall()
        else:
            rows = conn.execute("SELECT * FROM automations ORDER BY id DESC").fetchall()
    return jsonify([dict(r) for r in rows])

@app.route("/api/automations/<int:auto_id>", methods=["DELETE"])
def api_delete_automation(auto_id: int):
    with get_db() as conn:
        conn.execute("DELETE FROM automations WHERE id = ?", (auto_id,))
    return jsonify({"status": "ok"})

@app.route("/api/automations/<int:auto_id>/toggle", methods=["POST"])
def api_toggle_automation(auto_id: int):
    body = request.get_json(silent=True) or {}
    enabled = bool(body.get("enabled", True))
    with get_db() as conn:
        conn.execute("UPDATE automations SET is_enabled = ? WHERE id = ?", (1 if enabled else 0, auto_id))
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
