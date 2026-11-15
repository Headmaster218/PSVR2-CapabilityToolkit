# SteamVR launch handoff

Normal path: check for an existing vrserver/vrmonitor process; reuse it if present,
otherwise request the installed Valve-signed Steam client to run fixed app ID
250820. Requests share one in-flight operation and a short launch cooldown.
Failures require explicit retry. It never closes/restarts SteamVR or a game.

A successful command is only `launch-requested`; an existing process is only
`already-running`. `ready`/`hardwareReady` remain null. Neither means a headset
can display VR. The browser handles the original WebXR request and its normal
errors. No pose, compositor timing, HDR check, OpenVR SDK or compiled native
helper is required for launching. Background helper processes are hidden;
Steam controls the application's normal visible windows.

prepare.mjs + inspect-start.ps1 are the normal application-side adapter. The
inspection script only reads processes/Steam installation metadata. It accepts
no arbitrary program or app ID from an HTTP request. The application integration
must enforce its own trusted local Origin and explicit retry rules.

readiness.cpp, build-offline.ps1 and verify-install.ps1 remain **optional separate
diagnostics** from an earlier experiment. They are not imported by the normal
launch path. Their Background OpenVR connection does not render or start a
runtime; use only when separately requested. Official headers remain external.

Run `node --test adapters/steamvr-start/test-prepare.mjs` for mocked CPU tests.
Actual cold launch of the final simplified entry remains unverified. An earlier
normal-runtime attempt reached the compositor but failed display readiness;
this adapter does not diagnose or fix display/driver problems.