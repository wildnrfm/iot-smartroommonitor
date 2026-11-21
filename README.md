# Smart Room Monitor Firmware

Firmware pemantauan dan otomasi ruangan berbasis ESP32. Sistem membaca suhu dan kelembapan, intensitas cahaya, serta gerakan; mengendalikan kipas, buzzer, dan indikator LED; serta mengirim data ke Blynk IoT.

## Fitur

- Pemantauan suhu dan kelembapan menggunakan DHT22.
- Pemantauan cahaya menggunakan keluaran analog modul LDR.
- Deteksi gerakan menggunakan PIR dan indikator lampu yang bertahan 10 detik setelah gerakan terakhir.
- Kontrol kipas otomatis berdasarkan suhu atau manual melalui Blynk.
- Alarm suhu dengan histeresis 1 C, buzzer, tombol mute, dan kontrol mute dari Blynk.
- Koneksi Blynk menggunakan TLS.
- Tetap menjalankan fungsi lokal saat Wi-Fi atau Blynk tidak tersedia.

## Perangkat keras

Firmware ini ditujukan untuk ESP32 klasik yang kompatibel dengan `esp32doit-devkit-v1`.

### Daftar komponen

- 1 buah ESP32 DevKit
- 1 buah sensor DHT22
- 1 buah modul LDR/photoresistor
- 1 buah sensor PIR
- 1 buah modul relay
- 1 buah kipas DC 5 V
- 1 buah buzzer aktif
- 1 buah push button
- 1 buah LED merah
- 1 buah LED kuning
- 2 buah resistor seri untuk LED
- 1 buah PCB double layer untuk perakitan rangkaian

Daftar di atas mencakup komponen utama. Periksa label modul relay dan tipe buzzer sebelum perakitan karena modul generik dapat memiliki polaritas atau tegangan koil yang berbeda.

| Komponen | Sambungan ESP32 | Catatan |
| --- | --- | --- |
| Tombol mute | GPIO33 dan GND | Menggunakan `INPUT_PULLUP`; tombol aktif saat ditekan. |
| Buzzer aktif | GPIO25 dan GND | Konfigurasi awal menganggap buzzer aktif saat GPIO HIGH. Jika bebannya melebihi kemampuan GPIO, gunakan driver transistor. |
| DHT22 | DATA ke GPIO4; VCC ke 3V3; GND ke GND | Pastikan sinyal DATA tidak melebihi 3,3 V. Sensor tanpa modul mungkin memerlukan resistor pull-up. |
| Modul LDR | AO ke GPIO34; VCC ke 3V3; GND ke GND | GPIO34 hanya menerima masukan hingga 3,3 V. |
| PIR | OUT ke GPIO32; daya sesuai spesifikasi modul | Pastikan level keluaran OUT tidak melebihi 3,3 V. |
| LED merah | GPIO26 melalui resistor seri; katoda ke GND | Gunakan resistor pembatas arus. |
| LED kuning | GPIO27 melalui resistor seri; katoda ke GND | Digunakan sebagai indikator lampu. |
| Modul relay | IN ke GPIO13; DC+ dan DC- sesuai rating modul | Polaritas aktif relay dapat berbeda antar modul. |

### Catu daya kipas dan relay

Sistem menggunakan kipas DC 5 V. Jangan mengandalkan pin VIN atau port USB ESP32 untuk menyuplai kipas tanpa memastikan kemampuan arusnya. Pilihan paling aman adalah catu daya 5 V terpisah yang mampu memasok arus kipas. Sambungkan positif catu daya kipas ke `COM`, terminal `NO` ke positif kipas, dan negatif kipas ke GND/negatif catu daya kipas.

Jika relay memakai koil 5 V, sumber relay harus 5 V dan GND relay harus terhubung dengan GND ESP32. Jika kipas dan relay menggunakan catu daya terpisah, sambungkan GND kedua sumber agar level sinyal kendali memiliki referensi yang sama. Uji relay tanpa kipas sebelum beban terhubung.

Jangan menyambungkan tegangan listrik rumah ke rangkaian ini. Untuk beban AC, gunakan relay dan instalasi yang dirancang untuk tegangan tersebut serta penanganan dari teknisi yang berkompeten.

## Persiapan

1. Pasang Visual Studio Code dan ekstensi PlatformIO IDE.
2. Buka folder proyek ini di Visual Studio Code.
3. Buat file konfigurasi lokal dengan menyalin contoh:

   ```powershell
   Copy-Item include\secrets.h.example include\secrets.h
   ```

4. Isi `include/secrets.h` dengan Template ID, Template Name, Auth Token, SSID, dan password Wi-Fi. Gunakan jaringan Wi-Fi 2,4 GHz.
5. Jangan commit atau membagikan `include/secrets.h`. File tersebut sudah dikecualikan melalui `.gitignore`.

## Build, upload, dan Serial Monitor

Pilih environment sesuai target:

- `esp32doit-devkit-v1`: firmware utama dengan Blynk.
- `bench`: program uji komponen melalui Serial Monitor, tanpa Wi-Fi dan Blynk.

Perintah PlatformIO:

```powershell
pio run -e esp32doit-devkit-v1
pio run -e esp32doit-devkit-v1 -t upload
pio device monitor -e esp32doit-devkit-v1
```

Serial Monitor menggunakan baud rate `115200`. Environment `bench` dapat dibuild dan di-upload dengan mengganti nama environment pada perintah:

```powershell
pio run -e bench
pio run -e bench -t upload
pio device monitor -e bench
```

Program `bench` menerima perintah `r`, `y`, `b`, dan `k` untuk membalik level pin LED merah, LED kuning, buzzer, dan relay. Uji polaritas buzzer dan relay sebelum menggunakan beban.

## Konfigurasi Blynk

Buat datastream virtual dengan tipe dan fungsi berikut:

| Pin | Tipe yang disarankan | Fungsi |
| --- | --- | --- |
| V0 | Double | Suhu dalam C |
| V1 | Double | Kelembapan dalam persen |
| V2 | Integer | Nilai analog LDR (0-4095) |
| V3 | Integer, 0-1 | Perintah atau status kipas |
| V4 | Integer, 0-1 | Mode kipas: 0 otomatis, 1 manual |
| V5 | Double | Batas suhu dalam C |
| V6 | String | Status ruangan |
| V7 | Integer, 0-1 | Status keluaran deteksi gerakan PIR |
| V8 | Integer, 0-1 | Mute alarm: 1 mute, 0 aktif |

Kontrol kipas:

- Saat V4 bernilai `0`, kipas dikendalikan otomatis berdasarkan suhu.
- Klik V3 akan mengaktifkan mode manual (V4 bernilai `1`) dan mengatur kipas sesuai nilai V3.
- Untuk kembali ke mode otomatis, ubah V4 ke `0`.
- Dalam mode otomatis, kipas menyala saat suhu melewati batas V5 dan mati setelah suhu turun setidaknya 1 C di bawah batas. Histeresis ini mencegah relay sering berpindah saat suhu dekat ambang.

Data V0, V1, dan V2 dikirim setiap 30 detik. V6, V7, V8, serta status V3 dalam mode otomatis dikirim saat berubah. Mode dan batas suhu disinkronkan dari Blynk saat perangkat terhubung. Serial Monitor menampilkan ringkasan JSON untuk membantu diagnosis.

## Perilaku sistem

- LED merah menyala saat suhu melewati batas alarm.
- Buzzer berbunyi 300 ms setiap 2 detik selama alarm aktif dan tidak di-mute.
- Tombol GPIO33 membisukan atau mengaktifkan kembali buzzer saat alarm berlangsung. Mute direset ketika suhu kembali normal.
- LED kuning menyala saat kondisi terdeteksi gelap dan PIR mendeteksi gerakan; LED bertahan hingga 10 detik setelah gerakan terakhir.
- Status ruangan pada V6 mengikuti prioritas: panas, lembap, gelap, lalu nyaman.
- Saat boot, kedua LED berkedip dua kali sebagai indikator awal.

## Kalibrasi dan pemecahan masalah

Konstanta perangkat keras berada di bagian pengaturan `src/main.cpp`:

- `RELAY_ACTIVE_LOW`: polaritas input relay; nilai saat ini disetel aktif-HIGH berdasarkan hasil uji perangkat.
- `BUZZER_ACTIVE_LOW`: polaritas buzzer; nilai saat ini disetel aktif-HIGH untuk buzzer dua-kaki GPIO25-GND.
- `LDR_DARK_IS_HIGH` dan `LDR_GELAP`: arah pembacaan dan ambang gelap; kalibrasikan berdasarkan nilai Serial Monitor.

Jika perangkat tidak berperilaku sesuai harapan:

1. Periksa Serial Monitor pada `115200` untuk status Wi-Fi, Blynk, dan pembacaan DHT22.
2. Pastikan SSID dan password benar serta jaringan mendukung 2,4 GHz.
3. Pastikan Template ID, Template Name, dan Auth Token sesuai dengan perangkat Blynk.
4. Periksa tipe dan rentang datastream, khususnya V3 dan V4 yang harus menerima nilai `0` atau `1`.
5. Jika relay bekerja terbalik, uji polaritasnya tanpa beban kipas lalu sesuaikan `RELAY_ACTIVE_LOW`.
6. Jika ESP32 restart saat kipas berpindah, hentikan pengujian dan periksa catu daya serta arus awal kipas.

Selalu matikan daya sebelum mengubah sambungan. Tegangan masukan GPIO ESP32 tidak tahan 5 V.
