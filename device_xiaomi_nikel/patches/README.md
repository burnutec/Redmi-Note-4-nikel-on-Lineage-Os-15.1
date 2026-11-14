# Source patches for Lineage 15.1

Each file `patches/<path>/<name>.patch` is applied with `git apply` inside
`<path>` of the Lineage source tree (for example `frameworks/opt/telephony`).
`nikel_lineage_build.sh extract` applies them and skips ones already applied.

- `frameworks/opt/telephony/0001-...`: lets a device turn off AOSP radio
  capability switching (`ro.telephony.disable_rc_switch=true`). SHIFT's
  MediaTek RIL fails those requests, and the retries keep the radio off.
