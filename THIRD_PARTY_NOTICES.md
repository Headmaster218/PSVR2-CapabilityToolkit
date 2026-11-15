# Sources and third-party boundaries

First-party integration, native test-host, processing and CPU-reproduction code
was consolidated from earlier development experiments on 2026-11-15. It is
covered by this repository's Apache-2.0 license at this revision. Earlier
MIT-licensed revisions remain in Git history; see docs/LICENSING.md. Source migrations preserve behavior
unless portability/default-offline changes are explicitly documented.

## PSVR2Toolkit

Pinned source: [9900be1](https://github.com/BnuuySolutions/PSVR2Toolkit/tree/9900be1edac6ea8fce03ea5224b5ccf8c6d7ea33).
Its [LICENSE](https://github.com/BnuuySolutions/PSVR2Toolkit/blob/9900be1edac6ea8fce03ea5224b5ccf8c6d7ea33/LICENSE)
is MIT, copyright 2026 Bnuuy Solutions. Its
[README](https://github.com/BnuuySolutions/PSVR2Toolkit/blob/9900be1edac6ea8fce03ea5224b5ccf8c6d7ea33/README.md)
also says intended for non-commercial use and prohibits commercial use of its
eye-tracking data. These statements are recorded together; this project does
not decide their legal relationship or provide commercial-use clearance.

No Toolkit DLL, Sony DLL or full driver source is redistributed. The small
unapplied patch includes upstream context under its MIT notice, included beside
the patch. Toolkit's own external dependencies keep their respective licenses.

## UnitySetup

[v1.1.0](https://github.com/BnuuySolutions/PSVR2Toolkit.UnitySetup/tree/9dfb55e1caa7e09e9f66ac9835ace08835e8443f)
is referenced only as an external calibration application. No root project
LICENSE was found in that pinned tree; its Valve dependency's license must not
be treated as authorization for all Unity scripts/assets. None is copied here.

## OpenXR / OpenVR / Windows

OpenXR generated headers from SDK source 1.1.65 carry `Apache-2.0 OR MIT`;
the build consumes a separately supplied official SDK/include directory and
does not vendor generated headers. Keep the corresponding upstream notices
when distributing a build containing them. Full SDK components may have
different notices. Source commit and URL are in dependencies.lock.json.

Valve OpenVR's [LICENSE](https://github.com/ValveSoftware/openvr/blob/master/LICENSE)
is a three-clause BSD-style license. No full Valve SDK is vendored here;
interface names in the proposed patch refer to the upstream driver.
The optional readiness diagnostic also consumes official OpenVR headers from
the pinned external source; normal SteamVR launching does not need that SDK or
a native binary. The installed Steam/OpenVR DLLs are not redistributed.

Windows SDK, MSVC, FXC and .NET Framework are external build/runtime tools,
not distributed SDK contents. Sony's original driver remains separately
installed under its own terms; this repository's Apache-2.0 license does not grant
rights to distribute it. No GPL or license compatibility claim is inferred for
unreviewed alternative runtimes.
