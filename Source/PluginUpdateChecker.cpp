#include "PluginUpdateChecker.h"

namespace {

static constexpr const char* lastCheckKey = "lastUpdateCheckTimestamp";
static constexpr const char* dismissedVersionKey = "dismissedUpdateVersion";

bool parseSemanticVersion(const juce::String& version, int (&parts)[3]) {
    parts[0] = 0;
    parts[1] = 0;
    parts[2] = 0;

    auto tokens = juce::StringArray::fromTokens(version.trim(), ".", "");
    if (tokens.isEmpty() || tokens.size() > 3)
        return false;

    for (int i = 0; i < tokens.size(); ++i) {
        const auto token = tokens[i].trim();
        if (token.isEmpty())
            return false;

        for (auto character : token) {
            if (! juce::CharacterFunctions::isDigit(character))
                return false;
        }

        parts[i] = token.getIntValue();
        if (parts[i] < 0)
            return false;
    }

    return true;
}

juce::String valueAsString(const juce::DynamicObject& object, const juce::Identifier& key) {
    auto* property = object.getProperties().getVarPointer(key);
    if (property == nullptr || property->isVoid() || property->isUndefined())
        return {};

    return property->toString().trim();
}

bool valueAsBool(const juce::DynamicObject& object, const juce::Identifier& key, bool fallback) {
    auto* property = object.getProperties().getVarPointer(key);
    if (property == nullptr || property->isVoid() || property->isUndefined())
        return fallback;

    return static_cast<bool>(*property);
}

bool looksLikeWebUrl(const juce::String& url) {
    const auto trimmed = url.trim();
    return trimmed.startsWithIgnoreCase("https://") || trimmed.startsWithIgnoreCase("http://");
}

juce::String fallbackIfInvalidUrl(const juce::String& url, const juce::String& fallback) {
    const auto trimmed = url.trim();
    return looksLikeWebUrl(trimmed) ? trimmed : fallback;
}

juce::String defaultUpdateMessage(const juce::String& latestVersion) {
    return "Mix Checker " + latestVersion + " is available. Visit mixchecker.in to download.";
}

} // namespace

namespace MixCheckerUpdate {

bool isSemanticVersionGreater(const juce::String& latestVersion,
                              const juce::String& currentVersion) {
    int latest[3] = {};
    int current[3] = {};
    if (! parseSemanticVersion(latestVersion, latest) || ! parseSemanticVersion(currentVersion, current))
        return false;

    for (int i = 0; i < 3; ++i) {
        if (latest[i] > current[i])
            return true;
        if (latest[i] < current[i])
            return false;
    }

    return false;
}

bool isAutomaticCheckDue(juce::int64 lastCheckedAtMillis, juce::int64 nowMillis) {
    if (lastCheckedAtMillis <= 0)
        return true;

    return nowMillis - lastCheckedAtMillis >= updateCheckCooldownMs;
}

UpdateEvaluation evaluateUpdateJson(const juce::String& jsonText,
                                    const juce::String& currentVersion,
                                    const juce::String& dismissedVersion) {
    UpdateEvaluation evaluation;

    auto parsed = juce::JSON::parse(jsonText);
    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return evaluation;

    const bool enabled = valueAsBool(*object, "enabled", true);
    const auto latestVersion = valueAsString(*object, "latestVersion");
    if (latestVersion.isEmpty())
        return evaluation;

    int parsedLatest[3] = {};
    int parsedCurrent[3] = {};
    if (! parseSemanticVersion(latestVersion, parsedLatest) || ! parseSemanticVersion(currentVersion, parsedCurrent))
        return evaluation;

    evaluation.valid = true;
    evaluation.latestVersion = latestVersion;
    evaluation.minimumRecommendedVersion = valueAsString(*object, "minimumRecommendedVersion");
    evaluation.downloadUrl = fallbackIfInvalidUrl(valueAsString(*object, "downloadUrl"), defaultDownloadUrl);
    evaluation.releaseNotesUrl = fallbackIfInvalidUrl(valueAsString(*object, "releaseNotesUrl"), evaluation.downloadUrl);
    evaluation.message = valueAsString(*object, "message");
    if (evaluation.message.isEmpty())
        evaluation.message = defaultUpdateMessage(latestVersion);

    if (! enabled)
        return evaluation;

    if (latestVersion == dismissedVersion.trim())
        return evaluation;

    evaluation.updateAvailable = isSemanticVersionGreater(latestVersion, currentVersion);
    return evaluation;
}

} // namespace MixCheckerUpdate

PluginUpdateChecker::PluginUpdateChecker(juce::String currentPluginVersion)
    : Thread("MixCheckerPluginUpdateChecker"),
      currentVersion(std::move(currentPluginVersion)),
      properties(createPropertiesFile()) {
    if (properties != nullptr) {
        snapshot.lastCheckedAtMillis = static_cast<juce::int64>(properties->getDoubleValue(lastCheckKey, 0.0));
        dismissedUpdateVersion = properties->getValue(dismissedVersionKey).trim();
    }
}

PluginUpdateChecker::~PluginUpdateChecker() {
    signalThreadShouldExit();
    stopThread(3000);
}

void PluginUpdateChecker::startCheckIfDue(juce::int64 nowMillis) {
    {
        const juce::ScopedLock scoped(lock);
        if (snapshot.state == MixCheckerUpdate::UpdateState::Checking
            || ! MixCheckerUpdate::isAutomaticCheckDue(snapshot.lastCheckedAtMillis, nowMillis))
            return;
    }

    startCheck(nowMillis);
}

void PluginUpdateChecker::startManualCheck(juce::int64 nowMillis) {
    {
        const juce::ScopedLock scoped(lock);
        if (snapshot.state == MixCheckerUpdate::UpdateState::Checking)
            return;
    }

    startCheck(nowMillis);
}

void PluginUpdateChecker::dismissCurrentUpdate() {
    juce::String versionToDismiss;
    {
        const juce::ScopedLock scoped(lock);
        versionToDismiss = snapshot.latestVersion;
        snapshot.state = MixCheckerUpdate::UpdateState::UpToDate;
    }

    if (versionToDismiss.isNotEmpty())
        storeDismissedVersion(versionToDismiss);
}

PluginUpdateChecker::Snapshot PluginUpdateChecker::getSnapshot() const {
    const juce::ScopedLock scoped(lock);
    return snapshot;
}

void PluginUpdateChecker::run() {
    MixCheckerUpdate::UpdateEvaluation evaluation;

    if (! threadShouldExit()) {
        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(6000)
            .withNumRedirectsToFollow(3)
            .withStatusCode(&statusCode);

        if (auto stream = juce::URL(MixCheckerUpdate::versionMetadataUrl).createInputStream(options)) {
            const auto body = stream->readEntireStreamAsString();
            if (! threadShouldExit() && (statusCode == 0 || (statusCode >= 200 && statusCode < 300))) {
                juce::String dismissed;
                {
                    const juce::ScopedLock scoped(lock);
                    dismissed = dismissedUpdateVersion;
                }
                evaluation = MixCheckerUpdate::evaluateUpdateJson(body, currentVersion, dismissed);
            }
        }
    }

    {
        const juce::ScopedLock scoped(lock);
        if (threadShouldExit())
            return;

        if (! evaluation.valid) {
            snapshot.state = MixCheckerUpdate::UpdateState::Failed;
            return;
        }

        snapshot.latestVersion = evaluation.latestVersion;
        snapshot.downloadUrl = evaluation.downloadUrl;
        snapshot.releaseNotesUrl = evaluation.releaseNotesUrl;
        snapshot.message = evaluation.message;
        snapshot.state = evaluation.updateAvailable
            ? MixCheckerUpdate::UpdateState::UpdateAvailable
            : MixCheckerUpdate::UpdateState::UpToDate;
    }
}

void PluginUpdateChecker::startCheck(juce::int64 nowMillis) {
    {
        const juce::ScopedLock scoped(lock);
        snapshot.state = MixCheckerUpdate::UpdateState::Checking;
        snapshot.latestVersion.clear();
        snapshot.downloadUrl.clear();
        snapshot.releaseNotesUrl.clear();
        snapshot.message.clear();
        snapshot.lastCheckedAtMillis = nowMillis;
    }

    storeLastCheckedAt(nowMillis);
    startThread();
}

void PluginUpdateChecker::storeLastCheckedAt(juce::int64 nowMillis) {
    if (properties == nullptr)
        return;

    properties->setValue(lastCheckKey, static_cast<double>(nowMillis));
    properties->saveIfNeeded();
}

void PluginUpdateChecker::storeDismissedVersion(const juce::String& version) {
    {
        const juce::ScopedLock scoped(lock);
        dismissedUpdateVersion = version.trim();
    }

    if (properties == nullptr)
        return;

    properties->setValue(dismissedVersionKey, dismissedUpdateVersion);
    properties->saveIfNeeded();
}

std::unique_ptr<juce::PropertiesFile> PluginUpdateChecker::createPropertiesFile() {
    juce::PropertiesFile::Options options;
    options.applicationName = "MixCheckerPlugin";
    options.filenameSuffix = "settings";
    options.folderName = "MixChecker";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 0;

    return std::make_unique<juce::PropertiesFile>(options);
}
