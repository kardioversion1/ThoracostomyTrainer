# ThoracostomyTrainer

# Needle and Finger Thoracostomy Trainer
### A 3D-Printable Active Pneumothorax Simulation Trainer for EMS Education

**Built by Evan Kuhl, Co-Founder — Kentuckiana Prehospital Education Consortium (KPEC)**
**www.myKPEC.com**

---

## Overview

This project is a functional, active pneumothorax simulator designed to install inside a standard 
CPR mannequin (Prestan half-torso or equivalent). It provides realistic haptic and auditory feedback 
for two critical EMS procedures:

- **Needle thoracostomy** — 2nd intercostal space, lateral approach (AAL)
- **Finger thoracostomy** — 4th–5th intercostal space, lateral approach (AAL)

When a learner inserts at the correct anatomical location with sufficient force, the system releases 
a puff of pressurized air simulating decompression, then automatically recharges for the next learner.

Inspired by the work of **Ilana Roberts Krumm, MD** and the UCSF Explor-A-Thora project.  
Built to provide cost-efficient simulation through KPEC, a regional nonprofit organization.

> This project and all KPEC materials are **free for use and not for commercial use** without a 
> written agreement.

> Claude AI was used to design wiring layouts, code, and build instructions. All information was 
> reviewed and modified by Evan Kuhl.

---

## Repository Contents

| File | Description |
|---|---|
| `thoracostomy_trainer.ino` | ESP32 firmware — v5.6 |
| `Thoracostomy_Trainer_Build_Guide.pdf` | Full build guide and facilitator manual |

3D-printable files (.3MF) are available on **MakerWorld** — link below.

---

## Quick Specs

| Parameter | Value |
|---|---|
| Microcontroller | ESP32 WROOM-32 |
| Display | SH1106 128×64 OLED |
| Power | 2× 18650 Li-ion, USB-C charging |
| Operating pressure | 3–5 PSI (IV bag bladder) |
| Insertion zones | 2 independent (needle + finger) |
| Reset time between learners | ~15–30 seconds |
| Estimated build cost | $80–130 USD |

---

## Firmware — v5.6

**Requirements:**
- Arduino IDE 2.x (or PlatformIO)
- ESP32 board package: `espressif/arduino-esp32` v2.x
- Library: `U8g2` by olikraus (Library Manager)
- Library: `ESP32 Preferences` (included in board package)

**Flash procedure:**
1. Open `thoracostomy_trainer.ino` in Arduino IDE
2. Select board: `ESP32 Dev Module`
3. Set Upload Speed: `115200`
4. Connect ESP32 via USB-C
5. Hold BOOT button, click Upload, release BOOT when upload begins
6. Open Serial Monitor at 115200 baud to confirm boot

**Key firmware features:**
- Dual independent FSR zones with per-zone sensitivity (XLOW / LOW / MED / HIGH / STIFF)
- Dominant sensor logic — prevents cross-zone false triggers
- Dual lockout — both sensors must return to idle before re-trigger
- Full state machine: READY → FIRING → PAUSING → PUMPING → COOLDOWN → READY
- All settings saved to NVS flash (survive power cycle)
- OLED UI with live pressure bars, countdown timers, procedure counters

**GPIO assignments:**

| GPIO | Function |
|---|---|
| 18 | Air pump |
| 19 | Solenoid 1 (needle zone) |
| 21 | Solenoid 2 (finger zone) |
| 22 | OLED SCL (I2C) |
| 23 | OLED SDA (I2C) |
| 25 | Encoder SW (button) |
| 32 | Encoder CLK |
| 33 | Encoder DT |
| 34 | FSR Zone 1 — needle (ADC) |
| 35 | FSR Zone 2 — finger (ADC) |

> ⚠️ `Wire.begin(23, 22)` must be called before `u8g2.begin()` in setup(). Missing this causes 
> silent cold-boot display failure.

---

## 3D Print Files

Available on MakerWorld: **[link coming soon]**

| Part | Material | Walls | Infill |
|---|---|---|---|
| TPU Lung | Bambu TPU 90A | 1 | 3% Gyroid |
| Electronics enclosure | Bambu PETG HF | 2 | 15% |
| Rib sections | Bambu PETG HF | 4 | 15%+ |
| FSR Covers ×2 | TPU | 3 | 20% |
| Pump vibration reducers | TPU | 2+ | 15%+ |

---

## Full Build Guide

See `Thoracostomy_Trainer_Build_Guide.pdf` in this repository for:
- Complete assembly instructions with photos
- Full bill of materials
- Electronics wiring guide
- Kentucky EMS protocol reference (2023)
- Training scenarios and facilitator guide
- Troubleshooting

---

## Acknowledgments

- **Ilana Roberts Krumm, MD** — UCSF Department of Medicine
  - [Explor-A-Thora: A Novel Three-Dimensionally Printed Pleural Simulator](https://pubmed.ncbi.nlm.nih.gov/39371230/)
  - [Meet the Maker – Ilana Krumm, MD](https://www.library.ucsf.edu/news/meet-the-maker-ilana-krumm-md/)

---

## License

Free for educational and non-commercial use.  
Commercial use requires written agreement with KPEC.  
Contact: www.myKPEC.com
