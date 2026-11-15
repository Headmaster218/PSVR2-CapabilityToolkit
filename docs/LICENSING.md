# License scope and revision history

The first-party source, scripts, tests and documentation in this revision are
licensed under **Apache-2.0**, using the complete official text in the root
LICENSE. Copyright (c) 2026 PSVR2-CapabilityToolkit contributors is retained in
NOTICE. The official text was obtained from
[the Apache Software Foundation](https://www.apache.org/licenses/LICENSE-2.0.txt).

This license update starts with this revision. Earlier revisions through
`77a13b18cd5e52f3fffaaa48cfd7ac732d5d32bc` were published with the MIT license;
their recorded licenses and Git history are retained. This change does not
withdraw previously granted MIT permissions or rewrite those revisions.

## Separately licensed material

- `patches/psvr2toolkit/camera-coefficients-double-buffer.patch` contains upstream
  PSVR2Toolkit context and remains under the accompanying MIT license.
- `patches/psvr2toolkit/UPSTREAM_LICENSE.txt` remains the complete upstream MIT
  notice, copyright 2026 Bnuuy Solutions. It is not replaced or relicensed.
- The first-party CPU reproduction script in that directory is Apache-2.0,
  like the other first-party tests; it is not a copy of the upstream driver.
- External Toolkit source/DLLs retain their own terms. The separate upstream
  README statements about non-commercial use and eye-tracking data remain
  recorded in THIRD_PARTY_NOTICES.md; they are not added as restrictions to
  this project's Apache-2.0 license.
- External OpenXR generated headers retain `Apache-2.0 OR MIT`; Valve OpenVR
  retains its BSD-style license. No license of either project is changed here.
- UnitySetup remains an external application; its code/assets are not copied.
  Sony binaries, SDK installations and private data are not distributed here.

See THIRD_PARTY_NOTICES.md for pinned upstream sources and component boundaries.
This scope document and NOTICE do not add terms to Apache-2.0 or replace any
third-party license text.
