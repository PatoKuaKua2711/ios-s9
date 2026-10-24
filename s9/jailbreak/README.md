# Galaxy S9 (SM-G9600) — iOS 16.7 jailbreak userland

This turns the XNU + iOS 16.7.16 userland (booted by the shim, see `../README.md`
and `../NOTES.md`) into a **usable, tweak-capable jailbreak**, with internet over
the USB cable, SSH, ElleKit tweak injection, Sileo and NewTerm.

> **This is an AI-made fork.** The whole Galaxy S9 bring-up in this repository
> — the shim, the XNU patches, the device drivers, and this jailbreak layer —
> was built by a human directing an AI (Claude) step by step on real hardware.
> It is a hobby research port, provided as-is. See **CREDITS** below: it stands
> entirely on other people's work.

## How it fits together

1. `fetch-jailbreak.sh` downloads the third-party pieces (palera1n's Procursus
   rootful bootstrap, ElleKit, Sileo, NewTerm, OpenSSH, …) from their **official
   sources** into `jb/`. This repo does **not** redistribute them. It also takes
   this repo's own iOS pieces from its release (`s9net`, the `S9Wallpaper`
   tweak; source in `../ios-tools`) and the default wallpaper (`wallpaper/`).
2. `../tools/prep-ios6.sh` (run as root) builds the iOS root image: the Procursus
   bootstrap + those packages, our `s9net` (publishes the USB network to
   `configd`), the SSH key, and the launchd changes.
3. `../tools/jb-launchd.py` adds the boot jobs: the ElleKit loader (tweak
   injection), `s9net`, `sshd`, and `com.ios-s9.respring` (one respring per
   boot, once the ElleKit loader is done: the first SpringBoard of a boot
   starts before injection is ready); and sets `DYLD_INSERT_LIBRARIES` on
   SpringBoard.
4. `../tools/trim-daemons.py` removes ~82 Apple daemons that only crash-loop and
   eat RAM on a device with no cellular / account / sensors / BT / Wi-Fi / GPU —
   without this, jetsam kills Sileo/NewTerm/WebContent on launch (2 GB, ~28 MB
   free → 163 MB free).

## What works

- **Internet over the USB cable** — the PC shares its connection; `s9net`
  publishes the `ue0` service so `configd` sees a network.
- **Tweak injection** — ElleKit's `pspawn` is loaded into `launchd` at boot;
  SpringBoard and apps are injected (verified with ShowMyTouches + a probe dylib).
- **Sileo**, **NewTerm**, **OpenSSH**, **PreferenceLoader** (tweak deps resolve
  on-device via the BigBoss/Procursus repos).
- The permissive kernel (`cs_enforcement_disable=1`, built-in AMFI/sandbox
  stand-ins) allows unsigned tweaks and `task_for_pid`.
- **Tweaks on every boot** without a manual respring (`com.ios-s9.respring`).
- **Default wallpaper** on the lock screen (`S9Wallpaper`, original artwork in
  `wallpaper/`). Use your own: replace
  `/var/mobile/Library/S9Wallpaper/wallpaper.jpg` and respring; create
  `/var/mobile/Library/S9Wallpaper/disabled` to turn the tweak off.
- **19 Sileo repos** preinstalled (`repos.sources`), each checked to serve a
  valid index on 2026-10-24.

Installing a tweak by hand: never copy a `.dylib` over one that SpringBoard has
loaded (same file). The next SpringBoard maps it with a stale code signature,
loses its entitlements and aborts in `+[SpringBoardUI load]`, which sends
ElleKit into safe mode. Copy it to a new file, then `mv` it into place.

## Remote control over the USB cable (no screen needed)

- `../tools/s9ssh.sh [cmd]` — root shell on the phone (`root@192.168.7.2`).
- `../tools/s9-log.py FILE` — live kernel console (UDP 5140).
- `../tools/s9-twrp.sh` — reboot to TWRP (WIP; Samsung uses the `misc`/BCB, not
  the Qualcomm PON reason, so this is not reliable yet).

## Honest status / not done

- Safari does not load web pages yet; Settings → About and the Photos app do
  not open (Photos' daemons are trimmed to save RAM).
- The home screen wallpaper is black: PosterBoard finds no poster providers
  (the first-boot LaunchServices database lacks the plug-ins that live inside
  private frameworks), and `S9Wallpaper` only covers the lock screen so far.
- No GPU (CPU rendering), no cellular/Bluetooth/Wi-Fi radios, no camera.
