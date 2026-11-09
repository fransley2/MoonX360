# Install the local candidate

1. Back up the working XEX and existing `moonlight` data folder, including INI, profiles and pairing files.
2. Copy `build/release-candidate/default.xex` to a separate writable Xbox HDD/USB folder. Launch it through Aurora or the normal homebrew launcher. Keep the working installation available for rollback.
3. For an update using existing settings, retain the original `moonlight` folder. Do not distribute or replace another user's credentials, certificates, host records or configuration. A new empty installation starts with accurate Off decoding.
4. In Stream settings, select Custom before an A/B comparison, choose the requested dimensions/FPS/bitrate and select Off, Partial or Full. The order is Off → Partial → Full → Off. Full is experimental. Start a fresh stream after each change.
5. Test with Periodic Picture Refresh and refresh smoothing Off initially. Enable the diagnostic overlay for instrumented runs. Stop normally before copying session reports.
6. The separately compiled `MoonX360Shortcut/default.xex` is a launcher template. Create shortcuts using the existing MoonX360 Aurora workflow; it is not the main application. Existing shortcut ABI and identities are unchanged. Aurora scripts are supplied separately in the local package.

Main dashboard resources and fonts are embedded in the XEX. No SDK runtime libraries or headers need to be copied to the console. If returning to an older binary after saving profiles with this candidate, restore the backed-up profile/config files as well.

Host notes: Sunshine, Apollo and Vibepollo detection, permission and GameStream paths have desktop fixture coverage only. Unknown compatible hosts remain unknown. Android-compatible GameStream host behavior has not been tested live; this native client does not implement an Android runtime. Ethernet and Wi-Fi require real network tests. No host software or encoder settings were changed during candidate preparation.
