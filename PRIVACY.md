# Privacy and network behavior

This notice describes the MixChecker VST3 plugin source in this repository. The companion MixChecker app has its own privacy behavior, which is outside the scope of this plugin notice.

The plugin binds UDP port 49320 on the local network to discover and communicate with the MixChecker companion app. When connected, it sends audio and control data to that app on the local network. It also requests the public update manifest at `https://mixchecker.in/plugin-version.json` when checking for plugin updates. As with any HTTPS request, the update server and network providers can receive ordinary connection metadata such as the client's IP address and request time.

The plugin source contains no account sign-in, advertising SDK, or analytics upload. The local audio/control connection and update request are the network behaviors implemented by this plugin source.
