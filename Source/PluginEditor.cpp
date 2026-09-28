#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/Style.h"
#include "UI/qrcodegen.hpp"

CherryButton::CherryButton(const juce::String& text, bool isGlass)
    : Button(text), glassMode(isGlass) {}

void CherryButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) {
    auto bounds = getLocalBounds().toFloat();
    float radius = 14.0f;
    
    if (glassMode) {
        Style::drawGlassPanel(g, bounds, radius, false);
        if (shouldDrawButtonAsDown)
            { g.setColour(juce::Colours::white.withAlpha(0.08f)); g.fillRoundedRectangle(bounds, radius); }
        else if (shouldDrawButtonAsHighlighted)
            { g.setColour(juce::Colours::white.withAlpha(0.04f)); g.fillRoundedRectangle(bounds, radius); }
        g.setColour(Style::fg);
    } else {
        juce::Colour topC = Style::cherryBright;
        juce::Colour midC = Style::cherry;
        juce::Colour botC = Style::cherryDark;
        
        if (shouldDrawButtonAsHighlighted) {
            topC = topC.brighter(0.1f);
            midC = midC.brighter(0.1f);
            botC = botC.brighter(0.1f);
        }
        
        juce::ColourGradient grad(topC, 0.0f, 0.0f, botC, 0.0f, bounds.getHeight(), false);
        grad.addColour(0.45, midC);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(Style::fg);
    }
    
    g.setFont(14.0f);
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, true);
}


SegmentedControl::SegmentedControl(const juce::String& labelText, const juce::StringArray& options) 
    : label(labelText), items(options) 
{
    if (labelText == "Latency" || labelText == "Quality") selectedIndex = 1;
    if (labelText == "Mode") selectedIndex = 0;
}

void SegmentedControl::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    Style::drawGlassPanel(g, bounds, 14.0f, false);
    
    g.setColour(Style::fgMuted);
    g.setFont(12.0f);
    g.drawText(label, 12, 0, 60, (int)bounds.getHeight(), juce::Justification::centredLeft, true);
    
    int numItems = items.size();
    if (numItems == 0) return;
    
    float startX = 70.0f;
    float segWidth = (bounds.getWidth() - startX - 8.0f) / numItems;
    float h = bounds.getHeight() - 16.0f;
    float y = 8.0f;
    
    for (int i = 0; i < numItems; ++i) {
        juce::Rectangle<float> segBounds(startX + i * segWidth, y, segWidth, h);
        
        if (i == selectedIndex) {
            g.setColour(Style::cherry);
            g.fillRoundedRectangle(segBounds, 10.0f);
            g.setColour(Style::fg);
        } else {
            g.setColour(Style::fgMuted);
        }
        
        g.setFont(12.0f);
        g.drawText(items[i], segBounds.toNearestInt(), juce::Justification::centred, true);
    }
}

void SegmentedControl::mouseDown(const juce::MouseEvent& e) {
    auto bounds = getLocalBounds().toFloat();
    int numItems = items.size();
    float startX = 70.0f;
    float segWidth = (bounds.getWidth() - startX - 8.0f) / numItems;
    
    if (e.x >= startX && e.x <= bounds.getWidth() - 8.0f) {
        int index = (int)((e.x - startX) / segWidth);
        if (index >= 0 && index < numItems && index != selectedIndex) {
            selectedIndex = index;
            repaint();
            if (onChange) onChange(index);
        }
    }
}


void MixCheckerEditor::regenerateQrImage() {
    auto payload = audioProcessor.getQrPayload();
    if (payload.isEmpty()) {
        qrImage = {};
        lastQrPayload.clear();
        return;
    }
    if (payload == lastQrPayload && qrImage.isValid())
        return;
    lastQrPayload = payload;
    
    using namespace qrcodegen;
    QrCode qr = QrCode::encodeText(payload.toRawUTF8(), QrCode::Ecc::MEDIUM);
    
    int qrSize = qr.getSize();
    int margin = 2;
    int totalModules = qrSize + margin * 2;
    int pixelSize = 4; // each module = 4x4 pixels for crispness
    int imgSize = totalModules * pixelSize;
    
    qrImage = juce::Image(juce::Image::ARGB, imgSize, imgSize, true);
    juce::Graphics ig(qrImage);
    
    // White background
    ig.setColour(juce::Colours::white);
    ig.fillAll();
    
    // Black modules
    ig.setColour(juce::Colours::black);
    for (int y = 0; y < qrSize; ++y) {
        for (int x = 0; x < qrSize; ++x) {
            if (qr.getModule(x, y)) {
                ig.fillRect((x + margin) * pixelSize, (y + margin) * pixelSize, pixelSize, pixelSize);
            }
        }
    }
}


MixCheckerEditor::MixCheckerEditor(MixCheckerProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      updateChecker(JucePlugin_VersionString)
{
    setSize(520, 480);
    editorOpenedAtMs = juce::Time::getMillisecondCounter();
    addAndMakeVisible(networkBtn);
    addAndMakeVisible(networkHelpBtn);
    networkBtn.onClick = [this] {
        const auto addresses = audioProcessor.getAvailableNetworkAddresses();
        juce::PopupMenu menu;
        menu.addItem(1, "Automatic address");
        for (int i = 0; i < addresses.size(); ++i) menu.addItem(i + 2, addresses[i]);
                juce::Component::SafePointer<MixCheckerEditor> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&networkBtn),
                        [safe, addresses](int choice) mutable {
                if (safe == nullptr || choice == 0) return;
                safe->audioProcessor.selectNetworkAddress(choice == 1 ? juce::String() : addresses[choice - 2]);
                safe->regenerateQrImage();
                safe->repaint();
            });
    };
    networkHelpBtn.onClick = [this] {
        auto problem = audioProcessor.getNetworkProblem();
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "MixChecker connection help",
            problem + "\nPC address: " + audioProcessor.getLocalIpAddress()
            + "\nUDP port: 49320\n\nChoose your real Wi-Fi/Ethernet IPv4 under Network / IP if a VPN or virtual adapter was selected. Scan the current QR after changing it."
              "\n\nKeep PC and phone on a reachable trusted LAN. Try QR/manual IP if Nearby discovery is filtered. Guest Wi-Fi/client isolation must be changed on the router or avoided using your own hotspot."
              "\n\nOn Windows, run the installer firewall repair for the actual DAW/bridge EXE on a trusted Private network. Never disable your firewall. Use only one MixChecker instance.");
    };
    
    addAndMakeVisible(latencySelector);
    addAndMakeVisible(modeSelector);
    
    addAndMakeVisible(playBtn);
    playBtn.onClick = [this] {
        if (audioProcessor.getConnectionState() >= 1) {
            bool isStreaming = (audioProcessor.getConnectionState() == 2);
            audioProcessor.setStreaming(!isStreaming);
            playBtn.setButtonText(!isStreaming ? "Stop" : "Start");
        }
    };

    addAndMakeVisible(checkUpdateBtn);
    checkUpdateBtn.onClick = [this] {
        updateChecker.startManualCheck();
        syncUpdateBannerControls();
        repaint();
    };

    addAndMakeVisible(updateOpenWebsiteBtn);
    addAndMakeVisible(updateDismissBtn);
    updateOpenWebsiteBtn.setVisible(false);
    updateDismissBtn.setVisible(false);

    updateOpenWebsiteBtn.onClick = [this] {
        const auto snapshot = updateChecker.getSnapshot();
        const auto url = snapshot.downloadUrl.isNotEmpty()
            ? snapshot.downloadUrl
            : juce::String(MixCheckerUpdate::defaultDownloadUrl);
        juce::URL(url).launchInDefaultBrowser();
    };

    updateDismissBtn.onClick = [this] {
        updateChecker.dismissCurrentUpdate();
        syncUpdateBannerControls();
        resized();
        repaint();
    };
    
    regenerateQrImage();
    startTimerHz(30);
}

MixCheckerEditor::~MixCheckerEditor() {}

void MixCheckerEditor::paint(juce::Graphics& g) {
    // 1. Background
    juce::ColourGradient bgGradient(Style::bgCherryBlack, getWidth() / 2.0f, 0.0f,
                                    Style::bgNearBlack, getWidth() / 2.0f, getHeight() * 1.2f, true);
    g.setGradientFill(bgGradient);
    g.fillAll();
    
    // 2. Header
    g.setColour(Style::fg);
    g.setFont(18.0f);
    g.drawText("Mix Checker  v" + juce::String(JucePlugin_VersionString), 20, 15, 260, 24,
               juce::Justification::centredLeft, true);
    g.setColour(Style::fgMuted);
    g.setFont(12.0f);
    g.drawText("Real phone monitoring", 20, 35, 200, 14, juce::Justification::centredLeft, true);
    
    int state = audioProcessor.getConnectionState();
    juce::String stateText = "Waiting for phone";
    juce::Colour stateColor = Style::warning;
    if (state == 1) { stateText = "Phone connected"; stateColor = Style::success; }
    else if (state == 2) { stateText = "Streaming"; stateColor = Style::cherryBright; }

    // 3. Status Chip
    juce::Rectangle<float> statusBounds(getWidth() - 155.0f, 20.0f, 135.0f, 24.0f);
    Style::drawGlassPanel(g, statusBounds, 12.0f, false);
    g.setColour(stateColor);
    g.fillEllipse(statusBounds.getX() + 8, statusBounds.getCentreY() - 3, 6.0f, 6.0f);
    g.setColour(Style::fg);
    g.setFont(12.0f);
    g.drawText(stateText, statusBounds.withLeft(statusBounds.getX() + 20), juce::Justification::centredLeft, true);

    // 4. Main Glass Card
    juce::Rectangle<float> cardBounds(20.0f, 60.0f, getWidth() - 40.0f, 190.0f);
    Style::drawGlassPanel(g, cardBounds, 20.0f, true);
    
    // 5. QR Code - real rendered image
    float qrAreaX = cardBounds.getX() + 20;
    float qrAreaY = cardBounds.getY() + 20;
    float qrAreaSize = 150.0f;
    
    // White rounded container for QR
    g.setColour(juce::Colours::white);
    g.fillRoundedRectangle(qrAreaX, qrAreaY, qrAreaSize, qrAreaSize, 16.0f);
    
    // Draw the actual QR image inside with padding
    if (qrImage.isValid()) {
        float padding = 8.0f;
        juce::Rectangle<float> qrDrawArea(qrAreaX + padding, qrAreaY + padding,
                                           qrAreaSize - padding * 2, qrAreaSize - padding * 2);
        g.drawImage(qrImage, qrDrawArea, juce::RectanglePlacement::centred);
    } else {
        g.setColour(juce::Colours::black);
        g.setFont(14.0f);
        g.drawFittedText("Connection unavailable\nOpen Connection help", juce::Rectangle<int>(
            (int)qrAreaX + 10, (int)qrAreaY + 10, 130, 130), juce::Justification::centred, 5);
    }

    // 6. Pairing Details text
    float detailsX = cardBounds.getX() + 190;
    g.setColour(Style::fgMuted);
    g.setFont(12.0f);
    g.drawText("Scan in the Mix Checker app", detailsX, cardBounds.getY() + 20, 260, 16, juce::Justification::centredLeft, true);
    
    g.setColour(Style::fg);
    g.setFont(16.0f);
    g.drawText("Pairing details", detailsX, cardBounds.getY() + 40, 260, 20, juce::Justification::centredLeft, true);

    // Rows
    auto drawRow = [&](float y, const juce::String& label, const juce::String& val) {
        juce::Rectangle<float> row(detailsX, cardBounds.getY() + y, 260.0f, 28.0f);
        g.setColour(juce::Colours::black.withAlpha(0.2f));
        g.fillRoundedRectangle(row, 10.0f);
        g.setColour(Style::fgMuted);
        g.setFont(12.0f);
        g.drawText(label, row.withTrimmedLeft(10), juce::Justification::centredLeft, true);
        g.setColour(Style::fg);
        g.setFont(13.0f);
        g.drawText(val, row.withTrimmedRight(10), juce::Justification::centredRight, true);
    };
    
    drawRow(70, "IP", audioProcessor.getLocalIpAddress());
    drawRow(105, "Port", "49320");
    drawRow(140, "Code", audioProcessor.getPairingToken());

    // Chain-placement guidance: Mix Checker analyzes exactly the audio it receives, so it must
    // sit last on the master to hear the full processed mix (a pre-EQ slot shows uncut lows).
    g.setColour(Style::fgMuted);
    g.setFont(11.0f);
    g.drawText("Place last on the master chain to hear your full mix.",
               detailsX, cardBounds.getY() + 176, 260, 30,
               juce::Justification::topLeft, true);

    // 7. Level meter
    juce::Rectangle<float> meterBounds(playBtn.getRight() + 12.0f, (float)playBtn.getY(),
                                        getWidth() - playBtn.getRight() - 32.0f, 40.0f);
    Style::drawGlassPanel(g, meterBounds, 14.0f, false);
    
    if (state == 2) {
        int bars = 28;
        float barWidth = 3.0f;
        float gap = (meterBounds.getWidth() - 20 - (bars * barWidth)) / (bars - 1);
        for (int i = 0; i < bars; ++i) {
            float intensity = (float)i / bars;
            juce::Colour c = intensity > 0.8f ? Style::cherryBright : (intensity > 0.55f ? Style::warning : Style::success);
            g.setColour(c);
            float h = 12.0f + std::abs(std::sin((meterPhase + i * 5) * 0.1f)) * 16.0f;
            g.fillRoundedRectangle(meterBounds.getX() + 10 + i * (barWidth + gap), meterBounds.getCentreY() - h / 2.0f, barWidth, h, 1.5f);
        }
    }

    const auto updateSnapshot = updateChecker.getSnapshot();
    if (updateSnapshot.state == MixCheckerUpdate::UpdateState::UpdateAvailable) {
        const auto bannerBounds = getUpdateBannerBounds().toFloat();
        Style::drawGlassPanel(g, bannerBounds, 16.0f, true);

        g.setColour(Style::cherryBright.withAlpha(0.10f));
        g.fillRoundedRectangle(bannerBounds.reduced(1.0f), 16.0f);

        const auto textArea = getUpdateBannerBounds().withTrimmedLeft(14).withTrimmedRight(178);
        g.setColour(Style::fg);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText("New version available", textArea.withHeight(18), juce::Justification::centredLeft, true);

        g.setColour(Style::fgSecondary);
        g.setFont(11.0f);
        const auto body = "Mix Checker " + updateSnapshot.latestVersion
            + " is available. Visit mixchecker.in to download.";
        g.drawFittedText(body, textArea.withTrimmedTop(18), juce::Justification::centredLeft, 1);
    }
}

void MixCheckerEditor::resized() {
    networkBtn.setBounds(20, 438, 220, 28);
    networkHelpBtn.setBounds(255, 438, 245, 28);
    latencySelector.setBounds(20, 260, getWidth() / 2 - 26, 60);
    modeSelector.setBounds(getWidth() / 2 + 6, 260, getWidth() / 2 - 26, 60);
    const bool updateVisible = isUpdateBannerVisible();
    playBtn.setBounds(20, updateVisible ? 326 : 340, 120, 40);
    checkUpdateBtn.setBounds(20, updateVisible ? 388 : 388, 120, 22);

    const auto bannerBounds = getUpdateBannerBounds();
    updateDismissBtn.setBounds(bannerBounds.getRight() - 72, bannerBounds.getY() + 6, 58, 24);
    updateOpenWebsiteBtn.setBounds(updateDismissBtn.getX() - 102, bannerBounds.getY() + 6, 92, 24);
    syncUpdateBannerControls();
}

void MixCheckerEditor::timerCallback() {
    if (! updateCheckRequested
        && juce::Time::getMillisecondCounter() - editorOpenedAtMs >= 7000) {
        updateCheckRequested = true;
        updateChecker.startCheckIfDue();
    }

    if (audioProcessor.getConnectionState() == 2) {
        meterPhase++;
        playBtn.setButtonText("Stop");
    } else {
        playBtn.setButtonText("Start");
    }
    
    // Regenerate QR if payload changed (e.g. IP change)
    regenerateQrImage();

    const bool updateVisible = isUpdateBannerVisible();
    if (updateVisible != updateBannerWasVisible) {
        updateBannerWasVisible = updateVisible;
        resized();
    } else {
        syncUpdateBannerControls();
    }

    repaint();
}

bool MixCheckerEditor::isUpdateBannerVisible() const {
    return updateChecker.getSnapshot().state == MixCheckerUpdate::UpdateState::UpdateAvailable;
}

juce::Rectangle<int> MixCheckerEditor::getUpdateBannerBounds() const {
    return { 20, 372, getWidth() - 40, 36 }; // Keep the new network controls unobstructed.
}

void MixCheckerEditor::syncUpdateBannerControls() {
    const bool visible = isUpdateBannerVisible();
    const auto snapshot = updateChecker.getSnapshot();
    checkUpdateBtn.setVisible(! visible);
    checkUpdateBtn.setButtonText(snapshot.state == MixCheckerUpdate::UpdateState::Checking
        ? "Checking..."
        : "Check Update");
    updateOpenWebsiteBtn.setVisible(visible);
    updateDismissBtn.setVisible(visible);
}
