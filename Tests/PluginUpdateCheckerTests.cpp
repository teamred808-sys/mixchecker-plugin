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
    check(!MixCheckerUpdate::isSemanticVersionGreater("2.1.0", "2.2.0"),
          "an installed build newer than website metadata stays up to date");
    check(!MixCheckerUpdate::isSemanticVersionGreater("9999999999.0.0", "2.2.0"),
          "overflowing version component is rejected");
    check(!MixCheckerUpdate::isSemanticVersionGreater(juce::String::fromUTF8("\xef\xbc\x92.2.0"), "2.1.0"),
          "Unicode digits cannot become an incorrect numeric version");
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
    auto newBinary = MixCheckerUpdate::evaluateUpdateJson(updateJson, "2.2.0", "");
    check(newBinary.valid && !newBinary.updateAvailable,
          "installed 2.2.0 must not be offered the site's older 1.0.1 version");
    auto newestInstalled = MixCheckerUpdate::evaluateUpdateJson(R"json({"latestVersion":"2.2.0","enabled":true})json",
                                                                 "2.2.0", "");
    check(newestInstalled.valid && !newestInstalled.updateAvailable,
          "new install does not show update available when metadata catches up");

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

void testInstalledVersionPersistence() {
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;
    const auto file = juce::File::getCurrentWorkingDirectory()
        .getNonexistentChildFile("mixchecker-version-test", ".settings", false);
    {
        juce::PropertiesFile settings(file, options);
        settings.setValue("installedPluginVersion", "2.1.0");
        settings.setValue("lastUpdateCheckTimestamp", 123456.0);
        settings.setValue("dismissedUpdateVersion", "2.2.0");
        check(settings.saveIfNeeded(), "pre-existing settings can be saved");
    }
    {
        PluginUpdateChecker checker("2.2.0", std::make_unique<juce::PropertiesFile>(file, options));
        check(checker.getSnapshot().lastCheckedAtMillis == 0,
              "installing a new binary invalidates the previous check timestamp");
    }
    {
        juce::PropertiesFile settings(file, options);
        check(settings.getValue("installedPluginVersion") == "2.2.0", "installed version persists");
        check(settings.getValue("dismissedUpdateVersion").isEmpty(), "old dismissal is cleared");
        settings.setValue("lastUpdateCheckTimestamp", 654321.0);
        check(settings.saveIfNeeded(), "new version's check timestamp can be saved");
    }
    {
        PluginUpdateChecker checker("2.2.0", std::make_unique<juce::PropertiesFile>(file, options));
        check(checker.getSnapshot().lastCheckedAtMillis == 654321,
              "reopening the same binary retains its check timestamp");
    }
    check(file.deleteFile(), "temporary settings removed");
}

} // namespace

int main() {
    testSemanticVersionComparison();
    testAutomaticCheckCooldown();
    testJsonEvaluation();
    testInstalledVersionPersistence();

    if (failures != 0) {
        std::cerr << failures << " update checker test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "All update checker tests passed\n";
    return EXIT_SUCCESS;
}
