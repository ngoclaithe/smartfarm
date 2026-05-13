# Smart Farm Dashboard

Flask app de giam sat va dieu khien tuoi tieu cho Smart Farm.

## MQTT topics

- Sensor data topic: `esp8266/data`
- Control topic: `esp8266/control`

Payload mau tu ESP8266 gui len:

```json
{
  "temperature": 29.4,
  "air_humidity": 71.2,
  "soil_moisture": 35.8
}
```

Payload control gui xuong ESP8266:

```json
{
  "action": "pump_on",
  "duration_sec": 8,
  "reason": "manual",
  "sent_at": "2026-05-10T19:00:00"
}
```

`action` co the la `pump_on` hoac `pump_off`.

## Chay voi uv

```bash
uv sync
uv run python app.py
```

App chay tai `http://127.0.0.1:5000`.

## Tinh nang

- Dashboard realtime: nhiet do, do am khong khi, do am dat.
- Dieu khien thu cong bom tuoi qua MQTT.
- Auto tuoi theo threshold do am dat.
- Dat lich tuoi theo gio (HH:MM) va thoi gian tuoi.
- Luu lich su du lieu bang SQLite (`smart_farm.db`).
