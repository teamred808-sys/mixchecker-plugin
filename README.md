# MixChecker plugin

MixChecker is a VST3 audio plugin that connects to the MixChecker companion app on the same local network. It is designed for Windows 64-bit hosts.

## Build

Requirements: CMake 3.22 or newer, a C++17 compiler, and the platform SDK/toolchain. CMake downloads JUCE 8.0.0 from its upstream repository during configuration.

```sh
cmake -S . -B build -DMIXCHECKER_BUILD_VST3=ON -DMIXCHECKER_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows, configure with a Visual Studio generator and build for x64. The VST3 output is under `build/MixChecker_artefacts/Release/VST3/`.

## License

This project is licensed under the GNU Affero General Public License, version 3 or (at your option) any later version. See [LICENSE](LICENSE). JUCE is fetched separately and is dual-licensed under AGPLv3 and JUCE's commercial license; this project selects the AGPLv3 route. The QR Code generator in `Source/UI/qrcodegen.*` is MIT-licensed; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Updates and network behavior

The plugin checks `https://mixchecker.in/plugin-version.json` for update information and communicates with the MixChecker companion app over the local network for its core connection feature. See [PRIVACY.md](PRIVACY.md) for the network behavior in more detail.

## Code signing policy

Code signing is not active for this repository unless a release is published with a valid signature. Any future SignPath Foundation signing will begin only after the project is accepted and the signing workflow and policy are configured. See [CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md).
