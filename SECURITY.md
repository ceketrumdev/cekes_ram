# Security Policy & Low-Level Operations Notice

## ⚠️ Important Hardware & Privilege Notice

`cekes_ram` is a raw C and x86_64 assembly memory diagnostic tool designed specifically for extreme overclocking stability testing and hardware failure detection. 

Because the utility bypasses standard OS abstractions, disables processor caches, and maps threads directly to physical CPU cores (P-Cores / E-Cores), **it requires elevated privileges (Administrator/Root)** to run.

---

## 🔒 Security & Safety Guarantees

* **Zero Network Activity:** `cekes_ram` does not connect to the internet, gather telemetry, or transmit any data outside the local system.
* **Non-Persistent Execution:** All operations occur purely in volatile memory (DRAM). No data is written to disk except for user-requested diagnostic log files.
* **Direct Hardware Manipulation:** The tool writes specific memory patterns (e.g., vectorized AVX2 stress sequences, row-hammer style access patterns) directly to DRAM cells to expose IMC and signal integrity instabilities.

---

## 🛡️ Responsible Disclosure Policy

If you discover a security vulnerability or a critical bug that could cause unhandled system instability outside the expected stress testing bounds:

1. **Do NOT open a public issue.**
2. Contact the maintainer directly via GitHub or email at: **[TON_EMAIL_OU_CONTACT]**
3. Include:
   - System specifications (CPU architecture, RAM configuration, OS version).
   - Steps or commands used when the issue occurred.
   - Any crash logs or WHEA/ECC telemetry captured.

I will acknowledge receipt of your report within 48 hours and work on a fix or clarification.

---

## 🛑 Disclaimer

This tool is designed to push memory subsystems to their absolute electrical and thermal limits. 
The author is not responsible for hardware degradation, data loss on unstable systems, or system crashes (BSODs) occurring during tests on improperly cooled or over-volted hardware. **Use at your own risk.**
