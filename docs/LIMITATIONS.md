# Known limitations

- No actual VR/runtime/GPU operation has been performed from this repository.
- Prior user-tested gaze/Sense behavior came from the original integration;
  the portable CAPI port still requires its own hardware regression test.
- Toolkit CAPI is experimental/WIP; the bridge pins its two external DLL hashes.
- There is no calibration writer, automatic firmware downgrade, raw-eye access
  implementation, bare-hand tracker, or arbitrary-game HDR injection.
- Standard extension support, floating formats, compilation or frame submission
  do not establish physical HDR output or feature coexistence.
- Firmware 7.00 gaze access does not establish raw eye-image access. Toolkit's
  documented raw-feed jailbreak route targets 6.00; no firmware action is taken.
- The proposed camera patch is unapplied and not a validated dizziness fix.
- Live test modes require already-running SteamVR and explicit opt-in. A hung
  native call can require termination of the owned helper; graceful resource
  cleanup is not claimed after forced termination.
