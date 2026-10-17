# AutoRestock 0.3.0

[繁體中文](README.md) · [Download v0.3.0](https://github.com/XuanDiNa/AutoRestock/releases/tag/v0.3.0)

A native Windows x64 Minecraft Bedrock client plugin that refills the main hand
from the 27 main-inventory slots. Inspired by
[Tweakeroo Hand Restock](https://github.com/maruohon/tweakeroo); it does not load
Java mods or require a behavior pack. Use only where client modifications are
permitted by the server's rules.

## Features

- One DLL selects the exact supported game profile automatically.
- Main-hand restocking from slots 9–35 only. Other hotbar slots and the offhand
  are not reserve sources; the offhand is not refilled.
- JSON-only enable/disable and refill threshold; no hotkey, chat or HUD prompts.
- Full item-data comparison, including names, enchantments, tags and components.
- A genuine-depletion fallback can refill an empty main hand after the native
  world-use callback observed the same item being consumed to zero. This is not
  a generic empty-slot filler and does not guess an item after manual clearing.
- Same-session ordinary-transaction replanning after nonfatal tracking mismatch,
  instead of requiring a reconnect for that class of interruption.
- At least 1500 ms between native submissions. Version, session, thread,
  inventory-lifetime, item identity and native-transaction checks remain active.

## Exact supported versions

| Bedrock Windows x64 | Artifact | Manual acceptance |
| --- | --- | --- |
| 1.26.45.01 | One shared DLL | User reported normal use in the tested environment |
| 1.26.51.01 | The same DLL | User reported normal use in the tested environment |
| 1.26.52.03 | The same DLL | User reported normal use in the tested environment |

Each exact profile has independent PE identities, disk `.text` hashes, 96 native
address mappings and 65 loaded-byte guards; the plugin installs 21 existing
hooks. Unknown or mismatching images are refused, not routed to the 26.52
addresses. Loaded ImageBase accepts only the preferred base or the module's
actual loaded base; strict disk identity checks remain.

These reports do not establish support for every server implementation,
anti-cheat, network condition, recovery branch or long-duration workload.
BDS was not part of this acceptance target. Ready/queued/deferred log entries
alone are not server acknowledgments or item-count conservation proof.

## Release identity

- Package/manifest/mod version: **0.3.0**.
- Internal DLL build and log identifier: **0.3.0-test3**.
- DLL SHA256: `C2073234D4611FF6241340A1117B33815268D45C00C7075049B39D93E8C8FE6A`.
- Official ZIP SHA256: `90E19A615C1054CE72F123A52B31E3F3C71643937D1641F0EED1389DD424E5F8`.

The official release reuses the byte-identical user-tested test3 DLL. It was not
recompiled, relinked or patched to change its version strings. Historical text
such as `candidate` or `manual gameplay validation pending` is retained inside
that binary; the acceptance record above supersedes that delivery-time wording.
If test3 already works, there is no need to replace the DLL or repeat gameplay
testing solely for this metadata-only formal release.

## Installation and update

1. Download the ZIP and its adjacent `.sha256` file from GitHub Releases. Check
   the ZIP with `Get-FileHash -Algorithm SHA256` before installation.
2. Fully exit Minecraft. Back up the existing `mods/AutoRestock/` directory,
   including configuration, metadata and logs.
3. Use a compatible native-DLL loader that supports the supplied
   `preload-native` manifest, such as the LeviLauncher setup used for testing.
   Extract the seven root files into that loader's `mods/AutoRestock/` directory
   for the exact game version. This is not an `.mcpack` or a vanilla-game DLL
   installation procedure; a suitable native loader is required separately.
4. On upgrade, **keep your existing `config.json` and `AutoRestock.log`**. Do not
   overwrite custom configuration with the ZIP default. Preserve existing
   disabled `.close` filenames/states rather than silently enabling the plugin.
5. Set the retained JSON as needed, then launch the exact supported game build.
   A normal startup selects its exact profile and logs `Ready: 0.3.0-test3`.
   This proves local initialization, not successful remote restocking.

The ZIP contains `AutoRestock.dll`, `manifest.json`, `mod.json`, `config.json`,
`config.schema.json`, `README.md` and `THIRD_PARTY_NOTICES.txt`. To preserve the
accepted package hash, its original Chinese README remains unchanged. Complete
English documentation is provided here and in the bilingual Release notes.

## JSON configuration

The shipped configuration is:

```json
{
  "enabled": true,
  "preRestockThreshold": 17
}
```

`enabled` is the only switch. The threshold means **strictly less than**, so 17
attempts refill at remaining count **16 or below**; 16 means 15 or below. Its
range is 1–64, subject to native stack limits. If the field is omitted, the
existing parser/schema default remains 6; the package explicitly specifies 17.

Configuration reload is approximately every 500 ms. Missing files, malformed
JSON, invalid types/ranges, duplicate or unknown fields retain the last valid
configuration; the plugin does not write back to your file. Avoid editing while
a refill is active. Existing tool-replacement rules are unchanged.

## Empty-hand fallback and recovery details

The fallback remembers full item identity only when native world-use processing
really observed a non-tool stack go from nonempty to empty. A later empty-slot
sync alone need not discard that intent. A consistent main hand plus all 27
reserve slots must still be sampled across different ticks before replanning.
Only a current, fully matching reserve stack can be selected.

A native submission consumes the fallback intent; no matching reserve clears
it, rather than moving an unrelated later pickup. Native busy-before-submit
may retain it. UI opening, focus loss, slot/config changes, world/player/inventory
lifetime changes, a newly nonempty hand or invalid/stale observations invalidate
the intent as appropriate. The plugin does not force-send or clear native queues.

Ordinary remote tracking/recovery interruptions discard the local plan, wait at
least 1500 ms and resample within the same admitted session. Two complete,
different-tick snapshots must agree; any main-hand change must be explained by
observed continuous use. This path intentionally no longer requires old-request
two-slot coverage or a fresh post-sync use event; genuine-depletion identity is
still required for an empty hand. The original inbound route keeps two-slot
coverage, with the depletion fallback replacing its extra-use requirement.

A ten-second recovery timeout cancels that attempt rather than permanently
locking until reconnect. This is a new plan based on current state, not replay
of an old transfer or a manually resent packet. Late server updates still apply
normally and may interfere with later planning. Fatal native/session/thread/hook
failures remain fail-closed. There is no guarantee of gapless block placement.
Single-player and modern remote routes retain their native acceptance and exact
inventory checks rather than adopting the ordinary-route relaxation.

## Safety and logs

Native inventory setters let the game generate ordinary transactions. No hidden
inventory UI, edited borrowed stack/network ID, manual second packet send,
rollback, queue clearing, suppressed server update or EXE modification is used.
Log formatting/writing runs on a background worker; game callbacks pass bounded
copied data. English runtime messages are retained.

`Request ... emptyFallback=1` records a fallback submission, not remote success.
`PredictionPaused`, `ResampleBaseline`, `ResampleReady` and `PredictionRearmed`
describe local planning only. `PredictionStopped ... reconnectRequired=0
automaticRecovery=1` cancels the current attempt, not necessarily the session.
`serverConfirmed=0` and `oldRequestCompleted=0 oldRequestReplayed=0` remain honest
diagnostic boundaries, not fabricated ACKs.

## Disable, remove or roll back

Set `enabled` to `false` to disable via JSON. To remove or roll back, fully exit
Minecraft first, then disable/remove this plugin from the loader or restore the
backed-up DLL and metadata. Keep your configuration and logs. Do not replace a
loaded DLL and do not delete another plugin or the game executable.

## Build from source and verification

Requires Windows x64, Visual Studio 2022 C++ tools/Windows SDK, xmake and
PowerShell 7. JSON 3.11.3 is included under `vendor/json/`; xmake obtains static
MinHook 1.3.4. See `THIRD_PARTY_NOTICES.txt` for their licenses. No new project
source license is granted by publication alone.

```powershell
xmake f -m release -y
xmake build -y
pwsh -File tools/verify_release.ps1
```

The verifier builds/runs offline Debug/Release checks; it does not install,
start or attach to Minecraft, nor replace/repackage the accepted DLL. Optional
six-image identity/guard tests require legally obtained local game images that
are never distributed. See [verification details](docs/verification.md).

The original frozen build recorded 115 historical test executions, including
private image/leaf and analysis tests; those are distinct from the public-source
suite. A clean source rebuild can have a different binary hash and is **not**
automatically the manually accepted artifact.

Report issues with exact game version, single-player/server environment,
reproduction steps and short sanitized log excerpts. Do not upload game EXEs,
credentials, full private logs or analysis databases.
