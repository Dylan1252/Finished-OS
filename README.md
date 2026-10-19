![Maskot Kelompok](mascot.jpg)

# KronOS — Tugas Besar IF2130 Sistem Operasi 2026/2027

Implementasi sistem operasi 32-bit dari nol untuk arsitektur x86, mengikuti buku
panduan praktikum IF2130 Sistem Operasi. Sistem berjalan di QEMU, di-*boot* lewat GRUB
(Multiboot), dan menyediakan *multitasking* preemptive dengan *scheduler* round-robin.

## Kontributor

> Nama anggota belum tersedia pada berkas proyek yang saya terima. Ganti catatan ini dengan nama seluruh anggota kelompok sebelum pengumpulan.

## Daftar Isi

- [Cara Run](#cara-run)
- [Pengujian](#pengujian)
- [Fitur yang Dibuat](#fitur-yang-dibuat)
  - [Chapter 0 & 1 — Kernel Dasar](#chapter-0--1--kernel-dasar)
  - [Chapter 2 — Filesystem EXT2](#chapter-2--filesystem-ext2)
  - [Chapter 3 — User Mode & Shell](#chapter-3--user-mode--shell)
  - [Chapter 4 — Process, Scheduler & Multitasking](#chapter-4--process-scheduler--multitasking)
- [Perintah Shell](#perintah-shell)
- [Struktur Direktori](#struktur-direktori)
- [Maskot Kelompok](#maskot-kelompok)

## Cara Run

Prasyarat: WSL2 (Ubuntu 20.04/22.04) dengan `gcc`, `nasm`, `ld`, `make`,
`qemu-system-i386`, `genisoimage`, dan `qemu-img`.

```bash
# 1. Buat disk image kosong 4 MiB
make clean
make disk

# 2. Build kernel + ISO
make build

# 3. Build user program (shell, hello, clock) lalu masukkan ke disk image
make insert-shell

# 4. Tambahkan fixture teks untuk uji grep/cp/mv (opsional untuk demo)
make insert-test-fixture

# 5. Jalankan di QEMU
make run
```

Untuk memulai QEMU dalam keadaan pause dan menunggu debugger GDB, gunakan `make debug`.

## Pengujian

Jalankan test EXT2 host tanpa QEMU:

```bash
make test
```

Pada Windows tanpa WSL, jalankan test host yang sama melalui PowerShell:

```powershell
.\tests\run_ext2_host_tests.ps1
```

Test menjalankan 72 assertion dengan disk image in-memory. Cakupannya termasuk round-trip byte demi byte,
akses sekitar batas block dan byte 2047, direct/single/double indirect, alokasi yang menyeberangi block group,
file kosong, validasi buffer dan parent, operasi direktori, move/rename file dan folder termasuk pembaruan `..`,
penolakan overwrite dan siklus direktori, persistensi, penghapusan, serta rollback saat ruang habis.

Uji shell dan integrasi kernel di QEMU dijabarkan pada [matriks pengujian](tests/SYSTEM_TESTS.md). Catat hasil aktual dan konfigurasi QEMU pada laporan pengumpulan; daftar langkah belum menjadi bukti pengujian sampai benar-benar dijalankan.

Gunakan **Shift** untuk mengetik karakter simbol (mis. `|` untuk pipeline).

## Fitur yang Dibuat

### Chapter 0 & 1 — Kernel Dasar

- **GDT** (kernel & user segment) + **TSS** untuk perpindahan ring 0 ↔ ring 3
- **IDT** & *interrupt handler* generik (`intsetup.s`)
- **PIC** remap dan driver **keyboard** (termasuk dukungan **Shift** — fitur bonus)
- Driver **framebuffer** teks 80×25 (newline, backspace, scroll)

### Chapter 2 — Filesystem EXT2

- Format **EXT2 - IF2130 Edition**: superblock, block group descriptor, block &
  inode bitmap, inode table
- Operasi: `read`, `read_directory`, `write`, `delete`, `move_entry`, `get_file_size`, `get_directory_size`
- Mendukung **direct + single indirect + double indirect** block (file besar)
- Pembuatan filesystem otomatis saat disk masih kosong

### Chapter 3 — User Mode & Shell

- Masuk **user mode (ring 3)** via `iret`, *syscall* lewat `int 0x30`
- **Paging** 4 KiB, higher-half kernel, *recursive page table*
- Program shell user-space dengan perintah internal (lihat tabel di bawah)

### Chapter 4 — Process, Scheduler & Multitasking

- **Process Control Block (PCB)** & `_process_list`
- **Page directory terpisah per proses** (isolasi alamat virtual)
- **Context switch** (`context-switch.s`) dan **scheduler round-robin**
- **Preemptive multitasking** lewat interrupt **timer PIT (IRQ0)**
- Syscall proses: `exit`, `exec` (create process), `kill`, info proses (`ps`)
- Driver **CMOS/RTC** dan program **clock** yang berjalan konkuren dengan shell
- Program eksternal: `hello` dan `clock`

## Perintah Shell

| Perintah | Deskripsi |
|---|---|
| `help` | menampilkan daftar perintah |
| `clear` | membersihkan layar |
| `echo <teks>` | mencetak teks |
| `cd <dir>` | pindah direktori (`..` untuk naik) |
| `ls [dir]` | daftar isi direktori |
| `mkdir <dir>` | membuat direktori |
| `cat <file>` | menampilkan isi file |
| `cp <src> <dst>` | menyalin file |
| `mv <src> <dst>` | memindah / mengganti nama file atau folder |
| `rm <path>` | menghapus file / direktori kosong |
| `find <nama>` | mencari file di seluruh filesystem |
| `grep <pola> [file]` | mencari pola di file atau hasil pipeline |
| `\|` | pipeline antar dua perintah |
| `exec <file>` | menjalankan program eksternal (`hello`, `clock`) |
| `ps` | menampilkan daftar proses |
| `kill <pid>` | menghentikan proses berdasarkan PID |

Contoh:

```text
exec clock          # menampilkan jam RTC di pojok kanan atas (proses latar)
ps                  # PID 1 shell RUNNING, PID 2 clock READY
kill 2              # hentikan proses clock
exec hello          # cetak sapaan lalu proses selesai (exit)
ls | grep txt       # contoh pipeline
```

## Struktur Direktori

```text
src/
├─ header/
│  ├─ cpu/          gdt.h, idt.h, interrupt.h, portio.h
│  ├─ driver/       disk.h, keyboard.h, cmos.h
│  ├─ filesystem/   ext2.h
│  ├─ memory/       paging.h
│  ├─ process/      process.h, scheduler.h
│  ├─ stdlib/       string.h
│  └─ text/         framebuffer.h
├─ cpu/             gdt.c, idt.c, interrupt.c, intsetup.s
├─ driver/          disk.c, keyboard.c, cmos.c
├─ filesystem/      ext2.c
├─ memory/          paging.c
├─ process/         process.c, scheduler.c, context-switch.s
├─ stdlib/          string.c
├─ text/            framebuffer.c
├─ external/        hello.c, clock.c, external-inserter.c
├─ user-shell.c
├─ crt0.s, user-linker.ld
├─ kernel.c, kernel-entrypoint.s, linker.ld
└─ menu.lst
```

## Maskot Kelompok

![Maskot Kelompok](mascot.jpg)
