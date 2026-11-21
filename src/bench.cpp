// =====================================================================
//  Penguji komponen (Tahap 4) - tanpa WiFi dan tanpa Blynk
//  Serial Monitor 115200. Nilai sensor tercetak tiap 2 detik.
//  Kirim huruf: r = LED merah, y = LED kuning, b = buzzer, k = relay, h = bantuan.
//  Tiap huruf MEMBALIK level pin. Untuk buzzer dan relay, catat level pin
//  (HIGH atau LOW) yang membuatnya AKTIF, lalu isi RELAY_ACTIVE_LOW dan
//  BUZZER_ACTIVE_LOW di main.cpp.
// =====================================================================
#include <Arduino.h>
#include <DHT.h>

constexpr uint8_t PIN_DHT   = 4;
constexpr uint8_t PIN_LDR   = 34;
constexpr uint8_t PIN_PIR   = 32;
constexpr uint8_t PIN_BTN   = 33;
constexpr uint8_t PIN_LED_R = 26;
constexpr uint8_t PIN_LED_Y = 27;
constexpr uint8_t PIN_BUZZ  = 25;
constexpr uint8_t PIN_RELAY = 13;

DHT dht(PIN_DHT, DHT22);
bool lvlR = false, lvlY = false, lvlBuzz = false, lvlRelay = false;   // level pin saat ini
uint32_t lastPrint = 0;

void help() {
  Serial.println(F("Perintah: r=LED merah, y=LED kuning, b=buzzer, k=relay, h=bantuan"));
  Serial.println(F("Tiap perintah membalik level pin. Catat level (HIGH/LOW) yang membuat buzzer/relay AKTIF."));
}

void flip(uint8_t pin, bool &level, const char *name) {
  level = !level;
  digitalWrite(pin, level ? HIGH : LOW);
  Serial.printf("%s: pin = %s\n", name, level ? "HIGH" : "LOW");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_Y, OUTPUT);
  pinMode(PIN_BUZZ,  OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_Y, LOW);
  digitalWrite(PIN_BUZZ,  LOW);
  digitalWrite(PIN_RELAY, LOW);
  dht.begin();
  help();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if      (c == 'r') flip(PIN_LED_R, lvlR,     "LED merah");
    else if (c == 'y') flip(PIN_LED_Y, lvlY,     "LED kuning");
    else if (c == 'b') flip(PIN_BUZZ,  lvlBuzz,  "Buzzer");
    else if (c == 'k') flip(PIN_RELAY, lvlRelay, "Relay");
    else if (c == 'h') help();
  }
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    Serial.printf("Suhu=%.1f C  Lembap=%.0f %%  LDR=%d  PIR=%d  Tombol=%s\n",
                  t, h, analogRead(PIN_LDR), digitalRead(PIN_PIR),
                  digitalRead(PIN_BTN) == LOW ? "DITEKAN" : "lepas");
  }
}
