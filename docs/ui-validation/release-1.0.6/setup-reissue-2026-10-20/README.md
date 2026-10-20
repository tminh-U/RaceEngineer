# v1.0.6 simulator-setup reissue — 2026-10-20

> Historical simulator-setup reissue. The later [metadata reissue](../metadata-reissue-2026-10-20/README.md) replaced the public tag and assets again; the hashes below describe the earlier files.

- Binary source: `e57adf0c093eb06f77575886dc9997f5cc7b35aa`. Adds shared installer/in-app AC companion installation and ACC UDP configuration, game-setup status/repair controls, an on-demand GitHub update checker and the revised Quickstart.
- [Windows build 39372199687](https://github.com/tminh-U/RaceEngineer/actions/runs/39372199687) passed compilation, the existing 6/6 CTests, native and packaged setup UI assertions, package manifest/version verification and embedded application/installer icon checks.
- [Reissue workflow 39373778949](https://github.com/tminh-U/RaceEngineer/actions/runs/39373778949) verified that successful build's commit, both package hashes, all 1762 manifest files and the native UI executable hashes before updating the existing v1.0.6 tag, release notes and installer/ZIP/checksum assets.
- Public asset digests, tag, checksum file, portable manifest and executable were compared against that CI evidence. See [verification record](release-verification.json) and [SHA256SUMS](SHA256SUMS.txt). The original voice model hashes and Minh Quân preset are preserved.
- Static QML lint found no syntax errors; two existing unqualified-access warnings remain in the device selector delegate of `SystemDevicesPage.qml`.

The existing CTests and setup wizard assertions do not exercise the new game-setup or update-checker flows end to end. Interactive Windows UAC consent/cancellation, actual AC/ACC connections and the in-app update-checker network flow have not been exercised on a user Windows machine. Native UI instances were terminated after their exit grace period while models were warming up; orderly shutdown during warmup is not verified. AVX2 and a working Vulkan runtime/driver remain required.

Download the replacement installer from [v1.0.6](https://github.com/tminh-U/RaceEngineer/releases/tag/v1.0.6); older v1.0.6 installers do not contain these additions. Follow the [Quickstart](../../../quickstart.md).
