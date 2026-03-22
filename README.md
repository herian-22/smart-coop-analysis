# 🐔 SmartCoop Analysis

Sistem monitoring kandang ayam cerdas berbasis **ESP32** yang mengintegrasikan sensor lingkungan real-time dengan deteksi anomali berbasis kecerdasan buatan (**TensorFlow Lite for Microcontrollers**). Sistem ini mampu melakukan pembelajaran adaptif secara online dan mengirimkan data telemetri ke cloud melalui protokol MQTT.

---

## 📋 Daftar Isi

- [Fitur Utama](#fitur-utama)
- [Arsitektur Sistem](#arsitektur-sistem)
- [Hardware yang Digunakan](#hardware-yang-digunakan)
- [Struktur Direktori](#struktur-direktori)
- [Cara Kerja AI/ML](#cara-kerja-aiml)
- [Konektivitas & Cloud](#konektivitas--cloud)
- [Manajemen Daya](#manajemen-daya)
- [Cara Build & Flash](#cara-build--flash)
- [Dependensi](#dependensi)
- [Format Data Telemetri](#format-data-telemetri)
- [Lisensi](#lisensi)

---

## ✨ Fitur Utama

- 🌡️ **Monitoring real-time** suhu & kelembaban menggunakan sensor SHT3x
- 🤖 **Deteksi anomali berbasis AI** menggunakan Windowed Autoencoder (TFLite Micro)
- 🔄 **Pembelajaran online & self-calibration** dengan algoritma Reinforcement Learning
- ☁️ **Konektivitas cloud** via MQTT untuk pemantauan jarak jauh
- 🌐 **Antarmuka web** untuk konfigurasi WiFi secara nirkabel
- 🖥️ **LCD 16×2** menampilkan data sensor secara langsung
- 📡 **OTA (Over-The-Air)** update firmware tanpa kabel
- 💾 **Penyimpanan persisten** status model di NVS (Non-Volatile Storage)
- ⚡ **Manajemen daya dinamis** dengan Dynamic Frequency Scaling (DFS)

---

## 🏗️ Arsitektur Sistem

Sistem berjalan di atas **FreeRTOS** dengan 5 task yang berjalan secara paralel:

| Task | Prioritas | Stack | Fungsi |
|------|-----------|-------|--------|
| **Network Stack** | 5 (tertinggi) | 8 KB | Inisialisasi WiFi, NTP, MQTT |
| **TinyML Anomaly Detection** | 2 | 16 KB | Inferensi AI & analisis tren |
| **LCD Update** | 2 | 8 KB | Tampilan status & data sensor |
| **SHT3x Sensor Read** | 3 | 8 KB | Pembacaan suhu/kelembaban tiap 2 detik |
| **Analog Read** | 1 (terendah) | 4 KB | Placeholder sensor analog masa depan |

### Pipeline Data

```
┌─────────────────────────────────────────────────────────┐
│  SHT3x Sensor (setiap 2 detik)                          │
└──────────────────┬──────────────────────────────────────┘
                   │ (I2C mutex-protected)
                   ▼
┌──────────────────────────────────────────────────────────┐
│  SensorData Global Struct (dataMutex protected)          │
│  - temperature, humidity, timestamp                      │
│  - anomaly_detected, mae, inference metrics              │
└──────────────────┬──────────────────────────────────────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
   ┌─────────────┐    ┌──────────────────┐
   │  LCD Task   │    │  Sensor Queue    │
   └─────────────┘    │ (30-sample FIFO) │
                      └────────┬─────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │  TinyML Task         │
                    │  - Windowed Buffer   │
                    │  - Inferensi AI      │
                    │  - RL Feedback       │
                    │  - NVS Persistence   │
                    └────────┬─────────────┘
                             │
                             ▼
                    ┌──────────────────────┐
                    │  MQTT Publisher      │
                    │  (JSON telemetry)    │
                    └──────────────────────┘
```

---

## 🔧 Hardware yang Digunakan

**Mikrokontroler:** ESP32

| Komponen | Model/Tipe | Fungsi | Antarmuka |
|----------|-----------|--------|-----------|
| Sensor Suhu & Kelembaban | SHT3x | Monitoring lingkungan | I2C (addr: `0x44`) |
| LCD Display | HD44780 16×2 | Tampilan UI | I2C via PCF8574 (addr: `0x27`) |
| I/O Expander | PCF8574 | Kontrol LCD via I2C | I2C |

**Pin I2C:**
- SDA: GPIO 21
- SCL: GPIO 22

**Batas Aman Lingkungan:**
- Suhu: **20°C – 34°C**
- Kelembaban: **40% – 90%**

---

## 📁 Struktur Direktori

```
smart-coop-analysis/
├── main/
│   ├── src/
│   │   ├── main.cpp                   # Entry point, inisialisasi WiFi, pembuatan task
│   │   ├── hardware.cpp               # Driver I2C, SHT3x, LCD, sensor analog
│   │   ├── mqtt_handler.cpp           # MQTT client, publikasi telemetri
│   │   ├── web_server.cpp             # HTTP server, UI konfigurasi WiFi, OTA
│   │   ├── rl_feedback.cpp            # Reinforcement learning policy agent
│   │   └── tinyml/
│   │       ├── tinyml_task.cpp        # ML task utama, window management, inferensi
│   │       └── synthetic_inference.cpp# TFLite interpreter, kuantisasi
│   ├── include/
│   │   ├── app_config.h               # Definisi pin, batas lingkungan, konfigurasi
│   │   ├── hardware.h                 # Deklarasi hardware task
│   │   ├── mqtt_handler.h             # MQTT API
│   │   ├── rl_feedback.h              # RL agent API
│   │   ├── tinyml_task.h              # ML task API
│   │   ├── synthetic_inference.h      # Inference API
│   │   └── model/
│   │       ├── model_params.h         # Ukuran window, jumlah fitur, parameter kuantisasi
│   │       └── synthetic_autoencoder.h# Binary model yang sudah terkuantisasi
│   ├── synthetic_autoencoder.tflite   # Model TFLite (30×2 → reconstruction error)
│   ├── synthetic_autoencoder.h5       # Model Keras original (untuk referensi)
│   └── CMakeLists.txt                 # Registrasi komponen
├── CMakeLists.txt                     # CMake root project
├── partitions.csv                     # Tabel partisi flash dengan dukungan OTA
├── sdkconfig.defaults                 # Konfigurasi default ESP-IDF
└── dependencies.lock                  # Lock file dependensi komponen
```

---

## 🤖 Cara Kerja AI/ML

### Model: Windowed Autoencoder (Terkuantisasi int8)

| Parameter | Nilai |
|-----------|-------|
| Tipe Model | Quantized Autoencoder |
| Input | 30 sampel × 2 fitur (suhu, kelembaban) |
| Stride | 5 sampel antar inferensi |
| Arena TFLite | 64 KB |
| Framework | TensorFlow Lite for Microcontrollers |

### Strategi Deteksi Anomali

Sistem menggunakan **reconstruction error (MAE)** dari autoencoder untuk mendeteksi anomali:

```
Anomali = (MAE > threshold_dinamis) ATAU (tren MAE naik selama 4+ langkah)
```

**Perhitungan Threshold Dinamis:**
```
threshold = rata_rata_MAE + (multiplier_RL × standar_deviasi_MAE)
```

### Online Learning (Algoritma Welford)

- Secara kontinu memperbarui rata-rata dan variansi MAE selama 30 sampel awal (warmup)
- Menyimpan statistik ke NVS agar bisa dipulihkan setelah reboot
- Memungkinkan adaptasi baseline terhadap drift sensor

### Reinforcement Learning Feedback Loop

Agent RL menyesuaikan sensitivitas deteksi anomali secara adaptif:

| Kondisi | Reward | Aksi |
|---------|--------|------|
| True Positive (bahaya terdeteksi dengan benar) | `+1.0` | — |
| True Negative (kondisi normal teridentifikasi benar) | `+0.1` | — |
| False Positive (alarm palsu pada kondisi normal) | `-1.0` | Longgarkan threshold (`+0.05`) |
| False Negative (bahaya tidak terdeteksi) | `-2.0` | Perketat threshold (`-0.05`, min 1.0) |

---

## ☁️ Konektivitas & Cloud

### WiFi
- **Mode:** STA (client) atau APSTA (AP + STA simultan)
- **Konfigurasi:** Web UI pada AP `SmartCoop_Config` jika belum dikonfigurasi
- **NTP Sync:** Sinkronisasi waktu otomatis (pool.ntp.org, Cloudflare, Google)
- **Timezone:** WIB (UTC+7)

### MQTT
- **Topik Publish:**
  - `smartcoop/sensor` — Telemetri utama (JSON)
  - `smartcoop/anomaly` — Notifikasi anomali

### Web Server (Port 80)

| Endpoint | Metode | Fungsi |
|----------|--------|--------|
| `/scan` | GET | Daftar jaringan WiFi |
| `/config` | POST | Simpan kredensial WiFi & ID kandang |
| `/update` | POST | Upload firmware OTA |

---

## ⚡ Manajemen Daya

- **DFS (Dynamic Frequency Scaling):** Frekuensi CPU adaptif 40–240 MHz
- **Light Sleep:** Dinonaktifkan (untuk stabilitas WiFi)
- **FreeRTOS Tick:** 1000 Hz dengan tickless idle aktif

---

## 🚀 Cara Build & Flash

### Prasyarat

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) versi 5.x
- CMake ≥ 3.16
- Python ≥ 3.8

### Langkah Build

```bash
# 1. Clone repository
git clone https://github.com/herian-22/smart-coop-analysis.git
cd smart-coop-analysis

# 2. Set target ESP32
idf.py set-target esp32

# 3. Build project
idf.py build

# 4. Flash ke perangkat
idf.py -p /dev/ttyUSB0 flash

# 5. Monitor output serial
idf.py -p /dev/ttyUSB0 monitor
```

### Konfigurasi Awal

1. Setelah flash, perangkat akan membuat hotspot WiFi bernama **`SmartCoop_Config`**
2. Hubungkan ke hotspot tersebut dan buka browser ke `http://192.168.4.1`
3. Masukkan SSID, password WiFi, dan ID kandang, lalu simpan
4. Perangkat akan restart dan terhubung ke jaringan WiFi yang dikonfigurasi

---

## 📦 Dependensi

### Komponen Eksternal (idf_component.yml)

| Komponen | Versi | Fungsi |
|----------|-------|--------|
| `esp-idf-lib/sht3x` | `^1.0.8` | Driver sensor suhu & kelembaban |
| `esp-idf-lib/hd44780` | `^1.0.0` | Driver LCD display |
| `esp-idf-lib/pcf8574` | `^1.0.0` | Driver I2C port expander |
| `espressif/esp-tflite-micro` | `^1.0.0` | TensorFlow Lite for Microcontrollers |

### Komponen Built-in ESP-IDF

`esp_wifi` · `esp_event` · `esp_netif` · `mqtt` · `esp_http_server` · `nvs_flash` · `esp_timer` · `esp_pm`

---

## 📊 Format Data Telemetri

Data sensor dipublikasikan ke MQTT dalam format JSON:

```json
{
  "temperature": 30.67,
  "humidity": 82.49,
  "timestamp": "2026/03/23 04:00:01",
  "anomaly": false,
  "mae": 0.0806,
  "latency_us": 18500,
  "epoch_ms": 1742745301000
}
```

### CSV Logging (UART)

Sistem juga menghasilkan output CSV melalui serial untuk analisis offline:

```
--- START CSV DATA ---
Timestamp,Temperature,Humidity,Anomaly
2026/03/23 04:00:01,30.67,82.49,0
2026/03/23 04:00:03,30.70,82.50,0
```

---

## 📄 Lisensi

Proyek ini dikembangkan untuk keperluan penelitian dan edukasi.

---

<div align="center">
  <p>Dikembangkan dengan ❤️ untuk pemantauan kandang ayam yang lebih cerdas</p>
</div>
