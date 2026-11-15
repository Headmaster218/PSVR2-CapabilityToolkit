# Architecture

Clients (including Jarvis) consume application-side adapters. Sony/Toolkit
remain responsible for the device, USB commands and private driver hooks.
The CAPI bridge owns one client slot, not the hardware. Its line protocol
preserves gaze timestamps, units, validity and head-local coordinates, and
has bounded effects/reset/disconnect behavior.

Calibration is delegated to external UnitySetup. The upstream driver stores
and restores its blob; no private calibration commands are exposed here.
Camera perception and bare-hand tracking are future modules, not fabricated
capabilities. Switching runtime requires a separate integration assessment.

Graphical experiments are isolated from the bridge. Native HDR uses explicitly
enumerated BT.2020/PQ and matching format plus metadata, never an assumed
80-nit extended-linear/scRGB contract. SDR enhancement produces application-
owned RGBA16F linear BT.709 relative-white values (1=referenceWhiteNits).
Native HDR bypasses SDR expansion; a bypass never consumes an unwritten output.
The current HDR pattern does not yet dispatch the SDR enhancement adapter.

Driver changes belong in small pinned patches, preferably contributed upstream.
A thin fork is warranted only for required unmerged changes; broad duplicate
USB ownership or whole-driver replacement is outside this project.
