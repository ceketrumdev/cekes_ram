<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows%20x86__64-0078D4?style=for-the-badge&logo=windows&logoColor=white"/>
  <img src="https://img.shields.io/badge/Compiler-MSVC%20%2F%20GCC-FF6B35?style=for-the-badge&logo=c&logoColor=white"/>
  <img src="https://img.shields.io/badge/SIMD-AVX2-7C4DFF?style=for-the-badge&logo=intel&logoColor=white"/>
  <img src="https://img.shields.io/badge/License-MIT-00E676?style=for-the-badge"/>
</p>

<h1 align="center">⚡ Ceke's RAM Test</h1>

<p align="center">
  <strong>An extremely aggressive, bare-metal RAM stress testing tool for Windows x86_64<br>
  Built for overclockers and hardware diagnostics enthusiasts</strong>
</p>

---

## Overview

**Ceke's RAM Test** is a native C command-line tool targeting Windows x86_64, inspired by MemTest86 and TestMem5 but designed to run within Windows itself. It pushes your RAM subsystem to its absolute limits using six specialized stress modules, all vectorized with AVX2 SIMD intrinsics and parallelized across all CPU logical cores (including P-Cores and E-Cores on Intel Alder Lake / Raptor Lake).

> **Tested on**: Intel Core i9-14900K (8P + 16E cores) — DDR5 6000 MT/s XMP

---

## Features

- **Allocates Total RAM − 4 GB** — Uses `GlobalMemoryStatusEx.ullTotalPhys` to grab every byte possible (Total installed RAM − 4 GB system reserve), not just currently free RAM. Windows 11 handles the rest gracefully.
- **AVX2 256-bit SIMD** — All write/read/verify loops use 256-bit `__m256i` vector registers for maximum throughput
- **Work Stealing Thread Pool** — Atomic chunk indexing via `InterlockedIncrement64` across 16 MB blocks; P-Cores naturally pull more work than E-Cores
- **Hard CPU Affinity** — `SetThreadAffinityMask` pins each thread to a specific logical core; detected via `GetLogicalProcessorInformationEx` (EfficiencyClass P-Core vs E-Core)
- **Large Pages (2 MB)** — Attempts `MEM_LARGE_PAGES | MEM_COMMIT | MEM_RESERVE` via `SeLockMemoryPrivilege` for physically contiguous allocation
- **Real-time TUI Dashboard** — ANSI terminal dashboard with live bandwidth (GB/s), progress per module, 50-cell DRAM heatmap, and per-thread status
- **WHEA / Silent ECC Telemetry** — Reads Windows Event Log for WHEA-Logger events (IDs 1, 19, 26, 47) to detect hardware-masked memory corrections
- **SEH Protection** — `__try / __except` blocks catch access violations without crashing, logging the offending chunk
- **Auto-tuning** — Rowhammer iteration count auto-adjusts based on measured bandwidth (DDR4 vs DDR5 detection)
- **Full Log Output** — Every diagnostic run writes a timestamped `ram_stress_diag.log` file

---

## Stress Modules

| # | Module | What it Tests |
|---|--------|---------------|
| 1 | **Walking 1s & 0s** | Data bus crosstalk, address bus faults |
| 2 | **Rowhammer (16 KB Stride)** | DRAM row hammering with `_mm_stream_si64` + `_mm_clflush` bypass |
| 3 | **PRNG Xorshift32 AVX2** | IMC thermal stress, voltage droop (Vdroop), signal integrity |
| 4 | **Bit-Fade (Retention Test)** | Real DRAM charge retention — write, flush, wait, verify |
| 5 | **AVX2 Data Lanes Overload** | Power rail stress VDD/VDDQ with 256-bit inversion patterns |
| 6 | **Thermal Cycling** | Combined hot/cold coordinated stress across all threads |

---

## Memory Allocation Strategy

```
Total Physical RAM (ullTotalPhys)
│
├─► STRESS ZONE: Total RAM − 4 GB
│     ► Allocated via VirtualAlloc (MEM_COMMIT | MEM_RESERVE)
│     ► Locked in physical RAM (VirtualLock / MEM_LARGE_PAGES)
│     ► Divided into 16 MB chunks, distributed across all CPU threads
│
└─► SYSTEM RESERVE: 4 GB (always free for Windows 11, DWM, drivers)
```

> On a 48 GB system, the tool allocates **44 GB** and leaves **4 GB** for the OS.

---

## Requirements

- **OS**: Windows 10 / 11 x64
- **CPU**: Any x86_64 with AVX2 support (Intel Haswell+ / AMD Ryzen 1000+)
- **RAM**: At least 8 GB total installed
- **Compiler**:
  - MSVC: Visual Studio 2019+ (`cl.exe`)
  - GCC: MinGW-w64 with AVX2 support

---

## Build

### MSVC (Visual Studio)

```cmd
cl.exe /O2 /Oi /Ot /arch:AVX2 /W4 src\ram_stress_diag.c /Fe:ram_stress_diag.exe
```

Or use the provided build script:

```cmd
build.bat
```

### GCC (MinGW-w64)

```bash
gcc -O3 -march=native -mavx2 src/ram_stress_diag.c -o ram_stress_diag.exe -ladvapi32
```

---

## Usage

Open **PowerShell** or **CMD** as Administrator (required for large pages & WHEA log access):

```powershell
.\ram_stress_diag.exe
```

The tool will:
1. Detect your CPU topology (P-Cores / E-Cores)
2. Measure total RAM and allocate (Total − 4 GB)
3. Attempt to lock memory as Large Pages (2 MB) for best physical contiguity
4. Run all 6 stress modules sequentially across all threads
5. Read the Windows Event Log for silent hardware ECC corrections (WHEA)
6. Print a final PASS / FAIL summary and write `ram_stress_diag.log`

> **Tip**: Run as Administrator to enable `SeLockMemoryPrivilege` for Large Pages. Without it, the tool still works but may use standard 4 KB pages.

---

## Output Example

```
=====================================================================
 CEKE'S RAM TEST (AVX2 / Work Stealing Pool)
=====================================================================

[+] Timestamp       : 2025-07-17 21:00:00
[+] CPU Threads     : 32 (8 P-Cores HT | 16 E-Cores)
[+] Total RAM       : 47.85 GB
[+] System Reserve  : 4.00 GB (left free for Windows)
[+] Stress Zone     : 43.85 GB (2802 x 16 MB chunks)
[OK] MEM_LARGE_PAGES 2 MB — Physically contiguous allocation

 MODULE 2 : Rowhammer Real Stride 16 KB
 Progress : [██████████████████░░░░░░░░] 72.3% (2026 / 2802 chunks)
 Bandwidth: 58.21 GB/s  |  Elapsed: 124.5 sec

[OK] Module 2 — PASS (0 errors)
...
=====================================================================
 FINAL RESULT: PASS (100% STABLE)  |  WHEA/ECC events: 0
=====================================================================
```

---

## Project Structure

```
ceke's_ram/
├── src/
│   └── ram_stress_diag.c       # Full diagnostic engine (C99, AVX2, Win32)
├── bin/
│   └── ram_stress_diag.exe     # Pre-compiled release binary (x86_64 AVX2)
├── build.bat                   # One-click MSVC build script
├── ram_stress_diag.log         # Output log (generated at runtime)
└── README.md
```

---

## Disclaimer

> **Use at your own risk.**
> This tool deliberately stresses your memory subsystem at maximum intensity.
> It is not a replacement for dedicated hardware-level testers (MemTest86, TestMem5) which operate outside the OS.
> Always ensure adequate cooling before running extended stress sessions.

---

## License

MIT License — © Ceke / Ceketrum
