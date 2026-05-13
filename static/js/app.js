const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelectorAll(sel);

// Toast notification system
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

// Navigation system
function initNavigation() {
  const navItems = $$('.nav-item');
  const pages = $$('.page');
  const pageTitle = $('page-title');

  navItems.forEach(item => {
    item.addEventListener('click', (e) => {
      e.preventDefault();
      
      // Update active nav
      navItems.forEach(nav => nav.classList.remove('active'));
      item.classList.add('active');
      
      // Update title
      pageTitle.textContent = item.querySelector('span').textContent;
      
      // Show target page
      const targetId = item.getAttribute('data-target');
      pages.forEach(page => {
        if (page.id === targetId) {
          page.classList.add('active');
        } else {
          page.classList.remove('active');
        }
      });
    });
  });
}

// API wrapper
async function api(path, options = {}) {
  try {
    const response = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      ...options,
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    return await response.json();
  } catch (error) {
    showToast(`Lỗi kết nối: ${error.message}`, 'error');
    throw error;
  }
}

// Chart System
let sensorChart = null;
let chartData = {
  labels: [],
  temp: [],
  hum: [],
  soil: []
};

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
          label: 'Nhiệt độ (°C)',
          data: chartData.temp,
          borderColor: '#ef4444',
          backgroundColor: 'rgba(239, 68, 68, 0.1)',
          borderWidth: 2,
          tension: 0.4,
          fill: true
        },
        {
          label: 'Độ ẩm KK (%)',
          data: chartData.hum,
          borderColor: '#3b82f6',
          backgroundColor: 'rgba(59, 130, 246, 0.1)',
          borderWidth: 2,
          tension: 0.4,
          fill: true
        },
        {
          label: 'Độ ẩm đất (%)',
          data: chartData.soil,
          borderColor: '#8b5cf6',
          backgroundColor: 'rgba(139, 92, 246, 0.1)',
          borderWidth: 2,
          tension: 0.4,
          fill: true
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: {
        mode: 'index',
        intersect: false,
      },
      plugins: {
        legend: { labels: { color: textColor } }
      },
      scales: {
        x: {
          grid: { color: gridColor },
          ticks: { color: textColor }
        },
        y: {
          grid: { color: gridColor },
          ticks: { color: textColor }
        }
      }
    }
  });
}

function updateChart(dataPoint) {
  if (!sensorChart) return;
  
  const timeStr = dataPoint.created_at.split('T')[1] || dataPoint.created_at.split(' ')[1] || dataPoint.created_at;
  const timeOnly = timeStr.substring(0, 8);
  
  chartData.labels.push(timeOnly);
  chartData.temp.push(dataPoint.temperature);
  chartData.hum.push(dataPoint.air_humidity);
  chartData.soil.push(dataPoint.soil_moisture);
  
  // Keep max 20 points
  if (chartData.labels.length > 20) {
    chartData.labels.shift();
    chartData.temp.shift();
    chartData.hum.shift();
    chartData.soil.shift();
  }
  
  sensorChart.update('none'); // Use 'none' for smooth animation without full re-render
}

function prependHistory(row) {
  const table = $("history-table");
  
  // Xóa empty row message nếu có
  if (table.querySelector('td[colspan="4"]')) {
    table.innerHTML = "";
  }
  
  const tr = document.createElement("tr");
  tr.innerHTML = `
    <td>${row.created_at}</td>
    <td><span style="color: #ef4444; font-weight: 500;">${row.temperature.toFixed(1)} °C</span></td>
    <td><span style="color: #3b82f6; font-weight: 500;">${row.air_humidity.toFixed(1)} %</span></td>
    <td><span style="color: #8b5cf6; font-weight: 500;">${row.soil_moisture.toFixed(1)} %</span></td>
  `;
  table.prepend(tr);
  
  // Giữ tối đa 20 dòng trên UI
  if (table.children.length > 20) {
    table.lastElementChild.remove();
  }
}

async function loadInitialHistory() {
  const table = $("history-table");
  table.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">Đang tải dữ liệu...</td></tr>`;
  
  try {
    const history = await api("/api/history?limit=20");
    if (history.length === 0) {
      table.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--text-muted);">Chưa có dữ liệu</td></tr>`;
      return;
    }
    
    table.innerHTML = "";
    // History trả về mảng cũ nhất trước tiên để vẽ chart
    history.forEach((row) => {
      updateChart(row);
      prependHistory(row); // Prepend đẩy dòng mới nhất lên đầu bảng
    });
  } catch (err) {
    table.innerHTML = `<tr><td colspan="4" style="text-align: center; color: var(--danger);">Không thể tải lịch sử</td></tr>`;
  }
}

function renderSchedules(items) {
  const root = $("schedule-list");
  root.innerHTML = "";
  if (!items.length) {
    root.innerHTML = "<p style='color: var(--text-muted);'>Chưa có lịch tưới nào được thiết lập.</p>";
    return;
  }
  items.forEach((item) => {
    const row = document.createElement("div");
    row.className = "schedule-item";
    row.innerHTML = `
      <div class="schedule-info">
        <strong><i class="fa-regular fa-clock"></i> ${item.time_of_day}</strong>
        <span>Thời gian tưới: ${item.duration_sec}s | Lần cuối: ${item.last_run_date || "Chưa chạy"}</span>
      </div>
      <div class="schedule-actions">
        <button class="btn sm toggle-btn ${item.enabled ? '' : 'primary'}">
          <i class="fa-solid ${item.enabled ? 'fa-toggle-on' : 'fa-toggle-off'}"></i> ${item.enabled ? "Tắt" : "Bật"}
        </button>
        <button class="btn sm danger delete-btn"><i class="fa-solid fa-trash"></i> Xóa</button>
      </div>
    `;
    row.querySelector(".toggle-btn").onclick = async () => {
      await api(`/api/schedules/${item.id}/toggle`, {
        method: "POST",
        body: JSON.stringify({ enabled: !item.enabled }),
      });
      showToast(`Đã ${item.enabled ? 'tắt' : 'bật'} lịch tưới ${item.time_of_day}`);
      await loadSchedules();
    };
    row.querySelector(".delete-btn").onclick = async () => {
      if (confirm(`Bạn có chắc muốn xóa lịch lúc ${item.time_of_day}?`)) {
        await api("/api/schedules", {
          method: "DELETE",
          body: JSON.stringify({ id: item.id }),
        });
        showToast("Đã xóa lịch tưới");
        await loadSchedules();
      }
    };
    root.appendChild(row);
  });
}

function updateLatestData(data) {
  const badge = $("last-updated");
  $("temperature").textContent = data.temperature.toFixed(1);
  $("air_humidity").textContent = data.air_humidity.toFixed(1);
  $("soil_moisture").textContent = data.soil_moisture.toFixed(1);
  
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

// SSE Connection
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
      } else if (payload.type === 'init') {
        updateMqttStatus(payload.data.mqtt_connected);
        if (payload.data.latest) {
          updateLatestData(payload.data.latest);
        }
      }
    } catch(err) {
      console.error("SSE parse error", err);
    }
  };
  
  evtSource.onerror = function() {
    updateMqttStatus(false);
  };
}

async function loadSettings() {
  const settings = await api("/api/settings");
  $("threshold-enabled").checked = settings.threshold_enabled;
  $("soil-threshold").value = settings.soil_moisture_threshold;
  $("default-duration").value = settings.default_duration_sec;
}

async function loadSchedules() {
  const schedules = await api("/api/schedules");
  renderSchedules(schedules);
}

async function initActions() {
  const setupControlBtn = (btnId, action, durationId = null) => {
    const btn = $(btnId);
    if (!btn) return;
    
    btn.onclick = async () => {
      const originalHtml = btn.innerHTML;
      btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Đang gửi...';
      btn.disabled = true;
      
      const payload = { action };
      if (durationId) {
        payload.duration_sec = parseInt($(durationId).value || "8", 10);
      }
      
      try {
        await api("/api/control", {
          method: "POST",
          body: JSON.stringify(payload),
        });
        showToast(`Đã gửi lệnh: ${action}`);
      } finally {
        setTimeout(() => {
          btn.innerHTML = originalHtml;
          btn.disabled = false;
        }, 1000);
      }
    };
  };

  // Device Controls
  setupControlBtn("pump-on", "pump_on", "manual-duration");
  setupControlBtn("pump-off", "pump_off");
  setupControlBtn("fan-on", "fan_on");
  setupControlBtn("fan-off", "fan_off");
  setupControlBtn("light-on", "light_on");
  setupControlBtn("light-off", "light_off");

  // Settings Controls
  $("save-settings").onclick = async () => {
    const btn = $("save-settings");
    const originalText = btn.innerHTML;
    btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i> Đang lưu...';
    btn.disabled = true;
    
    try {
      await api("/api/settings", {
        method: "POST",
        body: JSON.stringify({
          threshold_enabled: $("threshold-enabled").checked,
          soil_moisture_threshold: parseFloat($("soil-threshold").value),
          default_duration_sec: parseInt($("default-duration").value || "8", 10),
        }),
      });
      showToast("Đã lưu cấu hình tự động");
      await loadSettings();
    } finally {
      btn.innerHTML = originalText;
      btn.disabled = false;
    }
  };

  $("add-schedule").onclick = async () => {
    const timeOfDay = $("schedule-time").value;
    const duration = parseInt($("schedule-duration").value || "10", 10);
    if (!timeOfDay) {
      showToast("Vui lòng chọn thời gian tưới", "error");
      return;
    }
    await api("/api/schedules", {
      method: "POST",
      body: JSON.stringify({ time_of_day: timeOfDay, duration_sec: duration }),
    });
    showToast("Đã thêm lịch tưới mới");
    $("schedule-time").value = "";
    await loadSchedules();
  };
}

async function bootstrap() {
  initNavigation();
  initChart();
  initSSE();
  
  try {
    await Promise.all([
      loadSettings(), 
      loadSchedules(), 
      loadInitialHistory()
    ]);
    await initActions();
  } catch (err) {
    console.error("Bootstrap error:", err);
  }
}

bootstrap();
