# Toolkit CAPI application bridge

First-party JSON-lines bridge migrated from an existing user-tested integration.
This repository port has offline coverage; no hardware connection has been
tested here. It is not a USB driver and does not implement raw cameras or
calibration. Sony/Toolkit keep device ownership.

Build with Windows PowerShell 5.1 and `build-offline.ps1`. Outputs are ignored
under `.local/build/toolkit-capi`. No DLLs are copied. The default executable
mode is help; all self-test modes use fake delegates or struct checks only.

Only `--connect --dll-dir <installed Toolkit directory>` loads native code.
The two required external DLL hashes match dependencies.lock.json; a mismatch
refuses connection. SteamVR must already be running. The bridge does not start
or restart it. Toolkit CAPI is WIP: do not update its ABI without review.

Input commands are JSON lines: `heartbeat`, `configure` (`gaze` boolean),
`effect`, `mode`, `reset`, `close`. Effects/modes require explicit requests;
the bridge does not apply haptics on connect. `reset`/`close` clear selection.
Clients must send heartbeat more often than 1500ms; timeout/disconnect/native
stall bounds cause cleanup or process exit. Short feedback expires and restores
the selected mode; selected continuous modes require the live client heartbeat.
No physical effect measurement is fabricated in acknowledgements.

`effect` accepts `kind` (pcm/trigger), `controller` (left/right/both), `preset`
and `commandId`. Read the finite allowlists in toolkit-helper.cs; arbitrary
device payloads are not exposed. `mode` selects supported continuous presets;
`normal` restores the relevant kind. Gaze messages include validity, a source
timestamp, receipt time, head-local origin (m) and normalized direction.
They are already processed gaze, not raw images. The receipt-anchored clock is
not a measured sensor-to-display latency. No gaze payload is saved by default.

Calibration remains external UnitySetup. A nonzero calibration ID or stored
blob is not synthesized by this adapter. UI integration, reconnection policy
and original client-specific projection/hit testing remain client responsibilities.
