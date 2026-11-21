# Smart Room Monitor - kerangka program (Kelompok 5)

1. Salin `include/secrets.h.example` menjadi `include/secrets.h`, lalu isi Template ID, Template Name, Auth Token (device "ESP32 FISIK") dan WiFi (hotspot 2,4 GHz). Jangan di-commit.
2. Buka folder ini di VS Code dengan ekstensi PlatformIO.
3. Environment `bench` (`src/bench.cpp`): penguji komponen untuk Tahap 4, tanpa WiFi dan Blynk.
4. Environment `esp32doit-devkit-v1` (`src/main.cpp`): program utama dengan Blynk, diturunkan dari kode mini project Smart Room Monitor v2. Daftar perubahannya ada di komentar bagian atas file.
5. Setelah uji komponen, sesuaikan konstanta bertanda "SESUAIKAN" di `src/main.cpp`.

Di Blynk, V3 mengendalikan kipas manual dan klik V3 otomatis mengaktifkan mode manual (V4). Matikan V4 untuk kembali ke kontrol kipas otomatis berdasarkan suhu. Pastikan datastream V3 dan V4 memakai nilai 0/1.

Panduan lengkap per tahap ada di dokumen "Panduan Pengerjaan Smart Room Monitor".

Catatan: kode ini belum dijalankan pada perangkat. Logika kontrolnya sudah diuji di komputer dengan simulasi, tetapi build pertama di PlatformIO mungkin perlu penyesuaian kecil.
