#include "../Source/PluginProcessor.h"
#include <iostream>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    MixCheckerProcessor first;
    if (first.getNetworkProblem().isNotEmpty()) {
        std::cout << "SKIP: requires a usable LAN IPv4 and free UDP 49320\n";
        return 77;
    }
    int failures = 0;
    const auto check = [&failures](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    const auto addresses = first.getAvailableNetworkAddresses();
    check(!addresses.isEmpty(), "LAN addresses available");
    for (const auto& ip : addresses) {
        first.selectNetworkAddress(ip);
        check(first.getQrPayload().contains("host=" + ip + "&port=49320"), "Selected address reaches QR immediately");
        check(first.getLocalIpAddress() == ip, "Displayed address matches QR");
    }
    const auto before = first.getQrPayload();
    first.selectNetworkAddress("127.0.0.1");
    check(first.getQrPayload() == before, "Reject unavailable/loopback override");
    first.selectNetworkAddress({});
    check(first.getQrPayload().startsWith("mixchecker://pair?"), "Return to automatic mode");
    std::unique_ptr<juce::AudioProcessorEditor> editor(first.createEditor());
    check(editor->getWidth() == 520 && editor->getHeight() == 480, "Network controls have dedicated layout space");
    if (argc > 1) {
        juce::FileOutputStream output{juce::File(argv[1])};
        check(juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), output),
              "Render editor preview");
    }
   #if JUCE_WINDOWS
    MixCheckerProcessor second;
    check(second.getNetworkProblem().isNotEmpty(), "Second instance reports port ownership conflict");
    check(second.getQrPayload().isEmpty(), "Blocked instance must not advertise a QR");
   #endif
    std::cout << "Connection tests: " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
