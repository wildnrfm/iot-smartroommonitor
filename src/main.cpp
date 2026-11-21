/*
  SMART ROOM MONITOR - versi perangkat asli (Kelompok 5)
  ESP32 + DHT22 + LDR + PIR + Relay + Blynk IoT
  Diturunkan dari Smart Room Monitor v2 (mini project, Wokwi).

  Datastream: V0 suhu, V1 kelembapan, V2 cahaya, V3 kipas, V4 mode manual,
              V5 batas suhu, V6 status, V7 keberadaan orang, V8 mute alarm.

  Perubahan dari versi simulasi:
  - DHT22 pindah ke GPIO4 (Bagian 5 dokumen), bukan GPIO15.
  - BlynkSimpleEsp32_SSL.h (TLS, port 443). Jalur DNS/IP cadangan khusus simulator dibuang.
  - Token dan WiFi dibaca dari include/secrets.h (tidak ikut di-commit).
  - Polaritas relay dan buzzer dibuat konstanta. Buzzer aktif dibunyikan non-blocking
    (300 ms tiap 2 detik), bukan digetarkan 1 kHz.
  - Histeresis suhu 1 C. Arah ambang LDR bisa dibalik.
  - Data sensor dikirim tiap 30 detik (hemat kuota pesan). V8 ditambah untuk mute dari aplikasi.
  - Kedua LED berkedip 2x saat boot (penanda jika ESP32 reset sendiri).
  Bagian bertanda "SESUAIKAN" diisi dari hasil uji komponen (Tahap 4).
*/

// ===== 1. DATA BLYNK DAN WIFI =====
// BLYNK_TEMPLATE_ID, BLYNK_TEMPLATE_NAME, BLYNK_AUTH_TOKEN, WIFI_SSID, dan WIFI_PASS
// ada di include/secrets.h (salin dari secrets.h.example). Jangan di-commit.
#include "secrets.h"
#define BLYNK_PRINT Serial
#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32_SSL.h>   // TLS (port 443). BlynkSimpleEsp32.h memakai port 80 tanpa enkripsi
#include <DHT.h>

// ===== 2. PIN (Bagian 5 dokumen rancangan) =====
#define DHT_PIN 4         // DATA DHT22 (bukan GPIO15)
#define DHT_TYPE DHT22
#define LDR_PIN 34        // AO modul photoresistor
#define LED_ALARM 26      // LED merah
#define LED_LAMPU 27      // LED kuning
#define BUZZER_PIN 25     // modul buzzer aktif
#define RELAY_PIN 13      // relay untuk kipas
#define PIR_PIN 32        // OUT sensor PIR
#define TOMBOL_PIN 33     // tombol mute (satu kaki ke GND)

// ===== 3. PENGATURAN =====
float batasSuhu = 30.0;                          // derajat C, bisa diubah lewat slider V5
const float HISTERESIS_C = 1.0;                  // alarm dan kipas mati saat suhu <= batas - 1 C
const float BATAS_LEMBAP = 70.0;
const unsigned long LAMPU_TAHAN_MS = 10000;      // lampu tetap nyala 10 detik setelah gerakan terakhir
const unsigned long EVENT_JEDA_MS = 30000;       // jeda minimal antar notifikasi
const unsigned long KIRIM_MS = 30000;            // data sensor ke Blynk tiap 30 detik (hemat kuota pesan)
const unsigned long COBA_BLYNK_MS = 30000;       // jeda antar percobaan menyambung ke Blynk saat offline
const unsigned long BUZZER_PERIODE_MS = 2000;    // alarm: bunyi 300 ms setiap 2 detik
const unsigned long BUZZER_BUNYI_MS = 300;

// --- SESUAIKAN setelah uji komponen (Tahap 4) ---
const bool RELAY_ACTIVE_LOW = false;  // dari pengujian: modul relay aktif saat pin HIGH
const bool BUZZER_ACTIVE_LOW = false;  // buzzer 2-kaki: + ke GPIO25, - ke GND
const bool LDR_DARK_IS_HIGH = true;    // true: nilai makin besar = makin gelap
const int LDR_GELAP = 2000;            // ambang gelap (skala 0-4095), kalibrasi di kamar
// -------------------------------------------------
const uint8_t RELAY_ON = RELAY_ACTIVE_LOW ? LOW : HIGH;
const uint8_t RELAY_OFF = RELAY_ACTIVE_LOW ? HIGH : LOW;
const uint8_t BUZZER_ON = BUZZER_ACTIVE_LOW ? LOW : HIGH;
const uint8_t BUZZER_OFF = BUZZER_ACTIVE_LOW ? HIGH : LOW;

// ===== 4. VARIABEL =====
DHT dht(DHT_PIN, DHT_TYPE);
BlynkTimer timer;

int cahaya = 0;
bool gelap = false;
bool adaOrang = false;
bool lampuNyala = false;
bool kipasNyala = false;
bool modeManual = false;       // V4: false = otomatis, true = manual
bool kipasManual = false;      // V3: perintah kipas saat mode manual
bool alarmDibisukan = false;   // tombol fisik atau V8
bool panasAktif = false;       // suhu di atas batas (dengan histeresis)
bool panasSebelumnya = false;
bool pernahGerak = false;
unsigned long terakhirGerak = 0;

bool pernahEvent = false;
unsigned long terakhirEvent = 0;

unsigned long terakhirKirim = 0;
bool pernahKirim = false;
String statusTerkirim = "";
int orangTerkirim = -1;
int kipasTerkirim = -1;
int muteTerkirim = -1;

bool wifiDilaporkan = false;
bool pernahCobaBlynk = false;
unsigned long terakhirCobaBlynk = 0;

// ===== 5. KONTROL DARI BLYNK =====
BLYNK_CONNECTED() {
  Serial.println("Terhubung ke Blynk Cloud");
  pernahKirim = false;
  statusTerkirim = "";
  orangTerkirim = -1;
  kipasTerkirim = -1;
  muteTerkirim = -1;
  Blynk.syncVirtual(V4, V5);  // ambil mode dan batas suhu; status kipas diperbarui dari firmware
}

// Switch Kipas (V3) selalu mengambil alih ke mode manual agar perintah tidak diabaikan.
BLYNK_WRITE(V3) {
  if (!modeManual) {
    modeManual = true;
    Blynk.virtualWrite(V4, 1);
    Serial.println("Mode: MANUAL (dari switch kipas)");
  }
  kipasManual = (param.asInt() == 1);
  kipasTerkirim = kipasManual ? 1 : 0;
}

// Switch Mode Manual (V4)
BLYNK_WRITE(V4) {
  modeManual = (param.asInt() == 1);
  if (modeManual) {
    kipasManual = kipasNyala;
    Blynk.virtualWrite(V3, kipasManual ? 1 : 0);
  } else {
    kipasTerkirim = -1;
  }
  Serial.println(modeManual ? "Mode: MANUAL" : "Mode: OTOMATIS");
}

// Slider Batas Suhu (V5)
BLYNK_WRITE(V5) {
  batasSuhu = param.asFloat();
  Serial.printf("Batas suhu diubah: %.1f C\n", batasSuhu);
}

// Switch Mute Alarm (V8): 1 = bisukan buzzer, 0 = aktifkan kembali
BLYNK_WRITE(V8) {
  alarmDibisukan = (param.asInt() == 1);
  Serial.println(alarmDibisukan ? "Buzzer DIBISUKAN (dari aplikasi)" : "Buzzer AKTIF kembali (dari aplikasi)");
}

// ===== 6. FUNGSI BANTU =====
bool bolehKirimEvent() {
  if (!pernahEvent || millis() - terakhirEvent >= EVENT_JEDA_MS) {
    pernahEvent = true;
    terakhirEvent = millis();
    return true;
  }
  return false;
}

// Jika internet bermasalah, alarm, kipas, dan lampu tetap bekerja.
void urusKoneksi() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!wifiDilaporkan) {
    wifiDilaporkan = true;
    Serial.println("WiFi terhubung, IP: " + WiFi.localIP().toString());
  }

  if (Blynk.connected()) {
    Blynk.run();
    return;
  }

  if (!pernahCobaBlynk || millis() - terakhirCobaBlynk >= COBA_BLYNK_MS) {
    pernahCobaBlynk = true;
    Serial.println("Mencoba menyambung ke Blynk...");
    Blynk.connect(5000);
    terakhirCobaBlynk = millis();
  }
}

// ===== 7. TUGAS CEPAT (tiap 50 ms): tombol mute, lampu pintar, buzzer =====
void tugasCepat() {
  // 1) Tombol mute: dideteksi saat tombol baru ditekan
  static bool tombolSebelumnya = HIGH;
  bool tombol = digitalRead(TOMBOL_PIN);
  if (tombolSebelumnya == HIGH && tombol == LOW) {
    alarmDibisukan = !alarmDibisukan;
    Serial.println(alarmDibisukan ? "Buzzer DIBISUKAN" : "Buzzer AKTIF kembali");
  }
  tombolSebelumnya = tombol;

  // 2) Lampu pintar: nyala jika gelap DAN ada orang
  cahaya = analogRead(LDR_PIN);
  gelap = LDR_DARK_IS_HIGH ? (cahaya > LDR_GELAP) : (cahaya < LDR_GELAP);
  adaOrang = (digitalRead(PIR_PIN) == HIGH);
  if (adaOrang) {
    terakhirGerak = millis();
    pernahGerak = true;
  }
  bool orangMasihAda = pernahGerak && (millis() - terakhirGerak < LAMPU_TAHAN_MS);
  lampuNyala = gelap && orangMasihAda;
  digitalWrite(LED_LAMPU, lampuNyala ? HIGH : LOW);

  // 3) Buzzer: selama alarm aktif dan tidak dibisukan, bunyi 300 ms setiap 2 detik
  bool bunyi = panasAktif && !alarmDibisukan && (millis() % BUZZER_PERIODE_MS < BUZZER_BUNYI_MS);
  digitalWrite(BUZZER_PIN, bunyi ? BUZZER_ON : BUZZER_OFF);
}

// ===== 8. TUGAS SENSOR (tiap 2 detik): suhu, alarm, kipas, kirim data =====
void tugasSensor() {
  float suhu = dht.readTemperature();
  float lembap = dht.readHumidity();
  if (isnan(suhu) || isnan(lembap)) {
    Serial.println("Gagal membaca DHT22");
    return;
  }

  // Suhu di atas batas, dengan histeresis supaya relay tidak berkedip di sekitar batas
  if (!panasAktif && suhu > batasSuhu) panasAktif = true;
  else if (panasAktif && suhu <= batasSuhu - HISTERESIS_C) panasAktif = false;
  bool panas = panasAktif;
  bool lembab = lembap > BATAS_LEMBAP;

  // Pembisuan alarm otomatis dibatalkan ketika suhu sudah normal
  if (!panas) alarmDibisukan = false;
  digitalWrite(LED_ALARM, panas ? HIGH : LOW);

  // Kipas: otomatis mengikuti suhu, atau manual dari Blynk
  kipasNyala = modeManual ? kipasManual : panas;
  digitalWrite(RELAY_PIN, kipasNyala ? RELAY_ON : RELAY_OFF);

  // Status ruangan (urutan prioritas: panas > lembap > gelap > nyaman)
  const char* status = "Nyaman";
  if (panas) status = "Panas";
  else if (lembab) status = "Lembap";
  else if (gelap) status = "Gelap";

  // Notifikasi hanya saat suhu baru melewati batas (bukan setiap 2 detik)
  if (panas && !panasSebelumnya && Blynk.connected() && bolehKirimEvent()) {
    Blynk.logEvent("suhu_tinggi",
                   String("Suhu ruangan ") + String(suhu, 1) + " C, melebihi batas " +
                   String(batasSuhu, 0) + " C");
  }
  panasSebelumnya = panas;

  if (Blynk.connected()) {
    if (!pernahKirim || millis() - terakhirKirim >= KIRIM_MS) {
      pernahKirim = true;
      terakhirKirim = millis();
      Blynk.virtualWrite(V0, suhu);
      Blynk.virtualWrite(V1, lembap);
      Blynk.virtualWrite(V2, cahaya);
    }
    if (statusTerkirim != status) {
      statusTerkirim = status;
      Blynk.virtualWrite(V6, status);
    }
    int orang = adaOrang ? 1 : 0;
    if (orangTerkirim != orang) {
      orangTerkirim = orang;
      Blynk.virtualWrite(V7, orang);
    }
    int kipas = kipasNyala ? 1 : 0;
    if (!modeManual && kipasTerkirim != kipas) {
      kipasTerkirim = kipas;
      Blynk.virtualWrite(V3, kipas);
    }
    int mute = alarmDibisukan ? 1 : 0;
    if (muteTerkirim != mute) {
      muteTerkirim = mute;
      Blynk.virtualWrite(V8, mute);
    }
  }

  // Tampilkan di Terminal (format JSON)
  char payload[360];
  snprintf(payload, sizeof(payload),
           "{\"suhu\":%.1f,\"kelembapan\":%.1f,\"cahaya\":%d,\"panas\":%s,\"gelap\":%s,"
           "\"orang\":%s,\"lampu\":%s,\"kipas\":%s,\"mode\":\"%s\",\"mute\":%s,\"status\":\"%s\","
           "\"wifi\":%s,\"blynk\":%s}",
           suhu, lembap, cahaya,
           panas ? "true" : "false",
           gelap ? "true" : "false",
           adaOrang ? "true" : "false",
           lampuNyala ? "true" : "false",
           kipasNyala ? "true" : "false",
           modeManual ? "MANUAL" : "OTOMATIS",
           alarmDibisukan ? "true" : "false",
           status,
           (WiFi.status() == WL_CONNECTED) ? "true" : "false",
           Blynk.connected() ? "true" : "false");
  Serial.println(payload);
}

// ===== 9. SETUP & LOOP =====
void setup() {
  Serial.begin(115200);
  pinMode(LED_ALARM, OUTPUT);
  pinMode(LED_LAMPU, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(TOMBOL_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, RELAY_OFF);     // kipas mati saat start
  digitalWrite(BUZZER_PIN, BUZZER_OFF);   // buzzer mati saat start

  // Kedua LED berkedip 2x sebagai penanda boot. Jika kedip muncul sendiri saat
  // kipas hidup atau mati, berarti ESP32 reset (cek kapasitor, dioda, dan kabel kipas).
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_ALARM, HIGH); digitalWrite(LED_LAMPU, HIGH); delay(150);
    digitalWrite(LED_ALARM, LOW);  digitalWrite(LED_LAMPU, LOW);  delay(150);
  }

  dht.begin();
  Serial.println("Smart Room Monitor (perangkat asli) mulai...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // tidak menunggu internet, sistem lokal langsung jalan
  Blynk.config(BLYNK_AUTH_TOKEN);     // koneksi Blynk dicoba lewat urusKoneksi()

  timer.setInterval(2000L, tugasSensor);
  timer.setInterval(50L, tugasCepat);
}

void loop() {
  urusKoneksi();
  timer.run();
  delay(10);
}
