# igloo

A lightweight, PTY-backed UNIX domain socket `su` daemon and privilege manager for Android, featuring **Coldforces**—an integrated least-privilege policy gatekeeper that grants only the exact access requested per call.

> **⚠️ COMPATIBILITY & SAFETY DISCLAIMER**
> - **HyperOS 1.0 Only:** This release is built and tested **exclusively for Xiaomi HyperOS 1.0** (specifically verified on `dizi` / `5.10.209-android12-9`). Do **not** run this build on HyperOS 2.0+, AOSP custom ROMs, OneUI, or other OEM firmware.
> - **Use at Your Own Risk:** Provided strictly for educational, research, and authorized development purposes. The author is not responsible for boot loops, data loss, or device instability. Always back up your data before testing.

---

## Key Features

- **PTY-Backed IPC Architecture:** Handles interactive shells and single-command execution (`OP_CMD`) cleanly over a UNIX domain socket with full TTY signal and terminal support.
- **Coldforces Policy Enforcement:** Blocks unauthorized callers and enforces the Principle of Least Privilege by granting only the specific access requested before execution.
- **Zero Kernel Hooks:** Operates without injecting unstable `kprobes` or inline kernel hooks that trigger `CONFIG_CFI_CLANG` panics.

---

## Building from Source

Requires `clang` (Termux or Android NDK) and `strip`:

```bash
make
```

Or compile manually:

```bash
clang -O2 -Wall igloo.c -o igloo
strip igloo
cp igloo su
```

---

## Usage

Check daemon status:

```bash
./igloo --status
```

---

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)** — see the LICENSE.md file for details.
