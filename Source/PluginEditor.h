#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>
#include "PluginProcessor.h"
#include "PluginUpdateChecker.h"

class CherryButton : public juce::Button {
public:
    CherryButton(const juce::String& text, bool isGlass = false);
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
private:
    bool glassMode;
};

class SegmentedControl : public juce::Component {
public:
    SegmentedControl(const juce::String& labelText, const juce::StringArray& options);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    
    std::function<void(int)> onChange;
    int selectedIndex = 0;
private:
    juce::String label;
    juce::StringArray items;
};

class MixCheckerEditor : public juce::AudioProcessorEditor, public juce::Timer {
public:
    MixCheckerEditor(MixCheckerProcessor&);
    ~MixCheckerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    MixCheckerProcessor& audioProcessor;

    SegmentedControl latencySelector{"Quality", {"Low", "Normal", "Lossless"}};
    SegmentedControl modeSelector{"Mode", {"Stereo", "Mono"}};

    CherryButton playBtn{"Start", false};
    CherryButton checkUpdateBtn{"Check Update", true};
    CherryButton networkBtn{"Network / IP", true};
    CherryButton networkHelpBtn{"Connection help", true};
    CherryButton updateOpenWebsiteBtn{"Open Website", true};
    CherryButton updateDismissBtn{"Dismiss", true};
    PluginUpdateChecker updateChecker;
    
    int meterPhase = 0;
    juce::uint32 editorOpenedAtMs = 0;
    bool updateCheckRequested = false;
    bool updateBannerWasVisible = false;

    // QR code cached image
    juce::Image qrImage;
    juce::String lastQrPayload;
    void regenerateQrImage();
    bool isUpdateBannerVisible() const;
    juce::Rectangle<int> getUpdateBannerBounds() const;
    void syncUpdateBannerControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixCheckerEditor)
};
