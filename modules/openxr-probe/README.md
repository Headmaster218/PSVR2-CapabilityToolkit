# Read-only capability probe

`Inspect` (default) reads the active SteamVR manifest and hashes its loader and
runtime. It writes ignored `.local/runtime-profile.json` once, never silently
refreshing an existing profile. It does not load XR or start SteamVR. Paths are
resolved locally rather than shipped as machine-specific defaults. Review this
profile before any live test; do not publish it.

`SelfTest` compiles a managed x64 helper and tests the 152-byte ABI without native
calls. `Enumerate -Profile <reviewed file>` additionally requires already-running
server/compositor, no runtime environment override, unchanged active manifest
and file hashes. It only enumerates available extensions; no XR instance,
session or D3D device is created. Enumeration loads the runtime, so it is an
explicit live operation even though it creates no session.

The output records all extensions/versions, return codes, completeness and
SONY extension presence. Failure/incomplete enumeration returns unknown, not
unsupported. Bounded capacity retries and 128-byte names prevent overreads.
Implicit layers may contribute extensions. Formats/color spaces need a session
and are deliberately not queried here. Physical HDR remains unverified.
