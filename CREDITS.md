# Credits

This project is an **AI-assisted hobby fork**. It exists only because of the
work below, and claims none of it. Huge thanks to every author.

## Foundations
- **ios-sweet** (dipa-studio) — the XNU + iOS 16.7 bring-up on a Qualcomm phone
  (Redmi Note 10 Pro) that this Galaxy S9 port is forked from. MIT.
- **Apple** — XNU and the iOS userland. XNU is under the Apple Public Source
  License (APSL 2.0); the iOS userland is Apple's and is **not** redistributed
  here. No Apple binaries are included in this repository.

## Jailbreak userland (downloaded from official sources, not redistributed here)
- **palera1n** & the **Procursus** team — the rootful bootstrap and the package
  ecosystem (`apt`, `dpkg`, the base tools).
- **ElleKit** (Évelyne / evelyneee) — the tweak injection / Substrate replacement.
- **Sileo** (Sileo Team, CoolStar et al.) — the package manager.
- **NewTerm** (Adam Demasi / HASHBANG Productions) — the terminal. GPLv3.
- **PreferenceLoader** (Procursus / DHowett lineage) — Settings panels for tweaks.
- **OpenSSH**, **libpcap**, **network-cmds** — via Procursus.
- **BigBoss** and the tweak repos — where tweaks and their dependencies come from.
- **ShowMyTouches** (dvntm) — used only to verify injection.

## Hardware reference
- **LineageOS / Samsung** SDM845 kernel sources (GPLv2) — the register sequences
  and device tree used to bring up the panel, touch, USB, RPMh, OSM, SMMU, etc.
- **U-Boot** (`qcom-hyp-smmu.c`, Linaro) — the apps-SMMU-behind-hypervisor recipe.

## This fork
- The Galaxy S9 shim, XNU patches, drivers and this jailbreak layer were written
  by a human + **Claude (Anthropic)** working together on real hardware.

Licenses of third-party components are their own; see each project. Nothing here
is affiliated with or endorsed by Apple, Samsung, or any of the above.
