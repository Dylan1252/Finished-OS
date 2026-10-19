# Matriks Pengujian Sistem KronOS

Matriks ini memetakan perilaku wajib Bab 1 sampai Bab 4 ke pemeriksaan yang dapat dijalankan. Test EXT2 pada host sudah otomatis; pemeriksaan kernel, keyboard, shell, dan proses memerlukan QEMU dan dilakukan secara manual.

## Persiapan build baru

Jalankan dari direktori root repositori dengan toolchain WSL/Linux:

```bash
make clean
make test
make disk
make build
make insert-shell
make insert-test-fixture
make run
```

`make run` melakukan boot biasa. Gunakan `make debug` hanya saat ingin menyambungkan GDB; QEMU akan mulai dalam keadaan pause pada mode tersebut.

## Pemeriksaan otomatis di host

Jalankan `make test`. Harness EXT2 memeriksa:

- Inisialisasi filesystem baru dan metadata direktori root.
- Data yang dibaca sama byte demi byte dengan data yang ditulis, termasuk batas block dan byte ke-2047.
- Direct, single-indirect, double-indirect, dan alokasi yang melintasi block group.
- File kosong, nama duplikat, nama tidak valid, parent tidak valid, buffer terlalu kecil, dan buffer null.
- Perbedaan operasi baca file dan direktori, operasi direktori bersarang, serta penolakan penghapusan root dan direktori yang tidak kosong.
- Pemindahan dan rename file/folder, pembaruan entri `..`, penolakan overwrite dan siklus direktori.
- Persistensi setelah filesystem diinisialisasi ulang dan rollback alokasi setelah disk kehabisan ruang.

Harness host tidak membuktikan kernel berhasil boot atau syscall dan scheduler berfungsi; hal tersebut diperiksa di QEMU di bawah ini.

## Pemeriksaan integrasi di QEMU

Gunakan disk image baru agar setiap kasus tidak bergantung pada state dari run sebelumnya. Catat hasil dan keluaran yang terlihat untuk tiap kasus.

| ID | Area buku | Langkah | Hasil yang diharapkan |
|---|---|---|---|
| Q01 | Kernel, framebuffer, interrupt, keyboard | Boot dengan `make run`; ketik `help`, jalankan `clear`, gunakan Backspace, lalu ketik `echo KronOS` dan Enter. | GRUB memuat kernel, prompt dimulai dari `/`, keyboard bekerja, Backspace mengedit baris, `clear` membersihkan layar, dan output tampil di framebuffer. |
| Q02 | REPL shell, direktori aktif | Jalankan `cd /`, `mkdir lab`, `ls /`, `cd lab`, `echo inside`, `ls`, `cd ..`. | `lab/` terlihat di daftar root; prompt menunjukkan `/lab` saat berada di dalamnya dan kembali ke `/` setelah naik. |
| Q03 | `cat`, `cp`, `grep` dengan file | Jalankan `cat /grep-fixture.txt`, `grep needle /grep-fixture.txt`, `cp /grep-fixture.txt /lab/copy.txt`, lalu `cat /lab/copy.txt`. | `cat` menampilkan fixture; `grep` hanya menampilkan dua baris yang memuat `needle`; file hasil salin memiliki isi yang sama. |
| Q04 | `mv`, `rm`, `find` | Jalankan `mkdir /archive`, `mkdir /lab/subdir`, `mv /lab/subdir /archive/renamed-dir`, `cd /archive/renamed-dir`, `cd ..`, `mkdir /archive/renamed-dir/inner`, `mkdir /lab/other`, `mkdir /lab/other/inner`, `mv /lab/copy.txt /lab/renamed.txt`, `find inner`, `find renamed-dir`, `rm /lab/renamed.txt`, lalu `find renamed.txt`. | Folder pindah antar-parent, `..` kembali ke `/archive`, `find inner` melaporkan kedua path bersarang, dan file yang dihapus tidak ditemukan lagi. |
| Q05 | Pipeline dan buffer output | Jalankan `help | grep process | grep process`, lalu `echo needle | grep needle`. | Setiap tahap menerima output tahap sebelumnya; baris yang cocok tampil dan baris help lain tidak ikut tampil. |
| Q06 | EXT2 melalui syscall shell | Jalankan `mkdir lab` sekali lagi, `cat /missing`, `cd /missing`, lalu `ls /`. | Duplikasi dan operasi atas file/direktori yang hilang melaporkan error tanpa hang atau merusak prompt; root tetap dapat dibaca. |
| Q07 | User mode dan program eksternal | Jalankan `exec hello`, lalu masukkan `help`. | `hello` mencetak sapaan lalu keluar; shell tetap hidup dan menerima perintah berikutnya. |
| Q08 | Pembuatan proses, preemption PIT, clock | Jalankan `exec clock`, tunggu minimal 10 detik, jalankan `ps`, catat PID clock, jalankan `kill <PID clock>`, lalu `ps` lagi. | Clock diperbarui sementara shell tetap responsif; `ps` menampilkan keduanya sebelum kill dan tidak lagi menampilkan clock sesudahnya. |
| Q09 | Cleanup setelah program selesai | Jalankan `exec hello` beberapa kali dan periksa `ps` setelah tiap proses selesai. | Proses `hello` yang sudah selesai hilang dari process table dan tidak meninggalkan entry usang. |

## Fitur bonus

Buku menandai variabel lingkungan `PATH` dan shell command terpisah sebagai bonus. Buku juga menandai `cp`/`rm` rekursif dan regex pada `grep` sebagai bonus. Uji serta laporkan fitur-fitur itu terpisah; fitur bonus tidak menggantikan kelulusan Q01–Q09.

## Catatan hasil

Salin tabel ini ke laporan pengumpulan dan isi berdasarkan pengujian yang benar-benar dilakukan. Jangan menandai kasus lulus hanya dari inspeksi kode.

| ID | Hasil | Catatan / bukti |
|---|---|---|
| Q01–Q09 | Menunggu uji QEMU | Catat versi toolchain/QEMU dan sertakan screenshot atau log. |
