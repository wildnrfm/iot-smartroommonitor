/**
 * Smart Room Monitor
 *
 * Pemantauan suhu, kelembapan, cahaya, dan gerakan menggunakan ESP32.
 * Kontrol Blynk tersedia pada V3-V8, sedangkan pembacaan sensor pada V0-V2.
 *
 * Konfigurasi sensitif disimpan di include/secrets.h dan tidak di-commit.
 */

#include "secrets.h"
#define BLYNK_PRINT Serial
#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32_SSL.h>
#include <DHT.h>

// Konfigurasi pin.
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define LDR_PIN 34
#define LED_ALARM 26
#define LED_LAMPU 27
#define BUZZER_PIN 25
#define RELAY_PIN 13
#define PIR_PIN 32
#define TOMBOL_PIN 33

// Pengaturan sensor dan jadwal tugas.
float batasSuhu = 30.0;
const float HISTERESIS_C = 1.0;                  // alarm dan kipas mati saat suhu <= batas - 1 C
const float BATAS_LEMBAP = 70.0;
const unsigned long LAMPU_TAHAN_MS = 10000;
const unsigned long EVENT_JEDA_MS = 30000;
const unsigned long KIRIM_MS = 30000;
const unsigned long COBA_BLYNK_MS = 30000;
const unsigned long BUZZER_PERIODE_MS = 2000;
const unsigned long BUZZER_BUNYI_MS = 300;

// Konfigurasi komponen hasil pengujian.
const bool RELAY_ACTIVE_LOW = false;
const bool BUZZER_ACTIVE_LOW = false;
const bool LDR_DARK_IS_HIGH = true;
const int LDR_GELAP = 2000;

const uint8_t RELAY_ON = RELAY_ACTIVE_LOW ? LOW : HIGH;
const uint8_t RELAY_OFF = RELAY_ACTIVE_LOW ? HIGH : LOW;
const uint8_t BUZZER_ON = BUZZER_ACTIVE_LOW ? LOW : HIGH;
const uint8_t BUZZER_OFF = BUZZER_ACTIVE_LOW ? HIGH : LOW;

DHT dht(DHT_PIN, DHT_TYPE);
BlynkTimer timer;

int cahaya = 0;
bool gelap = false;
bool adaOrang = false;
bool lampuNyala = false;
bool kipasNyala = false;
bool modeManual = false;
bool kipasManual = false;
bool alarmDibisukan = false;
bool panasAktif = false;
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

BLYNK_CONNECTED() {
  Serial.println("Terhubung ke Blynk Cloud");
  pernahKirim = false;
  statusTerkirim = "";
  orangTerkirim = -1;
  kipasTerkirim = -1;
  muteTerkirim = -1;
  Blynk.syncVirtual(V4, V5);
}

// V3 mengalihkan kontrol kipas ke mode manual.
BLYNK_WRITE(V3) {
  if (!modeManual) {
    modeManual = true;
    Blynk.virtualWrite(V4, 1);
    Serial.println("Mode: MANUAL (dari switch kipas)");
  }
  kipasManual = (param.asInt() == 1);
  kipasTerkirim = kipasManual ? 1 : 0;
}

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

BLYNK_WRITE(V5) {
  batasSuhu = param.asFloat();
  Serial.printf("Batas suhu diubah: %.1f C\n", batasSuhu);
}

BLYNK_WRITE(V8) {
  alarmDibisukan = (param.asInt() == 1);
  Serial.println(alarmDibisukan ? "Buzzer DIBISUKAN (dari aplikasi)" : "Buzzer AKTIF kembali (dari aplikasi)");
}

bool bolehKirimEvent() {
  if (!pernahEvent || millis() - terakhirEvent >= EVENT_JEDA_MS) {
    pernahEvent = true;
    terakhirEvent = millis();
    return true;
  }
  return false;
}

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

// Tugas berjalan setiap 50 ms.
void tugasCepat() {
  static bool tombolSebelumnya = HIGH;
  bool tombol = digitalRead(TOMBOL_PIN);
  if (tombolSebelumnya == HIGH && tombol == LOW) {
    alarmDibisukan = !alarmDibisukan;
    Serial.println(alarmDibisukan ? "Buzzer DIBISUKAN" : "Buzzer AKTIF kembali");
  }
  tombolSebelumnya = tombol;

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

  bool bunyi = panasAktif && !alarmDibisukan && (millis() % BUZZER_PERIODE_MS < BUZZER_BUNYI_MS);
  digitalWrite(BUZZER_PIN, bunyi ? BUZZER_ON : BUZZER_OFF);
}

// Tugas berjalan setiap 2 detik.
void tugasSensor() {
  float suhu = dht.readTemperature();
  float lembap = dht.readHumidity();
  if (isnan(suhu) || isnan(lembap)) {
    Serial.println("Gagal membaca DHT22");
    return;
  }

  // Histeresis mencegah perubahan relay saat suhu berada dekat ambang.
  if (!panasAktif && suhu > batasSuhu) panasAktif = true;
  else if (panasAktif && suhu <= batasSuhu - HISTERESIS_C) panasAktif = false;
  bool panas = panasAktif;
  bool lembab = lembap > BATAS_LEMBAP;

  if (!panas) alarmDibisukan = false;
  digitalWrite(LED_ALARM, panas ? HIGH : LOW);

  kipasNyala = modeManual ? kipasManual : panas;
  digitalWrite(RELAY_PIN, kipasNyala ? RELAY_ON : RELAY_OFF);

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

void setup() {
  Serial.begin(115200);
  pinMode(LED_ALARM, OUTPUT);
  pinMode(LED_LAMPU, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(TOMBOL_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);

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
