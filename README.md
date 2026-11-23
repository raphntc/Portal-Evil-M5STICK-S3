<div align="center">

<img src="assets/m5sticks3.svg" alt="M5StickS3 running Portal Evil HUEBR" width="420">

# ⚡ Portal Evil HUEBR — M5StickS3 Edition

### Educational captive portal (“Evil Portal”) for the **M5StickS3** (ESP32-S3)

_Built for Wi-Fi security awareness, live demos, and research._

<p>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-1877f2?style=for-the-badge" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/Platform-ESP32--S3-informational?style=for-the-badge&logo=espressif&logoColor=white" alt="Platform: ESP32-S3">
  <img src="https://img.shields.io/badge/Board-M5StickS3-E60012?style=for-the-badge" alt="Board: M5StickS3">
  <img src="https://img.shields.io/badge/Built%20with-Arduino-00979D?style=for-the-badge&logo=arduino&logoColor=white" alt="Built with Arduino">
  <img src="https://img.shields.io/badge/Purpose-Educational-2ea44f?style=for-the-badge" alt="Purpose: Educational">
</p>

**Author:** [@raphntc](https://github.com/raphntc/) &nbsp;·&nbsp; **License:** [MIT](LICENSE)

</div>

---

> [!CAUTION]
> **For educational and authorized security-testing use only.** Do not use it against networks, devices, or people without **explicit permission**. Capturing credentials from users who have not consented is illegal in most jurisdictions. You are solely responsible for how you use this software and assume all associated risks.

> [!NOTE]
> This project is **not affiliated with, endorsed by, or supported by Facebook/Meta** or any other brand referenced in the demo page. Those names and layouts are used only to illustrate how phishing portals impersonate well-known services.

<details>
<summary>🇧🇷 <b>Aviso em Português</b></summary>

<br>

Este código é fornecido **apenas para fins educacionais e testes de segurança autorizados**. Não o utilize contra redes, dispositivos ou pessoas sem **permissão explícita**. Capturar credenciais de usuários que não consentiram é ilegal na maioria dos países. Você é o único responsável pelo uso deste software e assume todos os riscos envolvidos.

Este projeto **não é afiliado, endossado ou suportado pelo Facebook/Meta** nem por qualquer outra marca citada na página de demonstração.

</details>

---

## 📖 About

This is the **M5StickS3 port** of [Portal Evil HUEBR](https://github.com/raphntc/Portal-Evil-M5STICK-C-PLUS), originally written for the M5StickC Plus. Wi-Fi honeypots are deceptive wireless networks that masquerade as legitimate hotspots: they lure users with free internet, then present a fake “sign-in” page to harvest data. This project recreates that scenario on cheap, pocket-sized hardware so students, educators, and security professionals can **see the technique in action** and learn to recognize and defend against it.

The user experience is identical to the C Plus build — the same laughing-skull boot animation, on-device menu, live LCD dashboard, login-protected web dashboard, CSV/JSON export, and persistent settings — but the firmware is rebuilt on the **M5Unified** driver stack for the ESP32-S3.

---

## 🔁 What changed vs. the M5StickC Plus

The display is the same 135×240 panel, so all of the on-screen UI (skull, menus, battery gauge, dashboard layout) is reused unchanged. Only the hardware-abstraction layer was rewritten:

| Area | M5StickC Plus | M5StickS3 (this repo) |
| --- | --- | --- |
| SoC | ESP32-PICO | **ESP32-S3-PICO-1** (8 MB flash, 8 MB PSRAM) |
| Arduino library | `M5StickCPlus` | **`M5Unified`** (+ `M5GFX`) |
| Power / battery | AXP192 (`M5.Axp`) | **M5PM1 PMU** → `M5.Power` |
| Battery % | voltage → estimate | `M5.Power.getBatteryLevel()` |
| Sound | passive buzzer (`M5.Beep`) | **ES8311 speaker** → `M5.Speaker.tone()` |
| Brightness | `M5.Axp.ScreenBreath()` (0–15) | `M5.Display.setBrightness()` (0–255) |
| Power off | `M5.Axp.PowerOff()` | `M5.Power.powerOff()` |
| Capture LED | GPIO 10 (red) | **none** — the StickS3 has no user LED (see below) |
| Buttons | BtnA / BtnB | BtnA / BtnB (KEY1 = G11, KEY2 = G12) |
| Capture storage | RAM only (lost on reboot) | **RAM + flash (LittleFS)** — survives power loss |

> [!IMPORTANT]
> The M5StickS3 has **no user-addressable LED** (only a hardware power/charge indicator). The *“LED blink on capture”* option is still present in the menus, but it is a safe **no-op** unless you wire an external LED and set `LED_PIN` at the top of the sketch. Audible/visual feedback on capture still works via the speaker beep and the on-screen counter.

---

## ✨ Features

- 📶 Open Wi-Fi access point with a configurable SSID (default `Wi-Fi Gratis`).
- 🌐 Captive portal via DNS spoofing — any requested domain is redirected to the portal page.
- 🔐 Customizable fake sign-in page; submissions HTML-escaped on capture.
- 💾 **Captures persisted to flash (LittleFS)** — every submission is appended to a history file on the 8 MB flash and reloaded on boot, so nothing is lost if the battery dies or the device reboots. A 50-entry RAM ring feeds the live LCD/dashboard; the CSV/JSON exports stream the **full** history from flash.
- ⏳ Animated “validating access” page after submission (pure CSS/JS, works offline).
- 💀 Laughing-skull boot animation (~5 s), vector-drawn, now with a **real synthesized evil cackle** played through the speaker (falls back to beeps if RAM is tight).
- 🎛️ **Animated on-device menu** (Tools / Configuration / Exit): gradient header + background, raised cards with drop shadows and bevels (depth), colored icons, a glowing selected row, rows that **slide in** when you open a menu and a **pop** when the highlight moves; plus a live status panel. The portal does **not** auto-start.
- 🖥️ Live LCD: SSID, admin URL (`http://172.0.0.1/admin`), battery gauge, and victim count.
- 🔊 Beep feedback on capture/boot via the speaker (toggleable).
- 📋 Operator dashboard at `/admin` — six stat tiles, AP info bar, search filter, configurable auto-refresh.
- 🔑 Login-protected admin (HTTP Basic Auth; user/password configurable, can be turned off).
- ⬇️ Export captures as **CSV** (`/export.csv`) or **JSON** (`/export.json`).
- 🧰 Web settings page (`/settings`) — organized into sections (Portal / Display / Audio / Security) with SSID, auto-refresh, brightness, **screen flip 180°**, beep, **speaker volume slider**, LED, and login; a **show-password** toggle; and a read-only **Device status** panel (battery, uptime, free heap, flash used, lifetime captures). All live, no reflashing.
- 💾 Persistent settings saved to flash (NVS) and restored on boot.

---

## 🔐 Admin Access — default login

> [!IMPORTANT]
> The operator dashboard (`/admin`) and all of its actions are protected by a login. **Default credentials:**
>
> | | |
> |:--|:--|
> | **User** | `admin` |
> | **Password** | `huebr` |
>
> **Change them before any real use** — on the device-served `/settings` page, or by editing the `ADMIN_USER_DEF` / `ADMIN_PASS_DEF` defines at the top of the sketch. The login can also be disabled from `/settings`. Victim-facing routes (`/`, `/post`) are never gated.

Open the dashboard while connected to the AP: `http://172.0.0.1/admin`

---

## 🛠️ Hardware

- **M5StickS3** — ESP32-S3-PICO-1-N8R8, 1.14″ 135×240 LCD (ST7789P3), ES8311 speaker, BMI270 IMU, M5PM1 PMU, 250 mAh battery.
- USB-C cable for flashing and power.

## 📦 Requirements

- [Arduino IDE](https://www.arduino.cc/en/software) (or Arduino CLI / PlatformIO).
- **ESP32 board support** (arduino-esp32 core) installed via Boards Manager.
- Libraries: **`M5Unified`** and **`M5GFX`** (install via Library Manager — `M5GFX` is pulled in as a dependency of `M5Unified`). `WiFi`, `DNSServer`, `WebServer`, `Preferences` ship with the ESP32 core.

---

## 🚀 Installation / Flashing

### 🧰 Option A — Arduino IDE (build from source)

1. Install the **Arduino IDE** and add ESP32 board support
   (Boards Manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`).
2. **Tools → Manage Libraries** → install **`M5Unified`** (accept the `M5GFX` dependency).
3. Open [`portal-evil-sticks3/portal-evil-sticks3.ino`](portal-evil-sticks3/portal-evil-sticks3.ino).
4. Select the board **M5StickS3** (install M5Stack's board package if prompted). If your core does not list it yet, select **ESP32S3 Dev Module** and set: *Flash Size* 8 MB, *PSRAM* OPI, *USB CDC On Boot* Enabled, *USB Mode* Hardware CDC and JTAG, *Partition Scheme* 8M.
5. Click **Upload**.

### ⚙️ Option B — Build the `.bin` with a script (for M5Burner)

The only prerequisite is **[arduino-cli](https://arduino.github.io/arduino-cli/latest/installation/)** on your `PATH`; the scripts install the ESP32 core and the `M5Unified` / `M5GFX` libraries automatically.

| Platform | Command |
| --- | --- |
| 🪟 **Windows (double-click)** | Double-click **`build.bat`** |
| 🪟 Windows (PowerShell) | `powershell -ExecutionPolicy Bypass -File scripts\build.ps1` |
| 🐧🍎 Linux / macOS / Git Bash | `./scripts/build.sh` |

The build writes to the **`firmware/`** folder:

| File | Flash offset | Use |
| --- | --- | --- |
| `portal-evil-sticks3-merged.bin` | **`0x0`** | ✅ Upload this to M5Burner (full image) |
| `portal-evil-sticks3-app.bin` | `0x10000` | App only |

> [!NOTE]
> The M5StickS3 is a new ESP32-S3 board. The build scripts target the generic **ESP32-S3 dev module** FQBN with the flash/PSRAM/USB options matching M5Stack's profile. If M5Stack publishes a dedicated `arduino-cli` board id (e.g. `esp32:esp32:m5stack_sticks3`), set `$Fqbn` / `FQBN` at the top of the build script to it.

---

## ▶️ Usage

1. On power-on, the device plays the laughing-skull splash (~5 s) and opens the **main menu** — it does **not** start the portal automatically.
2. Navigate with the two buttons: **BtnB** moves the highlight, **BtnA** selects.
3. **Tools → Start Portal** brings up the AP, DNS redirect, and web server, and switches to the live LCD dashboard.
4. When a device connects, the captive portal opens the fake sign-in page.
5. On submission, the device beeps, bumps the LCD count, and the client sees the animated “validating access” page.
6. Review captures (while connected to the AP) at `http://172.0.0.1/admin` — filter, export CSV/JSON, open `/settings`, or clear the list. You'll be asked to log in (`admin` / `huebr`).
7. While the portal is running, **hold BtnB for ~1 s** to stop it and return to the menu.

### 🔗 Pages / endpoints

| Path | Audience | Description |
| --- | --- | --- |
| `/` | 🎣 Victim | Fake sign-in page (any unknown URL redirects here) |
| `/post` | 🎣 Victim | Receives the form, stores the entry, returns the “validating” page |
| `/admin` | 🛡️ Operator 🔑 | Dashboard: stat tiles, AP info, searchable capture table |
| `/settings` | 🛡️ Operator 🔑 | Web config form (GET) / apply + persist (POST) |
| `/export.csv` | 🛡️ Operator 🔑 | Download all captures as CSV |
| `/export.json` | 🛡️ Operator 🔑 | Download all captures as JSON |
| `/clear` | 🛡️ Operator 🔑 | Clears all stored entries |

> [!NOTE]
> 🔑 endpoints are protected by HTTP Basic Auth when the login is enabled (default **on**). Victim-facing routes (`/`, `/post`) are never gated. Captures are saved to flash and reloaded on boot, so they survive a reboot or dead battery; the CSV/JSON exports stream the full history. **Clear all** (`/clear`) and the on-device **Reset Captures** erase the flash history too.

---

## ⚙️ Configuration

Portal text/branding are `#define` values near the top of
[`portal-evil-sticks3.ino`](portal-evil-sticks3/portal-evil-sticks3.ino): `SSID_NAME`, `SUBTITLE`, `POST_TITLE`, `POST_BODY`, `ADMIN_USER_DEF`, `ADMIN_PASS_DEF`, `MAX_CAPTURES`, and `LED_PIN` (set to a real GPIO to enable an external capture LED). Runtime settings (beep, speaker volume, brightness, screen flip, SSID, login, auto-refresh) are changed live from the on-device menu or the sectioned `/settings` page, and persisted to flash (NVS). Speaker volume and screen flip are also adjustable from `/settings`.

---

## ✅ Not yet verified on hardware

This port was adapted from the C Plus source using M5Stack's M5StickS3 documentation. The display layout, web stack, and settings carry over directly, but the following should be confirmed on a real device and tuned if needed:

- **FQBN / board options** for a clean `arduino-cli` / IDE compile.
- **Battery percentage** returned by `M5.Power.getBatteryLevel()`.
- **Button mapping** (BtnA/BtnB ↔ the two physical keys) under M5Unified.
- **Speaker volume** (`M5.Speaker.setVolume(160)`) — M5Stack recommends ≤ 75 % on battery.
- **LittleFS mount** — capture persistence needs a flash filesystem partition. The `default_8MB` partition scheme used by the build provides one; confirm `LittleFS.begin(true)` mounts it (it formats on first boot).
- **Boot laugh** — the synthesized cackle uses `M5.Speaker.playRaw()`; confirm the signature/playback on the device and tweak the `buildLaugh()` parameters to taste.
- **Menu animation timing** — the slide-in/pop delays (~16–45 ms) are tuned by eye; adjust if they feel sluggish or too fast on real hardware.

---

- [raphntc](https://github.com/raphntc) — M5StickC Plus port, further improvements, and this M5StickS3 port

---

<div align="center">

Made with ⚡ for Wi-Fi security education · **[MIT](LICENSE)**

</div>
