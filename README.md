# PSVR2 Capability Toolkit

Application-side integration tools and controlled experiments for PSVR2 on
Windows. Sony's driver and [PSVR2Toolkit](https://github.com/BnuuySolutions/PSVR2Toolkit)
remain the hardware owners. This project does not replace them.

The initial repository brings together a gaze/Sense CAPI bridge, offline HDR
and SDR processing experiments, and a small proposed upstream camera fix.
It is **not a ready-to-install HDR driver, universal game injector, or bare-hand
tracking implementation**. No device software is installed by these tools.

| Module | Current evidence |
|---|---|
| Toolkit CAPI bridge | First-party bridge migrated from an existing integration; gaze, Sense haptics and triggers were previously user-tested there. This repository's port is offline-tested; hardware retest is pending. |
| SteamVR launch handoff | Reuse existing server/monitor or request fixed Steam app launch. Singleflight and retry; no native readiness gate. Native probe remains optional diagnostics. Final normal-entry cold launch is not yet verified. |
| Eye calibration | Use external Toolkit UnitySetup; no calibration writes implemented here. |
| OpenXR extension probe | Static inspection and CPU ABI tests; live enumeration requires explicit opt-in and an already running SteamVR. |
| Native HDR host | Windows x64 compile/CPU tests; explicit BT.2020/PQ capability negotiation and controlled stereo test-pattern code. No live HDR/output verification. |
| SDR enhancement | CPU reference, shader and D3D11 adapter compile/tests. No GPU dispatch/HMD verification; native HDR bypasses this stage. |
| Camera coefficients patch | CPU reproduction and unapplied patch for a pinned upstream version. Not a validated comfort fix. |
| Bare-hand tracking / raw eye images | Not implemented here. Firmware/runtime/access constraints remain. |

Machine-readable status is in [capabilities.json](capabilities.json). A compile
or an advertised extension never sets `physicalHdrVerified` or hardware readiness.

Start with [building and offline tests](docs/BUILDING.md),
[architecture](docs/ARCHITECTURE.md), [limitations](docs/LIMITATIONS.md), and
[third-party notices](THIRD_PARTY_NOTICES.md).

Default help/self-test modes do not load OpenXR, connect CAPI, create graphics
devices, launch SteamVR, register API layers, or change drivers/settings.
Actual runtime/device commands always require explicit modes.

First-party code is Apache-2.0; see [LICENSE](LICENSE), [NOTICE](NOTICE), and
[licensing scope/history](docs/LICENSING.md). Third-party components retain their own terms. Toolkit
has an MIT LICENSE **and** README statements concerning non-commercial use and
eye-tracking data; both are recorded rather than silently resolved here.
UnitySetup is an external tool, not copied into this repository. No Sony DLLs,
SDK installations, runtime profiles, calibration blobs or personal gaze data
are included. Not affiliated with or endorsed by Sony or Bnuuy Solutions.
