<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows%20x86__64-0078D4?style=for-the-badge&logo=windows&logoColor=white"/>
  <img src="https://img.shields.io/badge/Driver-Windows%20KMDF%20Kernel-FF1744?style=for-the-badge&logo=windows11&logoColor=white"/>
  <img src="https://img.shields.io/badge/Compiler-MSVC%20%2F%20GCC-FF6B35?style=for-the-badge&logo=c&logoColor=white"/>
  <img src="https://img.shields.io/badge/SIMD-AVX2%20256bit-7C4DFF?style=for-the-badge&logo=intel&logoColor=white"/>
  <img src="https://img.shields.io/badge/License-MIT-00E676?style=for-the-badge"/>
</p>

<h1 align="center">⚡ Ceke's RAM Test v1.2</h1>

<p align="center">
  <strong>An extremely aggressive bare-metal RAM stress testing tool & KMDF Kernel Driver for Windows x86_64<br>
  Built for extreme overclockers and hardware diagnostics professionals</strong>
</p>

---

## Key Features in v1.2

- **Automated 1-Click Installer (`install.bat`)** — Installs the application to `C:\Program Files\Ceke's RAM Test\`, registers the Windows Kernel Driver service (`CekesRamDriver`) via `sc.exe`, and creates Desktop & Start Menu shortcuts.
- **KMDF Kernel Driver (`cekes_ram_drv.sys`)** — Includes a dedicated Windows Kernel-Mode driver to translate Virtual Addresses to 64-bit Physical Addresses (`MmGetPhysicalAddress`), lock physical MDL pages (`MmAllocatePagesForMdlEx`), and query CPU MSRs & SMBus TSOD RAM temperature sensors.
- **Automatic Fallback Mode** — If the kernel driver is not loaded (e.g. Windows 11 DSE / HVCI enforcement), the tool seamlessly falls back to 100% native Windows User-Space APIs (`VirtualAllocExNuma`, `VirtualLock`, `SetThreadGroupAffinity`).
- **10 Advanced AVX2 Stress Modules** — 10 specialized SIMD algorithms targeting TRR (Target Row Refresh) bypass, signal crosstalk, Vdroop power rail spikes, and DRAM charge decay.
- **Deep Fault Biopsy Subsystem v1.2** — Automatically locks faulty 16 MB chunks, captures 100% of bit-flips (*Stuck-at-0* and *Stuck-at-1* without zero filtering), 5s/10s/30s charge retention tests, and crosstalk analysis.
- **Dynamic PRNG Bank-Group Rowhammering** — Seeded per chunk and thread address for truly distributed non-contiguous row hammering.
- **Surgical Anti-Crash Logging** — Flushes error telemetry directly to disk using `_commit(_fileno)` and `FlushFileBuffers` before any potential BSOD or system freeze.

---

## 10 Stress Modules

| # | Module | Description & Physics Target |
|---|--------|------------------------------|
| 1 | **Walking 1s & 0s** | Data bus crosstalk & address bus line faults (AVX2 256-bit) |
| 2 | **Rowhammer (16 KB Stride)** | Non-temporal streaming store `_mm_stream_si64` + `_mm_clflush` bypass |
| 3 | **PRNG Xorshift32 AVX2** | IMC thermal stress, voltage droop (Vdroop) & signal noise |
| 4 | **Bit-Fade (Retention Test)** | Real DRAM charge retention — write, flush, thread pause, verify |
| 5 | **AVX2 Data Lanes Overload** | Power rail stress VDD/VDDQ with 256-bit inversion patterns |
| 6 | **Thermal Cycling** | Combined hot/cold coordinated stress across all threads |
| 7 | **Bit Flip Explorer** | High frequency `0x5555555555555555` vs `0xAAAAAAAAAAAAAAAA` switching |
| 8 | **Moving Inversions** | Marching pattern writes, bitwise inversions (`~pattern`) & address shifts |
| 9 | **Block Read/Write Massif** | Asynchronous non-temporal write streams followed by unthrottled reads |
| 10 | **Random Stride Bank-Group** | Asymmetric non-contiguous Bank-Group hammering to bypass DRAM TRR |

---

## Memory Allocation Strategy

```
Total Installed Physical RAM (ullTotalPhys)
│
├─► STRESS ZONE: Total RAM − 4 GB
│     ► Locked via KMDF Kernel Driver MDLs (or VirtualAllocExNuma / MEM_LARGE_PAGES)
│     ► Divided into 16 MB chunks, distributed across all CPU logical threads
│
└─► SYSTEM RESERVE: 4 GB (always preserved for Windows 11, DWM, drivers)
```

---

## Project Structure

```
ceke's_ram/
├── driver/
│   ├── shared_ioctl.h         # Shared IOCTL codes & buffer payloads
│   ├── cekes_ram_drv.h        # WDK & KMDF driver headers
│   ├── cekes_ram_drv.c        # KMDF Kernel Driver source code
│   └── cekes_ram_drv.inf      # Windows Driver INF setup specification
├── src/
│   └── ram_stress_diag.c      # Main diagnostic engine (10 AVX2 modules)
├── bin/
│   └── ram_stress_diag.exe    # Compiled 64-bit AVX2 release binary
├── build.bat                  # One-click MSVC compilation script
├── install.bat                # Automated Administrative Windows Installer
├── uninstall.bat              # Automated System Uninstaller
├── installer.iss              # Inno Setup compilation script
└── README.md                  # Comprehensive documentation
```

---

## Installation & Usage

### 🚀 Option 1: Automated 1-Click Installation (Recommended)
Right-click **`install.bat`** -> **Run as Administrator**.
- Installs to `C:\Program Files\Ceke's RAM Test\`
- Configures and starts `CekesRamDriver` Windows Kernel Service
- Creates shortcuts on Desktop and Start Menu

### 💻 Option 2: Standalone Direct Run
Run as Administrator in PowerShell / CMD:
```powershell
.\bin\ram_stress_diag.exe
```

### 🛠️ Option 3: Build from Source
```cmd
build.bat
```
Or manually using MSVC:
```cmd
cl.exe /O2 /Oi /Ot /arch:AVX2 /W4 /D_CRT_SECURE_NO_WARNINGS /I driver src\ram_stress_diag.c /Fe:bin\ram_stress_diag.exe /link advapi32.lib
```

### 🗑️ Uninstallation
Right-click **`uninstall.bat`** -> **Run as Administrator**.
- Stops and deletes `CekesRamDriver` kernel service
- Removes files from `Program Files` and cleans shortcuts

---

## License

MIT License — © Ceke / Ceketrum
