#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_core/juce_core.h>

#pragma pack(push, 1)
struct LegacyAudioPacketHeader {
    uint8_t type = 0x04;
    uint8_t protocolVersion = 1;
    uint16_t sessionId;
    uint32_t sequenceNumber;
    uint32_t sampleRate;
    uint8_t channels;
    uint16_t frameCount;
    uint8_t format; // 0 = float32
    uint64_t timestamp;
};
#pragma pack(pop)

enum class AudioQualityMode : int {
    LowLatency = 0,
    Normal = 1,
    Lossless = 2
};

enum class AudioWireFormat : int {
    Float32Pcm = 0,
    Pcm16 = 1
};

enum class DebugTestSource : int {
    Off = 0,
    Silence = 1,
    Sine1k = 2,
    PinkNoise = 3,
    LeftOnly = 4,
    RightOnly = 5,
    StereoPhase = 6,
    FullScalePeak = 7
};

class MixCheckerProcessor : public juce::AudioProcessor {
public:
    MixCheckerProcessor();
    ~MixCheckerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Networking public access
    juce::String getLocalIpAddress() const;
    int getConnectionState() const; // 0=Waiting, 1=Connected, 2=Streaming
    void setStreaming(bool shouldStream);
    juce::String getQrPayload(); // refreshes the advertised IP (network may change after load)
    juce::String getPairingToken() const;
    uint16_t getSessionId() const;
    int getActiveQualityMode() const;

private:
    class NetworkThread : public juce::Thread {
    public:
        NetworkThread(MixCheckerProcessor& p);
        ~NetworkThread() override;
        void run() override;
        void triggerStop();
    private:
        MixCheckerProcessor& processor;
        
        void handleConnect(const juce::String& senderIp, int senderPort);
        void handleStartStream();
        void handleStopStream();
        void handleModeChange(const char* data, int size, const juce::String& senderIp, int senderPort);
        void handleDebugTestSourceChange(const char* data, int size, const juce::String& senderIp, int senderPort);
        void sendModeAck(const juce::String& targetIp, int targetPort, uint32_t requestId, bool accepted, AudioQualityMode mode, AudioWireFormat format, uint8_t reason);
    };

    juce::DatagramSocket socket{true};
    NetworkThread networkThread;

    std::atomic<int> connectionState{0}; // 0=Waiting, 1=Connected, 2=Streaming
    std::atomic<bool> isStreaming{false};

    juce::String clientIp;
    int clientPort = 0;
    double lastClientActivityMs = 0.0;
    juce::CriticalSection clientLock; // only used in network thread context!

    // localIp is written by the editor thread (QR refresh) and read by the network thread
    // (discovery replies); lastDiscoverySenderIp flows the opposite way. Both go under ipLock.
    juce::String localIp;
    juce::String lastDiscoverySenderIp;
    mutable juce::CriticalSection ipLock;
    double lastIpRefreshMs = 0.0;
    bool socketBound = false;
    juce::String pairingToken;

    // Lock-free ring buffer
    static constexpr int ringBufferSize = 192000;
    juce::AbstractFifo audioFifo{ringBufferSize};
    std::vector<float> audioData;

    std::atomic<uint32_t> sequenceNumber{0};
    std::atomic<uint16_t> currentSessionId{0};
    std::atomic<double> currentSampleRate{44100.0};
    std::atomic<int> currentChannels{2};
    std::atomic<int> currentBlockSize{0};
    std::atomic<uint64_t> samplePosition{0};
    std::atomic<int> activeQualityMode{static_cast<int>(AudioQualityMode::Normal)};
    std::atomic<int> activeWireFormat{static_cast<int>(AudioWireFormat::Float32Pcm)};
    std::atomic<bool> clientSupportsV2Audio{false};
    std::atomic<int> debugTestSource{static_cast<int>(DebugTestSource::Off)};

    uint32_t packetsSentSinceLog = 0;
    double lastDiagnosticsLogMs = 0.0;
    double debugSinePhase = 0.0;
    uint32_t debugNoiseState = 0x12345678;

    int getPacketFramesForActiveMode() const;
    float nextDebugSample(DebugTestSource source, int frame, int channel, int channels, double sampleRate);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixCheckerProcessor)
    
    friend class NetworkThread;
};
