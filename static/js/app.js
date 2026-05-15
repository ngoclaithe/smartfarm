const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelectorAll(sel);

let currentDevice = null;

function showToast(message, type = 'success') {
  const container = $('toast-container');
  const toast = document.createElement('div');
  toast.className = `toast ${type}`;
  toast.innerHTML = `
    <i class="fa-solid ${type === 'success' ? 'fa-check-circle' : 'fa-circle-exclamation'}"></i>
    <span>${message}</span>
  `;
  container.appendChild(toast);
  setTimeout(() => {
    toast.style.animation = 'slideIn 0.3s ease reverse forwards';
    setTimeout(() => toast.remove(), 300);
  }, 3000);
}

function initNavigation() {
  const navItems = $$('.nav-item');
  const pages = $$('.page');
  const pageTitle = $('page-title');

  navItems.forEach(item => {
    item.addEventListener('click', (e) => {
      e.preventDefault();
      navItems.forEach(nav => nav.classList.remove('active'));
      item.classList.add('active');
      pageTitle.textContent = item.querySelector('span').textContent;
      const targetId = item.getAttribute('data-target');
      pages.forEach(page => {
        page.classList.toggle('active', page.id === targetId);
      });
    });
  });
}

async function api(path, options = {}) {
  try {
    const response = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      ...options,
    });
    const data = await response.json();
    if (!response.ok) {
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    return data;
  } catch (error) {
    showToast(`Lỗi: ${error.message}`, 'error');
    throw error;
  }
}

// Chart
let sensorChart = null;
let chartData = { labels: [], temp: [], hum: [], soil: [] };

function initChart() {
  const ctx = document.getElementById('sensor-chart').getContext('2d');
  const textColor = '#9cb0df';
  const gridColor = 'rgba(255, 255, 255, 0.05)';

  sensorChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: chartData.labels,
      datasets: [
        {
          label: 'Nhiệt độ (°C)', data: chartData.temp,
          borderColor: '#ef4444', backgroundColor: 'rgba(239, 68, 68, 0.1)',
          borderWidth: 2, tension: 0.4, fill: true
        },
        {
          label: 'Độ ẩm KK (%)', data: chartData.hum,
          borderColor: '#3b82f6', backgroundColor: 'rgba(59, 130, 246, 0.1)',
          borderWidth: 2, tension: 0.4, fill: true
        },
        {
          label: 'Độ ẩm đất (%)', data: chartData.soil,
          borderColor: '#8b5cf6', backgroundColor: 'rgba(139, 92, 246, 0.1)',
          borderWidth: 2, tension: 0.4, fill: true
        }
      ]
    },
    options: {
      responsive: true, maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false },
      plugins: { legend: { labels: { color: textColor } } },
      scales: {
        x: { grid: { color: gridColor }, ticks: { color: textColor } },
        y: { grid: { color: gridColor }, ticks: { color: textColor } }
      }
    }
  });
}

function updateChart(dataPoint) {
  if (!sensorChart) return;
  const timeStr = dataPoint.created_at.split('T')[1] || dataPoint.created_at.split(' ')[1] || dataPoint.created_at;
  chartData.labels.push(timeStr.substring(0, 8));
  chartData.temp.push(dataPoint.temperature);
  chartData.hum.push(dataPoint.air_humidity);
  chartData.soil.push(dataPoint.soil_moisture);
  if (chartData.labels.length > 20) {
    chartData.labels.shift(); chartData.temp.shift();
    chartData.hum.shift(); chartData.soil.shift();
  }
  sensorChart.update('none');
}

function prependHistory(row) {
  const table = $("history-table");
  if (table.querySelector('td[colspan="4"]')) table.innerHTML = "";
  const tr = document.createElement("tr");
  tr.innerHTML = `
    <td>${row.created_at}</td>
    <td><span style="color: #ef4444; font-weight: 500;">${row.temperature.toFixed(1)} °C</span></td>
    <td><span style="color: #3b82f6; font-weight: 500;">${row.air_humidity.toFixed(1)} %</span></td>
    <td><span style="color: #8b5cf6; font-weight: 500;">${row.soil_moisture.toFixed(1)} %</span></td>
  `;
  table.prepend(tr);
  if (table.children.length > 20) table.lastElementChild.remove();
}

async function loadInitialHistory() {
  const table = $("history-table");
  table.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">Đang tải...</td></tr>`;
  try {
    const history = await api("/api/history?limit=20");
    if (!history.length) {
      table.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">Chưa có dữ liệu</td></tr>`;
      return;
    }
    table.innerHTML = "";
    history.forEach(row => { updateChart(row); prependHistory(row); });
  } catch {
    table.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--danger);">Không thể tải</td></tr>`;
  }
}

function updateLatestData(data) {
  $("temperature").textContent = data.temperature.toFixed(1);
  $("air_humidity").textContent = data.air_humidity.toFixed(1);
  $("soil_moisture").textContent = data.soil_moisture.toFixed(1);
  const badge = $("last-updated");
  badge.innerHTML = `<i class="fa-solid fa-check"></i> Cập nhật: ${data.created_at}`;
  badge.style.color = 'var(--primary)';
  badge.style.background = 'rgba(16, 185, 129, 0.1)';
  badge.style.borderColor = 'rgba(16, 185, 129, 0.2)';
}

function updateMqttStatus(isConnected) {
  const badge = $("mqtt-status");
  if (isConnected) {
    badge.className = "status-badge connected";
    badge.innerHTML = '<i class="fa-solid fa-link"></i> Đã kết nối MQTT';
  } else {
    badge.className = "status-badge disconnected";
    badge.innerHTML = '<i class="fa-solid fa-link-slash"></i> Mất kết nối MQTT';
  }
}

// Device state sync
let isSendingControl = false;

function updateDeviceState(state) {
  const toggles = [
    { id: 'toggle-pump', label: 'pump-label', key: 'pump' },
    { id: 'toggle-fan', label: 'fan-label', key: 'fan' },
    { id: 'toggle-light', label: 'light-label', key: 'light' },
  ];

  toggles.forEach(({ id, label, key }) => {
    const input = $(id);
    const lbl = $(label);
    if (!input || !lbl) return;
    const isOn = !!state[key];
    input.checked = isOn;
    lbl.textContent = isOn ? "Đang bật" : "Đang tắt";
    lbl.className = `toggle-status ${isOn ? 'on' : 'off'}`;
  });

  const isManual = state.mode === "manual";
  
  const modeInput = $('toggle-mode');
  const modeLabel = $('mode-label');
  if (modeInput && modeLabel) {
    modeInput.checked = !isManual;
    modeLabel.textContent = isManual ? "THỦ CÔNG" : "TỰ ĐỘNG";
    modeLabel.style.color = isManual ? "#ef4444" : "#10b981";
  }

  $$('.control-card').forEach(card => {
    if (card.querySelector('#toggle-mode')) return;
    card.classList.toggle('disabled', !isManual);
  });
}

function setupToggle(toggleId, onAction, offAction) {
  const input = $(toggleId);
  if (!input) return;
  input.addEventListener('change', async () => {
    if (isSendingControl) return;
    isSendingControl = true;
    const action = input.checked ? onAction : offAction;
    try {
      await api("/api/control", {
        method: "POST", body: JSON.stringify({ action }),
      });
      showToast(`Đã gửi lệnh: ${input.checked ? 'Bật' : 'Tắt'}`);
    } catch {
      input.checked = !input.checked;
    } finally {
      isSendingControl = false;
    }
  });
}

function initSSE() {
  const evtSource = new EventSource("/api/stream");
  evtSource.onmessage = function(event) {
    try {
      const payload = JSON.parse(event.data);
      if (payload.type === 'status') {
        updateMqttStatus(payload.data.mqtt_connected);
      } else if (payload.type === 'sensor') {
        updateLatestData(payload.data);
        updateChart(payload.data);
        prependHistory(payload.data);
      } else if (payload.type === 'device_state') {
        updateDeviceState(payload.data);
      }
    } catch(err) {
      console.error("SSE parse error", err);
    }
  };
  evtSource.onerror = () => updateMqttStatus(false);
}

async function loadDeviceState() {
  try {
    const state = await api("/api/device-state");
    updateDeviceState(state);
  } catch {}
}

// --- AUTOMATION MODAL ---

function openSettings(device, deviceName) {
  currentDevice = device;
  $('modal-title').textContent = `Cài đặt tự động: ${deviceName}`;
  $('settings-modal').classList.add('active');
  loadAutomations();
}

function closeSettings() {
  $('settings-modal').classList.remove('active');
  currentDevice = null;
}

function switchModalTab(tabId) {
  $$('.tab-btn').forEach(btn => btn.classList.remove('active'));
  $$('.tab-content').forEach(content => content.classList.remove('active'));
  document.querySelector(`.tab-btn[onclick="switchModalTab('${tabId}')"]`).classList.add('active');
  $(`tab-${tabId}`).classList.add('active');
}

async function loadAutomations() {
  if (!currentDevice) return;
  const list = await api(`/api/automations?device=${currentDevice}`);
  renderAutomations(list);
}

function renderAutomations(items) {
  const threshList = $('list-threshold');
  const schedList = $('list-schedule');
  threshList.innerHTML = '';
  schedList.innerHTML = '';

  items.forEach(item => {
    const isThresh = item.type === 'threshold';
    const row = document.createElement('div');
    row.className = 'auto-item';
    
    let infoHtml = '';
    if (isThresh) {
      const sMap = {soil_moisture: "Độ ẩm đất", temperature: "Nhiệt độ", air_humidity: "Độ ẩm KK"};
      const aMap = {on: "Bật", off: "Tắt"};
      infoHtml = `
        <div class="auto-info">
          <strong>${sMap[item.sensor] || item.sensor} ${item.condition} ${item.threshold_value}</strong>
          <span>=> ${aMap[item.action] || item.action} (${item.duration_sec === 0 ? 'Vô hạn' : item.duration_sec + 's'})</span>
        </div>
      `;
    } else {
      infoHtml = `
        <div class="auto-info">
          <strong><i class="fa-regular fa-clock"></i> ${item.time_of_day} - ${item.end_time || 'K.Rõ'}</strong>
          <span>=> Bật từ ${item.time_of_day} và Tắt lúc ${item.end_time || 'K.Rõ'}</span>
        </div>
      `;
    }

    row.innerHTML = `
      ${infoHtml}
      <div style="display:flex; gap:10px; align-items:center;">
        <label class="toggle-switch">
          <input type="checkbox" ${item.is_enabled ? 'checked' : ''} onchange="toggleAutomation(${item.id}, this.checked)" />
          <span class="slider"></span>
        </label>
        <button class="btn sm danger settings-btn" style="color: var(--danger); padding:0; background:none;" onclick="deleteAutomation(${item.id})"><i class="fa-solid fa-trash"></i></button>
      </div>
    `;

    if (isThresh) threshList.appendChild(row);
    else schedList.appendChild(row);
  });
}

window.toggleAutomation = async function(id, enabled) {
  await api(`/api/automations/${id}/toggle`, {
    method: "POST", body: JSON.stringify({ enabled }),
  });
  showToast(`Đã ${enabled ? 'bật' : 'tắt'} luật tự động`);
}

function customConfirm(message) {
  return new Promise((resolve) => {
    const modal = $('confirm-modal');
    $('confirm-message').textContent = message;
    modal.classList.add('active');

    const btnOk = $('confirm-ok');
    const btnCancel = $('confirm-cancel');

    const cleanup = () => {
      modal.classList.remove('active');
      btnOk.removeEventListener('click', onOk);
      btnCancel.removeEventListener('click', onCancel);
    };

    const onOk = () => { cleanup(); resolve(true); };
    const onCancel = () => { cleanup(); resolve(false); };

    btnOk.addEventListener('click', onOk);
    btnCancel.addEventListener('click', onCancel);
  });
}

window.deleteAutomation = async function(id) {
  const confirmed = await customConfirm("Bạn có chắc chắn muốn xóa luật này?");
  if (!confirmed) return;
  await api(`/api/automations/${id}`, { method: "DELETE" });
  showToast("Đã xóa");
  await loadAutomations();
}

window.addAutomation = async function(type) {
  if (!currentDevice) return;
  const payload = { device: currentDevice, type };
  
  if (type === 'threshold') {
    payload.sensor = $('auto-sensor').value;
    payload.condition = $('auto-condition').value;
    payload.threshold_value = parseFloat($('auto-value').value);
    payload.action = $('auto-action-thresh').value;
    payload.duration_sec = parseInt($('auto-duration-thresh').value);
  } else {
    payload.time_of_day = $('auto-time-start').value;
    payload.end_time = $('auto-time-end').value;
    if (!payload.time_of_day || !payload.end_time) return showToast("Vui lòng nhập cả giờ bật và tắt", "error");
  }

  await api("/api/automations", {
    method: "POST", body: JSON.stringify(payload)
  });
  showToast("Đã thêm luật tự động mới");
  await loadAutomations();
}

function initActions() {
  setupToggle("toggle-mode", "mode_auto", "mode_manual");
  setupToggle("toggle-pump", "pump_on", "pump_off");
  setupToggle("toggle-fan", "fan_on", "fan_off");
  setupToggle("toggle-light", "light_on", "light_off");
}

async function bootstrap() {
  initNavigation();
  initChart();
  initSSE();
  try {
    await Promise.all([loadInitialHistory(), loadDeviceState()]);
    initActions();
  } catch (err) {
    console.error("Bootstrap error:", err);
  }
}

bootstrap();
