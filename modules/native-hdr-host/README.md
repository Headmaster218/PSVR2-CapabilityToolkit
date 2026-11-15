# Controlled native HDR host (experimental)

Windows x64 C++17/D3D11/OpenXR host, derived from a first-party offline test
experiment. Official SDK headers are supplied separately, never copied from a
system SDK into this repository. See the root build guide.

Default EXE/launcher mode is help; self-test is pure CPU and checks that no XR
module was loaded. XR_NO_PROTOTYPES and dynamic function resolution avoid
static OpenXR/vrclient imports. D3D11/DXGI system imports do not create a device
in offline branches. No game injection, layer registration or SteamVR startup.

Live `Capabilities` requires an explicitly reviewed local profile (paths and
SHA256 pins for manifest, loader, runtime) and already-running SteamVR. It
enumerates extensions before creating anything else. Missing D3D11/SONY color
space exits before instance/device/session. If present, it creates an instance,
selects the runtime's required physical adapter LUID, creates the D3D11 device
and session, and enumerates formats and each format's SONY color spaces.
No swapchain/frames are created by Capabilities.

Pattern additionally requires SONY HDR metadata and an explicitly enumerated
BT2020_PQ render-target format (R10G10B10A2_UNORM preferred; FP16 only if also
explicitly PQ). It creates two eye swapchains and known neutral/color patches.
1 in source linear BT.709 is reference-white nits; the final shader applies
BT.709→BT.2020 then ST2084 absolute-nits encoding. It never assumes extended
linear means Windows scRGB/80 nits. Metadata describes this test source, not
measured OLED output. The patterns do not dispatch the SDR expansion stage.

Ranges: 1–30 seconds, reference white 20–200 and peak reference-white–400 nits.
These are encoding targets. CPU validation rejects invalid/nonfinite inputs.
The fixed pattern shader keeps the exact PQ equation; only FXC's conservative
pow-domain warning 3571 is suppressed for host-validated nonnegative samples,
without abs/clamp substituting invalid parameters. Other warnings are errors.
CPU tests include known ST2084 100/1000/10000-nit vectors and neutral white.

Runtime retains COM device/context ownership across all XR destruction, including
early return/exception. Images are used only after acquire+successful wait,
unbound before release. Session STOPPING is handled on normal exit. The launcher
has an external hard timeout, kills only its child if stuck and records forced
termination; graceful cleanup is not claimed after that exceptional kill.

No GPU, XR session, physical HDR, driver coexistence or display behavior has
been tested from this repository. Capability advertisement, FP16/10-bit formats
or submitted frames alone do not prove HDR. Absence of a SONY OpenXR extension
does not establish impossibility of separate OpenVR-private approaches.
