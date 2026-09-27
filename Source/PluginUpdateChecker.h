#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace MixCheckerUpdate {

static constexpr const char* versionMetadataUrl = "https://mixchecker.in/plugin-version.json";
static constexpr const char* defaultDownloadUrl = "https://mixchecker.in";
static constexpr juce::int64 updateCheckCooldownMs = 24LL * 60LL * 60LL * 1000LL;

enum class UpdateState {
    Idle,
    Checking,
    UpdateAvailable,
    UpToDate,
    Failed
};

struct UpdateEvaluation {
    bool valid = false;
    bool updateAvailable = false;
    juce::String latestVersion;
    juce::String minimumRecommendedVersion;
    juce::String downloadUrl;
    juce::String releaseNotesUrl;
    juce::String message;
};

bool isSemanticVersionGreater(const juce::String& latestVersion,
                              const juce::String& currentVersion);

bool isAutomaticCheckDue(juce::int64 lastCheckedAtMillis, juce::int64 nowMillis);

UpdateEvaluation evaluateUpdateJson(const juce::String& jsonText,
                                    const juce::String& currentVersion,
                                    const juce::String& dismissedVersion);

} // namespace MixCheckerUpdate

class PluginUpdateChecker : private juce::Thread {
public:
    struct Snapshot {
        MixCheckerUpdate::UpdateState state = MixCheckerUpdate::UpdateState::Idle;
        juce::String latestVersion;
        juce::String downloadUrl;
        juce::String releaseNotesUrl;
        juce::String message;
        juce::int64 lastCheckedAtMillis = 0;
    };

    explicit PluginUpdateChecker(juce::String currentPluginVersion);
    ~PluginUpdateChecker() override;

    void startCheckIfDue(juce::int64 nowMillis = juce::Time::currentTimeMillis());
    void startManualCheck(juce::int64 nowMillis = juce::Time::currentTimeMillis());
    void dismissCurrentUpdate();
    Snapshot getSnapshot() const;

private:
    void run() override;

    void startCheck(juce::int64 nowMillis);
    void storeLastCheckedAt(juce::int64 nowMillis);
    void storeDismissedVersion(const juce::String& version);
    static std::unique_ptr<juce::PropertiesFile> createPropertiesFile();

    const juce::String currentVersion;
    std::unique_ptr<juce::PropertiesFile> properties;

    mutable juce::CriticalSection lock;
    Snapshot snapshot;
    juce::String dismissedUpdateVersion;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginUpdateChecker)
};
