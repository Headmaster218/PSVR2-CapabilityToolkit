# Building and offline tests

Requirements: Windows x64, Windows PowerShell 5.1, .NET Framework 4.x compiler,
Node.js 18+, Visual Studio C++ x64 tools and Windows SDK with FXC. The scripts
locate VS using vswhere; `-VisualStudioPath` overrides discovery. Nothing is
installed or downloaded. Builds/logs/profiles are ignored under `.local`.

From the repository root:

```powershell
node tests/offline.cjs
powershell.exe -NoLogo -NoProfile -NonInteractive -File scripts/Build-Offline.ps1
```

The default Core build compiles/tests the managed bridge, SM5 shader, C++ SDR
adapter and managed probe. It invokes only help/self-test/rejection/bypass
branches. Optional `-DxcPath <dxc.exe>` compiles SM6 separately.

## Native HDR host

Supply a separate official OpenXR header directory (containing `openxr/`) or
the official SDK-Source checkout. The tested source version is 1.1.65, commit
`a9f14c416e383d05c142f5a2539a91fe3931b52a`; source URL is in dependencies.lock.json.
For example, obtain the immutable official archive yourself from
[Khronos](https://github.com/KhronosGroup/OpenXR-SDK-Source/archive/a9f14c416e383d05c142f5a2539a91fe3931b52a.zip)
and extract outside tracked source (for example ignored `deps/`). No SDK or
generated headers are committed here. SDK-Source header generation requires an
existing Python environment satisfying the upstream generator's requirements.

```powershell
powershell.exe -NoLogo -NoProfile -NonInteractive -File scripts/Build-Offline.ps1 -Component All -OpenXrSdkSource '<official SDK-Source directory>' -PythonPath '<existing python.exe>'
# Or: -Component All -OpenXrInclude '<official include directory>'
```

This still runs only offline modes. Build records include inputs/tools and
actual shader/executable hashes; raw logs are local and should not be published.
The native launcher refuses stale recorded inputs. Root/source location is
derived from the checkout, so a fresh archive builds without the original
development staging directory. External SDK/tool directories are explicit
dependencies, not copied into the repository.

## Future device/runtime tests (not part of the offline build)

The probe's default Inspect is static registry/manifest/hash inspection:

```powershell
powershell.exe -NoProfile -File modules/openxr-probe/Invoke-OpenXrHdrCapabilities.ps1
```

It creates `.local/runtime-profile.json` without loading XR. Review its paths,
hashes, environment and layer information. Existing profiles are not silently
overwritten. Keep it private; if runtime files change, preserve the old record
and explicitly create/review a new profile.

With the headset ready and SteamVR already started normally, explicit live
operations are `Enumerate -Profile <reviewed JSON>` on the probe, or
`-Mode Capabilities` / `-Mode Pattern` with the same `-Profile` on the native
host launcher. They can load runtime/create devices according to their mode;
they are **not** offline tests. No command starts/restarts SteamVR or registers
a layer. Read each module's README before any live operation.

The CAPI helper similarly needs explicit `--connect --dll-dir <installed Toolkit
directory>` and a JSON-lines heartbeat client. UnitySetup is used externally;
there is no auto-calibration or firmware action. No helper is wired to an
existing Jarvis service by building this repository.
