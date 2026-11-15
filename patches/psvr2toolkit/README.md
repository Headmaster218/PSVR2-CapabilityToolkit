# Proposed camera coefficient payload fix — NOT APPLIED

Upstream PSVR2Toolkit tag v1.0.0-experimental-3, commit
`9900be1edac6ea8fce03ea5224b5ccf8c6d7ea33`:
[hmd_device_hooks.cpp](https://github.com/BnuuySolutions/PSVR2Toolkit/blob/9900be1edac6ea8fce03ea5224b5ccf8c6d7ea33/projects/psvr2_openvr_driver_ex/driver_hooks/hmd_device_hooks.cpp).
Upstream MIT notice is included as UPSTREAM_LICENSE.txt; see root notices for
the separate upstream README statements. Only a small patch/context is included.

The upstream source copies double fitted coefficients into a float[2][8]
buffer with memcpy(sizeof(destination)), then advertises a double property.
There is no numeric double→float conversion: the 64-byte payload preserves the
first eye's 8 doubles but omits the second eye. The proposed double[2][8]
buffer provides the intended 128 bytes while preserving the property tag.

`node reproduce-coefficient-payload.cjs` tests that serialization distinction
with synthetic coefficients on the CPU. It does not load OpenVR or modify the
driver. No script automatically applies this patch. It has not been built into
the actual upstream driver, installed or tested with a headset, and does not
establish that this property is used by every passthrough entry point.
It is not a confirmed cause/fix of near-field discomfort or dizziness.

Review against the pinned upstream, contribute a small patch if appropriate,
and perform an independently authorized driver build/A/B before claiming a fix.
