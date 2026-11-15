# Initial offline validation — 2026-11-15

Windows x64; Windows PowerShell 5.1; MSVC 14.51.36231; Windows SDK 10.0.26100.0;
FXC SM5 and optional DXC SM6; .NET Framework 4.x; official OpenXR SDK-Source
1.1.65 at the commit in dependencies.lock.json. No dependencies were installed.

The repository's `Build-Offline.ps1 -Component All` completed successfully:

- CAPI helper: strict managed compile, help/ABI and fake-mode tests, bounded
  child watchdog self-test (expected self-exit 4). No library/device connection.
- SDR: FXC cs_5_0 and DXC cs_6_0 compiled; D3D11 adapter/test linked under
  MSVC /W4 /WX; 34 CPU assertions and 12 JS reference tests passed. Only
  pre-device rejection/bypass paths were exercised by the C++ executable.
- OpenXR probe: managed x64 compile and 152-byte ABI self-test passed without
  calling native APIs.
- Native host: /W4 /WX C++ build, embedded VS/PS compilation, help and CPU tests
  passed. Known PQ 100/1000/10000-nit vectors, neutral white, invalid inputs,
  explicit-run argument rejection and native-HDR bypass were checked. No
  static OpenXR/vrclient DLL import was present in the inspected PE imports.
- Camera candidate: five CPU serialization assertions passed; no patch applied.
- All repository PowerShell scripts parsed successfully under PS5.1. The CPU
  audit checked capability claims, pinned bridge hashes, source portability and
  absence of distributed binaries. No original staging drive path is embedded
  in tracked source; tools/SDK are explicit external dependencies.

Local raw logs/build records remain ignored in `.local/build/`. They contain
machine-local paths and are not public fixtures. Build records include actual
generated shader/executable hashes and relevant input/tool hashes.

## Migration boundaries

The CAPI bridge was ported from first-party integration code that had prior
user-tested gaze/Sense feedback. The port changes defaults to help, requires
an explicit installed-DLL directory and connection command, and retains its
pinning/heartbeat/reset behavior. This repository has **not** hardware-retested
that port. Client-specific gaze projection and current Jarvis service wiring
were deliberately not migrated or modified.

The SDR algorithm/resource contract and camera candidate were imported from
offline-validated first-party experiments. The SDR shader-path constant became
a caller-resolved filename. Runtime paths and hashes in the native/probe tools
became private, explicitly reviewed local profiles rather than machine defaults.

## Not tested / not installed

No Inspect/live enumeration, CAPI --connect, XR instance/session, D3D device,
shader dispatch or headset pattern was executed during repository integration.
No driver, firmware, Sony/Toolkit/SteamVR files, existing Jarvis service,
preferences, registry or API-layer registration was changed. No physical HDR,
camera comfort fix, bare-hand tracking or feature coexistence claim follows
from these offline checks. See capabilities.json and LIMITATIONS.md.
