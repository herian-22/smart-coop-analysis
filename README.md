# 🤖 Mochi Robot: Context-Aware Desktop Companion

![Mochi Demo](docs/mochi_demo.gif)
*(Tambahkan file animasi GIF dari robot yang sedang berjalan di direktori `docs/mochi_demo.gif`)*

Mochi Robot adalah proyek *Smart Environment Monitor* berbasis **ESP32** dan **ESP-IDF**. Lebih dari sekadar termometer biasa, Mochi mengemas data lingkungan ke dalam karakter virtual interaktif yang ditampilkan pada layar LCD 16x2. Mochi akan bereaksi terhadap suhu ruangan (SHT31) dengan mengubah ekspresi wajah (animasi pixel art khusus) dan memberikan respons audio melalui Buzzer.

---

## ✨ Fitur Utama

- 🌡️ **Context-Aware Engine**: Robot "merasakan" suhu dan beralih otomatis menggunakan status mesin berbasis Hysteresis.
  - **NORMAL (22°C - 30°C)**: Santai, mengedip santai, pose tangan *Salute* (🫡).
  - **HOT (> 30°C)**: Kegerahan, keringat bercucuran (*sweat drip animation*), mulut "ngos-ngosan" mengipas tangan.
  - **COLD (< 22°C)**: Kedinginan, mulut bergetar (*chattering*), menggigil dengan cepat (jitter ±1 kolom).
  - **ERROR**: Sensor rusak/dicabut otomatis memunculkan wajah "pusing" dengan mata silang (✖_✖).
- 👾 **Custom LCD Pixel-Art Engine**: Sistem manajemen batas memori **CGRAM (8-slot limit)** yang memungkinkan "Live Swapping" untuk asset wajah (Mata, Mulut, Keringat, Tangan) sesuai mode yang aktif tanpa memori *overflow*.
- 🔊 **Audio Feedback (LEDC PWM)**: Integrasi Buzzer asinkronus dengan *Queue* FreeRTOS untuk memutar nada tanpa nge-*block* jalannya animasi I2C.
  - Nada *Boot* yang ceria (3 nada naik).
  - *Dual-beep* transisi penggantian status suhu.
  - Alarm bahaya/kritis ketika T > 35°C bersamaan kedipan lampu LCD.
- 🛡️ **Robust I2C Recovery**: Toleransi kesalahan bus hardware. Jika sensor SHT31 terputus secara fisik (Ghost Reads), ESP32 akan membaca *Fail Threshold* dan secara otomatis mereset I2C bus *driver* agar Mochi bisa menstabilkan dirinya.

---

## 🛠️ Persyaratan Perangkat Keras (Hardware)

| Komponen | Kegunaan | PIN ESP32 (Default) |
| --- | --- | --- |
| **ESP32 Dev Board** | Otak Komputasi (Task Berbasis RTOS) | - |
| **LCD 1602 + PCF8574** | Penampil antarmuka (Layar Animasi Mochi) | I2C |
| **SENSIRION SHT31** | Modul Sensor Suhu & Kelembapan Presisi | I2C |
| **Buzzer Aktif/Pasif** | Suara & *Alarm Feedback* | GPIO `33` |

### Konfigurasi Koneksi (Wiring)
*(Pastikan LCD dan SHT31 dihubungkan membagi jalur I2C yang sama)*
- **SDA** -> GPIO `21`
- **SCL** -> GPIO `22`
- **VCC/GND** -> Sesuaikan modul ke 3.3V / 5.0V.

---

## 🏗️ Struktur Direktori / Arsitektur

```text
mochi-robot/
├── main/
│   ├── include/
│   │   ├── app_config.h     (Pengaturan Suhu Threshold, Pin & Enum)
│   │   ├── face_assets.h    (Manajemen Slot Buffer LCD CGRAM)
│   │   ├── hardware.h       (Antarmuka I2C / Init Sensor)
│   │   └── lcd_face.h       (Loop Logic Animasi Wajah)
│   └── src/
│       ├── face_assets.cpp  (Desain Matrix Pixel Art Wajah)
│       ├── hardware.cpp     (Sensor Reading Task & Buzzer PWM Task)
│       ├── lcd_face.cpp     (Frame Renderer & State Machine Engine)
│       └── main.cpp         (Titik Entry & FreeRTOS Task Bootstrap)
└── CMakeLists.txt
```

---

## 🚀 Cara Menjalankan (Build & Flash)

Proyek ini dibangun menggunakan lingkungan ekosistem **ESP-IDF** (v5.0+ direkomendasikan).

1. **Clone repositori**:
   ```bash
   git clone <url-repo-anda>
   cd mochi-robot
   ```
2. **Atur Lingkungan ESP-IDF** (Jalankan terminal *ESP-IDF Command Prompt*):
   ```bash
   # Contoh (bisa berbeda tergantung OS lokal)
   . $HOME/esp/esp-idf/export.sh
   # Untuk Windows:
   # C:\Espressif\frameworks\esp-idf-v5.x\export.bat
   ```
3. **Build, Flash, & Buka Terminal Monitor**:
   ```bash
   idf.py build flash monitor
   ```

*(Gunakan `Ctrl+]` untuk keluar dari monitor).*

---

## 🔍 Logika Interaksi Animasi

Mochi memiliki *Event Loop* 100ms mandiri untuk merender bentuk tubuhnya berdasarkan `TickType_t` *(FreeRTOS Ticks)*. 
Tidak seperti program *Delay* konvensional, Animasi LCD Mochi sama sekali **TIDAK MENGGUNAKAN** `vTaskDelay` sembarangan untuk menahan pergerakannya, melainkan logika berbasis perbedaan rentang waktu Delta Time (*Elapsed Time*).
Sebagai hasil, elemen wajah dapat bergerak pada interval spesifik mereka masing-masing:
- Berkedip tidak dapat ditebak: **2000-6000ms** (Random).
- Kipas kepanasan lambat: **1000ms**.
- Gigil COLD yang panik: **80ms**.
- *Sweat Drip* Jatuh: **500ms**.

---
*Created by [Herian] — UGM Thesis Project*
