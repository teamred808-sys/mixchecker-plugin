#include "../Source/PluginUpdateChecker.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testSemanticVersionComparison() {
    check(MixCheckerUpdate::isSemanticVersionGreater("1.0.1", "1.0.0"),
          "1.0.1 should be newer than 1.0.0");
    check(MixCheckerUpdate::isSemanticVersionGreater("1.1.0", "1.0.9"),
          "1.1.0 should be newer than 1.0.9");
    check(MixCheckerUpdate::isSemanticVersionGreater("2.0.0", "1.9.9"),
          "2.0.0 should be newer than 1.9.9");
    check(!MixCheckerUpdate::isSemanticVersionGreater("1.0.0", "1.0.0"),
          "same version should not be newer");
    check(!MixCheckerUpdate::isSemanticVersionGreater("1.0", "1.0.0"),
          "missing patch should be treated as zero");
    check(!MixCheckerUpdate::isSemanticVersionGreater("1.beta", "1.0.0"),
          "malformed latest version should not be newer");
}

void testAutomaticCheckCooldown() {
    check(MixCheckerUpdate::isAutomaticCheckDue(0, 1000),
          "automatic update check should be due when it has never run");
    check(!MixCheckerUpdate::isAutomaticCheckDue(1000, 1000 + 60 * 1000),
          "automatic update check should not be due one minute after a check");
    check(MixCheckerUpdate::isAutomaticCheckDue(1000, 1000 + MixCheckerUpdate::updateCheckCooldownMs),
          "automatic update check should be due after the cooldown expires");
}

void testJsonEvaluation() {
    const juce::String updateJson = R"json({
      "latestVersion": "1.0.1",
      "minimumRecommendedVersion": "1.0.0",
      "downloadUrl": "https://mixchecker.in",
      "releaseNotesUrl": "https://mixchecker.in",
      "message": "A new Mix Checker plugin update is available.",
      "enabled": true
    })json";

    auto result = MixCheckerUpdate::evaluateUpdateJson(updateJson, "1.0.0", "");
    check(result.valid, "newer valid JSON should evaluate successfully");
    check(result.updateAvailable, "newer valid JSON should report update available");
    check(result.latestVersion == "1.0.1", "latest version should be captured");
    check(result.downloadUrl == "https://mixchecker.in", "download URL should be captured");

    auto same = MixCheckerUpdate::evaluateUpdateJson(updateJson, "1.0.1", "");
    check(same.valid, "same-version JSON should still evaluate successfully");
    check(!same.updateAvailable, "same version should not report update available");

    auto dismissed = MixCheckerUpdate::evaluateUpdateJson(updateJson, "1.0.0", "1.0.1");
    check(dismissed.valid, "dismissed-version JSON should evaluate successfully");
    check(!dismissed.updateAvailable, "dismissed latest version should not report update available");

    const juce::String newerAfterDismissal = R"json({
      "latestVersion": "1.0.2",
      "downloadUrl": "https://mixchecker.in",
      "enabled": true
    })json";
    auto newer = MixCheckerUpdate::evaluateUpdateJson(newerAfterDismissal, "1.0.0", "1.0.1");
    check(newer.valid, "newer-after-dismissal JSON should evaluate successfully");
    check(newer.updateAvailable, "newer version after dismissed old version should appear");

    auto disabled = MixCheckerUpdate::evaluateUpdateJson(R"json({
      "latestVersion": "9.0.0",
      "enabled": false
    })json", "1.0.0", "");
    check(disabled.valid, "disabled JSON should evaluate successfully");
    check(!disabled.updateAvailable, "enabled=false should not report update available");

    auto invalid = MixCheckerUpdate::evaluateUpdateJson("{not json", "1.0.0", "");
    check(!invalid.valid, "invalid JSON should be marked invalid");
    check(!invalid.updateAvailable, "invalid JSON should not report update available");

    auto missingLatest = MixCheckerUpdate::evaluateUpdateJson(R"json({
      "enabled": true
    })json", "1.0.0", "");
    check(!missingLatest.valid, "missing latestVersion should be marked invalid");
    check(!missingLatest.updateAvailable, "missing latestVersion should not report update available");
}

} // namespace

int main() {
    testSemanticVersionComparison();
    testAutomaticCheckCooldown();
    testJsonEvaluation();

    if (failures != 0) {
        std::cerr << failures << " update checker test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All update checker tests passed\n";
    return EXIT_SUCCESS;
}
