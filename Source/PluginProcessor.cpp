#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#endif
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {
static constexpr uint32_t audioV2Magic = 0x5541434D; // "MCAU" in little-endian packets
static constexpr uint8_t audioPacketType = 0x04;
static constexpr uint8_t modeChangeType = 0x0A;
static constexpr uint8_t modeAckType = 0x0B;
static constexpr uint8_t debugTestSourceChangeType = 0x0C;
static constexpr uint8_t protocolVersion2 = 0x02;
static constexpr int audioV2HeaderBytes = 64;
static constexpr uint8_t flagCrcPresent = 0x01;

static uint16_t readUInt16LE(const char* data) {
    return static_cast<uint16_t>(static_cast<unsigned char>(data[0]))
         | static_cast<uint16_t>(static_cast<unsigned char>(data[1])) << 8;
}

static uint32_t readUInt32LE(const char* data) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[0]))
         | static_cast<uint32_t>(static_cast<unsigned char>(data[1])) << 8
         | static_cast<uint32_t>(static_cast<unsigned char>(data[2])) << 16
         | static_cast<uint32_t>(static_cast<unsigned char>(data[3])) << 24;
}

static void appendUInt16LE(std::vector<char>& out, uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

static void appendUInt32LE(std::vector<char>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
}

static void appendUInt64LE(std::vector<char>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
}

static void appendLengthPrefixedText(std::vector<char>& out, const juce::String& text, int maxBytes = 120) {
    auto utf8 = text.toRawUTF8();
    const int length = juce::jmin(maxBytes, static_cast<int>(std::strlen(utf8)));
    out.push_back(static_cast<char>(length & 0xFF));
    out.insert(out.end(), utf8, utf8 + length);
}

static uint32_t crc32Bytes(const char* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint8_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int packetFramesForMode(AudioQualityMode mode) {
    switch (mode) {
        case AudioQualityMode::LowLatency: return 128;
        case AudioQualityMode::Lossless: return 256;
        case AudioQualityMode::Normal:
        default: return 256;
    }
}

static AudioQualityMode qualityModeFromWire(int value) {
    if (value == static_cast<int>(AudioQualityMode::LowLatency)) return AudioQualityMode::LowLatency;
    if (value == static_cast<int>(AudioQualityMode::Lossless)) return AudioQualityMode::Lossless;
    return AudioQualityMode::Normal;
}

static bool isValidQualityMode(int value) {
    return value == static_cast<int>(AudioQualityMode::LowLatency)
        || value == static_cast<int>(AudioQualityMode::Normal)
        || value == static_cast<int>(AudioQualityMode::Lossless);
}

static juce::String qualityModeName(AudioQualityMode mode) {
    switch (mode) {
        case AudioQualityMode::LowLatency: return "Low";
        case AudioQualityMode::Lossless: return "Lossless";
        case AudioQualityMode::Normal:
        default: return "Normal";
    }
}

static std::vector<char> buildLegacyAudioPacket(uint16_t sessionId,
                                                uint32_t sequenceNumber,
                                                uint32_t sampleRate,
                                                uint8_t channels,
                                                uint16_t frameCount,
                                                uint64_t samplePosition,
                                                const std::vector<float>& payloadSamples) {
    LegacyAudioPacketHeader header;
    header.type = audioPacketType;
    header.protocolVersion = 1;
    header.sessionId = sessionId;
    header.sequenceNumber = sequenceNumber;
    header.sampleRate = sampleRate;
    header.channels = channels;
    header.frameCount = frameCount;
    header.format = static_cast<uint8_t>(AudioWireFormat::Float32Pcm);
    header.timestamp = samplePosition;

    std::vector<char> packet;
    packet.resize(sizeof(LegacyAudioPacketHeader) + payloadSamples.size() * sizeof(float));
    std::memcpy(packet.data(), &header, sizeof(LegacyAudioPacketHeader));
    std::memcpy(packet.data() + sizeof(LegacyAudioPacketHeader), payloadSamples.data(), payloadSamples.size() * sizeof(float));
    return packet;
}

static std::vector<char> buildV2AudioPacket(uint16_t sessionId,
                                            uint32_t sequenceNumber,
                                            uint32_t sampleRate,
                                            uint16_t channels,
                                            uint32_t frameCount,
                                            uint64_t samplePosition,
                                            AudioQualityMode mode,
                                            const std::vector<float>& payloadSamples) {
    std::vector<char> payload;
    payload.resize(payloadSamples.size() * sizeof(float));
    std::memcpy(payload.data(), payloadSamples.data(), payload.size());

    const uint32_t payloadCrc = crc32Bytes(payload.data(), payload.size());
    std::vector<char> packet;
    packet.reserve(audioV2HeaderBytes + payload.size());

    appendUInt32LE(packet, audioV2Magic);
    packet.push_back(static_cast<char>(audioPacketType));
    packet.push_back(static_cast<char>(protocolVersion2));
    packet.push_back(static_cast<char>(audioV2HeaderBytes));
    packet.push_back(static_cast<char>(flagCrcPresent));
    appendUInt16LE(packet, sessionId);
    appendUInt16LE(packet, static_cast<uint16_t>(AudioWireFormat::Float32Pcm));
    appendUInt16LE(packet, static_cast<uint16_t>(mode));
    appendUInt16LE(packet, channels);
    appendUInt32LE(packet, sequenceNumber);
    appendUInt32LE(packet, sampleRate);
    appendUInt32LE(packet, frameCount);
    appendUInt64LE(packet, samplePosition);
    appendUInt32LE(packet, static_cast<uint32_t>(payload.size()));
    appendUInt32LE(packet, payloadCrc);

    while (packet.size() < audioV2HeaderBytes)
        appendUInt32LE(packet, 0);

    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

static juce::String discoveryStateText(int state) {
    if (state == 2)
        return "Streaming";
    if (state == 1)
        return "Ready";
    return "Waiting";
}

static std::vector<char> buildDiscoveryResponse(const juce::String& pluginIp,
                                                uint16_t sessionId,
                                                const juce::String& token,
                                                int connectionState) {
    std::vector<char> response;
    response.reserve(192);
    response.push_back(0x09);
    response.push_back(0x01);
    appendUInt16LE(response, sessionId);
    appendUInt16LE(response, 49320);
    appendLengthPrefixedText(response, token, 64);
    appendLengthPrefixedText(response, discoveryStateText(connectionState));
    appendUInt64LE(response, static_cast<uint64_t>(juce::Time::currentTimeMillis()));

    auto computerName = juce::SystemStats::getComputerName();
    auto displayName = computerName.isNotEmpty()
        ? "Mix Checker - " + computerName
        : "Mix Checker";
    appendLengthPrefixedText(response, displayName);
    appendLengthPrefixedText(response, pluginIp);
    return response;
}

static int scoreLocalIpv4(const juce::String& ip) {
    if (ip.isEmpty() || ip == "0.0.0.0" || ip.startsWith("127.") || ip.containsChar(':'))
        return -1000;
    if (ip.startsWith("169.254."))
        return -200;
    if (ip.startsWith("192.168.43.") || ip.startsWith("192.168.137.") || ip.startsWith("172.20.10."))
        return 700;
    if (ip.startsWith("192.168."))
        return 600;
    if (ip.startsWith("172.")) {
        auto parts = juce::StringArray::fromTokens(ip, ".", "");
        if (parts.size() >= 2) {
            int second = parts[1].getIntValue();
            if (second >= 16 && second <= 31)
                return 580;
        }
    }
    if (ip.startsWith("10."))
        return 560;
    if (ip.startsWith("100.64."))
        return 420;
    return 100;
}

static bool sameSlash24(const juce::String& a, const juce::String& b) {
    if (a.isEmpty() || b.isEmpty())
        return false;
    auto pa = juce::StringArray::fromTokens(a, ".", "");
    auto pb = juce::StringArray::fromTokens(b, ".", "");
    return pa.size() == 4 && pb.size() == 4
        && pa[0] == pb[0] && pa[1] == pb[1] && pa[2] == pb[2];
}

// UDP connect selects a route without sending application data. Prefer Windows'
// actual return interface to the phone over address-prefix guesses on multi-NIC PCs.
static juce::String routedLocalAddress(const juce::String& peer) {
   #if JUCE_WINDOWS
    if (peer.isEmpty()) return {};
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(49320);
    remote.sin_addr.s_addr = inet_addr(peer.toRawUTF8());
    if (remote.sin_addr.s_addr == INADDR_NONE) return {};
    const auto probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET) return {};
    juce::String result;
    if (::connect(probe, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
        sockaddr_in local{};
        int size = sizeof(local);
        if (::getsockname(probe, reinterpret_cast<sockaddr*>(&local), &size) == 0) {
            const auto host = ntohl(local.sin_addr.s_addr);
            result = juce::String((int)(host >> 24)) + "." + juce::String((int)((host >> 16) & 255))
                + "." + juce::String((int)((host >> 8) & 255)) + "." + juce::String((int)(host & 255));
        }
    }
    ::closesocket(probe);
    return result;
   #else
    juce::ignoreUnused(peer);
    return {};
   #endif
}

// preferSubnetOf: the source IP of the most recent discovery request from the app. The phone has
// told us which subnet it lives on, and an adapter on that subnet beats every static heuristic
// (multi-NIC PCs with VM/hotspot adapters otherwise advertise an IP the phone can't reach).
// previousSelection: pass the current advertised IP so the log fires only when the choice changes
// (this now runs periodically while the editor is open, not just once at load).
static juce::String chooseBestLocalIpv4(const juce::String& preferSubnetOf,
                                        const juce::String& previousSelection = {}) {
    auto addresses = juce::IPAddress::getAllAddresses(false);
    const auto routedIp = routedLocalAddress(preferSubnetOf);
    juce::String bestIp;
    int bestScore = -1001;

    juce::StringArray candidates;
    for (auto& addr : addresses) {
        auto ip = addr.toString();
        int score = scoreLocalIpv4(ip);
        if (score > -200 && sameSlash24(ip, preferSubnetOf))
            score += 1000;
        if (score >= 0 && ip == routedIp) score += 2000;
        candidates.add(ip + " score=" + juce::String(score));
        if (score > bestScore) {
            bestScore = score;
            bestIp = ip;
        }
    }

    if (bestScore < 0 || bestIp.isEmpty())
        bestIp = juce::IPAddress::getLocalAddress(true).toString();

    if (bestIp != previousSelection) {
        juce::Logger::writeToLog("MixChecker local IPv4 candidates: " + candidates.joinIntoString(", ")
                               + " | selected=" + bestIp
                               + (preferSubnetOf.isNotEmpty() ? " | phoneSubnetHint=" + preferSubnetOf : juce::String())
                               + " | pairing help: select the LAN IP; allow the actual DAW host on a trusted Private network.");
    }
    return bestIp;
}

}

MixCheckerProcessor::NetworkThread::NetworkThread(MixCheckerProcessor& p) 
    : Thread("MixCheckerNetworkThread"), processor(p) {}

MixCheckerProcessor::NetworkThread::~NetworkThread() {
    stopThread(2000);
}

void MixCheckerProcessor::NetworkThread::triggerStop() {
    signalThreadShouldExit();
}

void MixCheckerProcessor::NetworkThread::sendModeAck(const juce::String& targetIp,
                                                     int targetPort,
                                                     uint32_t requestId,
                                                     bool accepted,
                                                     AudioQualityMode mode,
                                                     AudioWireFormat format,
                                                     uint8_t reason) {
    if (targetIp.isEmpty() || targetPort <= 0)
        return;

    std::vector<char> ack;
    ack.reserve(17);
    ack.push_back(static_cast<char>(modeAckType));
    ack.push_back(static_cast<char>(protocolVersion2));
    appendUInt16LE(ack, processor.currentSessionId.load());
    appendUInt32LE(ack, requestId);
    ack.push_back(static_cast<char>(accepted ? 1 : 0));
    ack.push_back(static_cast<char>(mode));
    appendUInt32LE(ack, static_cast<uint32_t>(processor.currentSampleRate.load()));
    ack.push_back(static_cast<char>(juce::jlimit(1, 2, processor.currentChannels.load())));
    ack.push_back(static_cast<char>(format));
    ack.push_back(static_cast<char>(reason));
    processor.socket.write(targetIp, targetPort, ack.data(), static_cast<int>(ack.size()));
}

void MixCheckerProcessor::NetworkThread::handleModeChange(const char* data, int size, const juce::String& senderIp, int senderPort) {
    if (size < 11 || static_cast<uint8_t>(data[1]) != protocolVersion2)
        return;

    const uint16_t reqSession = readUInt16LE(data + 2);
    const uint32_t requestId = readUInt32LE(data + 4);
    const int requestedMode = static_cast<uint8_t>(data[8]);
    const int requestedFormat = static_cast<uint8_t>(data[9]);
    const int tokenLength = static_cast<uint8_t>(data[10]);

    if (tokenLength > 64 || size < 11 + tokenLength)
        return;

    const juce::String reqToken(data + 11, tokenLength);
    bool accepted = false;
    uint8_t reason = 5; // NOT_CONNECTED
    auto activeMode = qualityModeFromWire(processor.activeQualityMode.load());
    auto activeFormat = AudioWireFormat::Float32Pcm;

    {
        juce::ScopedLock sl(processor.clientLock);
        const bool senderMatches = senderIp == processor.clientIp && senderPort == processor.clientPort;
        if (!senderMatches) {
            reason = 5;
        } else if (!isValidQualityMode(requestedMode)) {
            reason = 3;
        } else if (requestedFormat != static_cast<int>(AudioWireFormat::Float32Pcm)) {
            reason = 4;
        } else if (reqSession == 0 && reqToken.isEmpty()) {
            const int previousMode = processor.activeQualityMode.load();
            processor.activeQualityMode = requestedMode;
            processor.activeWireFormat = requestedFormat;
            processor.clientSupportsV2Audio = true;
            processor.lastClientActivityMs = juce::Time::getMillisecondCounterHiRes();
            activeMode = qualityModeFromWire(requestedMode);
            activeFormat = AudioWireFormat::Float32Pcm;
            accepted = true;
            reason = 0;

            if (previousMode != requestedMode) {
                processor.audioFifo.reset();
                processor.sequenceNumber = 0;
            }
        } else if (reqSession != processor.currentSessionId.load()) {
            reason = 1;
        } else if (reqToken != processor.pairingToken) {
            reason = 2;
        } else {
            const int previousMode = processor.activeQualityMode.load();
            processor.activeQualityMode = requestedMode;
            processor.activeWireFormat = requestedFormat;
            processor.clientSupportsV2Audio = true;
            processor.lastClientActivityMs = juce::Time::getMillisecondCounterHiRes();
            activeMode = qualityModeFromWire(requestedMode);
            activeFormat = AudioWireFormat::Float32Pcm;
            accepted = true;
            reason = 0;

            if (previousMode != requestedMode) {
                processor.audioFifo.reset();
                processor.sequenceNumber = 0;
            }
        }
    }

    sendModeAck(senderIp, senderPort, requestId, accepted, activeMode, activeFormat, reason);
}

void MixCheckerProcessor::NetworkThread::handleDebugTestSourceChange(const char* data, int size, const juce::String& senderIp, int senderPort) {
#if JUCE_DEBUG
    if (size < 6 || static_cast<uint8_t>(data[1]) != protocolVersion2)
        return;

    const uint16_t reqSession = readUInt16LE(data + 2);
    const int source = static_cast<uint8_t>(data[4]);
    const int tokenLength = static_cast<uint8_t>(data[5]);
    if (tokenLength > 64 || size < 6 + tokenLength)
        return;

    const juce::String reqToken(data + 6, tokenLength);
    juce::ScopedLock sl(processor.clientLock);
    if (senderIp == processor.clientIp
        && senderPort == processor.clientPort
        && reqSession == processor.currentSessionId.load()
        && reqToken == processor.pairingToken
        && source >= static_cast<int>(DebugTestSource::Off)
        && source <= static_cast<int>(DebugTestSource::FullScalePeak)) {
        processor.debugTestSource = source;
        processor.audioFifo.reset();
        processor.sequenceNumber = 0;
    }
#else
    juce::ignoreUnused(data, size, senderIp, senderPort);
#endif
}

void MixCheckerProcessor::NetworkThread::run() {
    char recvBuffer[256];

    while (!threadShouldExit()) {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();

        if (processor.connectionState.load() == 2) {
            juce::ScopedLock sl(processor.clientLock);
            if (processor.clientIp.isNotEmpty() && processor.clientPort > 0 && nowMs - processor.lastClientActivityMs > 5000.0) {
                processor.connectionState = 1;
                processor.isStreaming = false;
                processor.audioFifo.reset();
            }
        }

        // 1. Check for incoming packets (timeout 1ms)
        if (processor.socket.waitUntilReady(true, 1)) {
            juce::String senderIp;
            int senderPort = 0;
            int bytesRead = processor.socket.read(recvBuffer, sizeof(recvBuffer), false, senderIp, senderPort);
            if (bytesRead > 0) {
                juce::uint8 type = static_cast<juce::uint8>(recvBuffer[0]);
                
                if (type == 0x08) { // DISCOVERY_REQUEST
                    juce::String advertisedIp;
                    {
                        // Remember the phone's subnet so the QR/advertised IP can prefer the
                        // adapter the phone can actually reach (multi-NIC machines).
                        juce::ScopedLock ipSl(processor.ipLock);
                        processor.lastDiscoverySenderIp = senderIp;
                        advertisedIp = processor.localIp;
                    }
                    auto response = buildDiscoveryResponse(advertisedIp,
                                                           processor.currentSessionId.load(),
                                                           processor.pairingToken,
                                                           processor.connectionState.load());
                    processor.socket.write(senderIp, senderPort, response.data(), static_cast<int>(response.size()));
                }
                else if (type == 0x01) { // HELLO
                    bool accepted = false;
                    if (bytesRead >= 5) {
                        const int nameLen = static_cast<unsigned char>(recvBuffer[3]);
                        const int requiredBytes = 4 + nameLen + 2 + 8;
                        if (nameLen >= 0 && bytesRead >= requiredBytes) {
                            const uint16_t reqSession = readUInt16LE(recvBuffer + 4 + nameLen);
                            const juce::String reqToken(recvBuffer + 4 + nameLen + 2, 8);
                            const bool legacyManual = reqSession == 0;
                            const bool pairingMatch = reqSession == processor.currentSessionId.load()
                                                    && reqToken == processor.pairingToken;
                            accepted = legacyManual || pairingMatch;
                        }
                    }

                    if (!accepted) {
                        char ack[2] = { 0x02, 0x00 }; // 0x00 = rejected
                        processor.socket.write(senderIp, senderPort, ack, 2);
                        continue; // Do not connect
                    }

                    juce::ScopedLock sl(processor.clientLock);
                    processor.clientIp = senderIp;
                    processor.clientPort = senderPort;
                    processor.lastClientActivityMs = nowMs;
                    processor.connectionState = 1; // Connected
                    processor.clientSupportsV2Audio = false;
                    processor.activeQualityMode = static_cast<int>(AudioQualityMode::Normal);
                    
                    // Send ACK back (accepted = 1)
                    char ack[2] = { 0x02, 0x01 };
                    processor.socket.write(senderIp, senderPort, ack, 2);
                } 
                else if (type == 0x03) { // START_STREAM
                    juce::ScopedLock sl(processor.clientLock);
                    if (senderIp == processor.clientIp && senderPort == processor.clientPort) {
                        processor.lastClientActivityMs = nowMs;
                        if (!processor.isStreaming) {
                            processor.audioFifo.reset();
                        }
                        processor.connectionState = 2; // Streaming
                        processor.isStreaming = true;
                    }
                }
                else if (type == 0x06) { // HEARTBEAT
                    bool shouldAck = false;
                    {
                        juce::ScopedLock sl(processor.clientLock);
                        if (senderIp == processor.clientIp && senderPort == processor.clientPort) {
                            processor.lastClientActivityMs = nowMs;
                            shouldAck = true;
                        }
                    }

                    if (shouldAck) {
                        char ack[5] = {
                            0x07,
                            0x01,
                            static_cast<char>(processor.currentSessionId.load() & 0xFF),
                            static_cast<char>((processor.currentSessionId.load() >> 8) & 0xFF),
                            static_cast<char>(processor.connectionState.load() & 0xFF)
                        };
                        processor.socket.write(senderIp, senderPort, ack, 5);
                    }
                }
                else if (type == 0x05) { // STOP/DISCONNECT
                    juce::ScopedLock sl(processor.clientLock);
                    if (senderIp == processor.clientIp && senderPort == processor.clientPort) {
                        processor.lastClientActivityMs = nowMs;
                        processor.connectionState = 0; // Back to waiting
                        processor.isStreaming = false;
                        processor.clientIp.clear();
                        processor.clientPort = 0;
                        processor.clientSupportsV2Audio = false;
                        processor.audioFifo.reset();
                    }
                }
                else if (type == modeChangeType) {
                    handleModeChange(recvBuffer, bytesRead, senderIp, senderPort);
                }
                else if (type == debugTestSourceChangeType) {
                    handleDebugTestSourceChange(recvBuffer, bytesRead, senderIp, senderPort);
                }
            }
        }

        // 2. Send outgoing audio packets if streaming
        if (processor.isStreaming && processor.connectionState == 2) {
            int channels = processor.currentChannels;
            if (channels == 0) channels = 2;
            const auto activeMode = qualityModeFromWire(processor.activeQualityMode.load());
            const int maxFramesPerPacket = processor.clientSupportsV2Audio.load()
                ? packetFramesForMode(activeMode)
                : 128;

            int itemsReady = processor.audioFifo.getNumReady();
            int framesReady = itemsReady / channels;

            // We can send multiple packets if we have a lot of data,
            // but let's just send up to 4 packets per loop iteration to avoid hogging the thread
            int packetsSent = 0;
            while (framesReady >= maxFramesPerPacket && packetsSent < 4 && !threadShouldExit()) {
                
                int itemsToRead = maxFramesPerPacket * channels;
                int startIndex1, blockSize1, startIndex2, blockSize2;
                processor.audioFifo.prepareToRead(itemsToRead, startIndex1, blockSize1, startIndex2, blockSize2);

                if (blockSize1 > 0) {
                    std::vector<float> payloadSamples;
                    payloadSamples.resize(static_cast<size_t>(itemsToRead));

                    for (int i = 0; i < itemsToRead; ++i) {
                        const int sourceIndex = (i < blockSize1)
                            ? (startIndex1 + i)
                            : (startIndex2 + (i - blockSize1));
                        payloadSamples[static_cast<size_t>(i)] = processor.audioData[sourceIndex];
                    }

                    processor.audioFifo.finishedRead(blockSize1 + blockSize2);

                    juce::ScopedLock sl(processor.clientLock);
                    if (processor.clientIp.isNotEmpty() && processor.clientPort > 0) {
                        const auto sessionId = processor.currentSessionId.load();
                        const auto sequence = processor.sequenceNumber++;
                        const auto sampleRate = static_cast<uint32_t>(processor.currentSampleRate.load());
                        const auto position = processor.samplePosition.load();
                        const auto packet = processor.clientSupportsV2Audio.load()
                            ? buildV2AudioPacket(sessionId,
                                                 sequence,
                                                 sampleRate,
                                                 static_cast<uint16_t>(channels),
                                                 static_cast<uint32_t>(maxFramesPerPacket),
                                                 position,
                                                 activeMode,
                                                 payloadSamples)
                            : buildLegacyAudioPacket(sessionId,
                                                     sequence,
                                                     sampleRate,
                                                     static_cast<uint8_t>(channels),
                                                     static_cast<uint16_t>(maxFramesPerPacket),
                                                     position,
                                                     payloadSamples);
                        processor.socket.write(processor.clientIp, processor.clientPort, packet.data(), (int)packet.size());
                        processor.packetsSentSinceLog++;
                    }
                }
                
                framesReady -= maxFramesPerPacket;
                packetsSent++;
            }

            if (nowMs - processor.lastDiagnosticsLogMs >= 2000.0) {
                processor.lastDiagnosticsLogMs = nowMs;
                const int itemsNowReady = processor.audioFifo.getNumReady();
                const int framesNowReady = channels > 0 ? itemsNowReady / channels : 0;
                juce::Logger::writeToLog("MixChecker stream diag sampleRate="
                    + juce::String(processor.currentSampleRate.load(), 1)
                    + " mode=" + qualityModeName(activeMode)
                    + " format=Float32 PCM"
                    + " protocol=" + juce::String(processor.clientSupportsV2Audio.load() ? 2 : 1)
                    + " blockSize=" + juce::String(processor.currentBlockSize.load())
                    + " packetFrames=" + juce::String(maxFramesPerPacket)
                    + " ringFrames=" + juce::String(framesNowReady)
                    + " packetsPerSec=" + juce::String(processor.packetsSentSinceLog / 2.0, 1)
                    + " sequence=" + juce::String(processor.sequenceNumber.load()));
                processor.packetsSentSinceLog = 0;
            }
            
            // If we don't have enough frames, just wait a tiny bit
            if (framesReady < maxFramesPerPacket) {
                juce::Thread::sleep(2);
            }
        } else {
            // Not streaming, sleep to save CPU
            juce::Thread::sleep(5);
        }
    }
}

MixCheckerProcessor::MixCheckerProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                     ),
      networkThread(*this)
#endif
{
    audioData.resize(ringBufferSize);

    // Generate fresh session ID and pairing token
    auto& rng = juce::Random::getSystemRandom();
    currentSessionId = (uint16_t)rng.nextInt(65535);
    
    // Generate 8-char hex token
    juce::String tokenHex;
    for (int i = 0; i < 8; ++i)
        tokenHex += juce::String::toHexString(rng.nextInt(16));
    pairingToken = tokenHex;

    // Do not silently share control packets between DAWs/plugin instances.
    bool exclusive = socket.setEnablePortReuse(false);
   #if JUCE_WINDOWS
    const BOOL enabled = TRUE;
    exclusive = exclusive && setsockopt(static_cast<SOCKET>(socket.getRawSocketHandle()),
        SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
   #endif
    if (exclusive && socket.bindToPort(49320, "0.0.0.0")) {
        socketBound = true;
        localIp = chooseBestLocalIpv4({});
        networkThread.startThread();
    } else {
        localIp = "Port in use!";
    }
}

MixCheckerProcessor::~MixCheckerProcessor() {
    networkThread.triggerStop();
    networkThread.stopThread(2000);
    socket.shutdown();
}

void MixCheckerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    audioFifo.reset();
}

void MixCheckerProcessor::releaseResources() {}

bool MixCheckerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}

void MixCheckerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    
    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();
    currentChannels = numChannels;
    samplePosition += numSamples;
    const uint64_t blockStartPosition = samplePosition.load() >= static_cast<uint64_t>(numSamples)
        ? samplePosition.load() - static_cast<uint64_t>(numSamples)
        : 0;
    const auto testSource = static_cast<DebugTestSource>(debugTestSource.load(std::memory_order_relaxed));
    const bool useDebugSource = testSource != DebugTestSource::Off;

    // We MUST be real-time safe here. No locks, no allocations, no network calls.
    // Just copy to the ring buffer if we are streaming.
    if (isStreaming.load(std::memory_order_relaxed)) {
        int itemsToWrite = numSamples * numChannels;
        if (audioFifo.getFreeSpace() >= itemsToWrite) {
            int startIndex1, blockSize1, startIndex2, blockSize2;
            audioFifo.prepareToWrite(itemsToWrite, startIndex1, blockSize1, startIndex2, blockSize2);

            if (blockSize1 + blockSize2 == itemsToWrite) {
                for (int item = 0; item < itemsToWrite; ++item) {
                    const int frame = item / numChannels;
                    const int channel = item % numChannels;
                    const int destinationIndex = (item < blockSize1)
                        ? (startIndex1 + item)
                        : (startIndex2 + (item - blockSize1));
                    audioData[destinationIndex] = useDebugSource
                        ? nextDebugSample(testSource, static_cast<int>(blockStartPosition + static_cast<uint64_t>(frame)), channel, numChannels, currentSampleRate.load())
                        : buffer.getReadPointer(channel)[frame];
                }
                audioFifo.finishedWrite(itemsToWrite);
            }
        }
    }
}

juce::String MixCheckerProcessor::getLocalIpAddress() const {
    juce::ScopedLock sl(ipLock);
    return localIp;
}
int MixCheckerProcessor::getConnectionState() const { return connectionState; }

void MixCheckerProcessor::setStreaming(bool shouldStream) {
    if (connectionState >= 1) {
        isStreaming = shouldStream;
        connectionState = shouldStream ? 2 : 1;
        if (shouldStream) {
            audioFifo.reset();
        }
    }
}

juce::String MixCheckerProcessor::getQrPayload() {
    if (!socketBound) return {}; // Never present a scannable but unusable pairing code.
    // The advertised IP was historically frozen at plugin load, so a network that came up later
    // (hotspot enabled after the DAW, VPN toggled, adapter switched) left a dead IP in the QR
    // forever. The editor polls this method, so refresh here — rate-limited, and preferring the
    // subnet the phone's own discovery requests come from.
    if (socketBound) {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        bool shouldRefresh = false;
        juce::String subnetHint;
        {
            juce::ScopedLock sl(ipLock);
            if (nowMs - lastIpRefreshMs > 2000.0) {
                lastIpRefreshMs = nowMs;
                shouldRefresh = true;
                subnetHint = lastDiscoverySenderIp;
            }
        }
        if (shouldRefresh) {
            juce::String previous;
            {
                juce::ScopedLock sl(ipLock);
                previous = localIp;
            }
            auto refreshed = chooseBestLocalIpv4(subnetHint, previous); // enumerates NICs — outside the lock
            const auto available = getAvailableNetworkAddresses();
            juce::ScopedLock sl(ipLock);
            localIp = selectedNetworkAddress.isEmpty() ? refreshed
                : (available.contains(selectedNetworkAddress) ? selectedNetworkAddress : juce::String());
        }
    }

    juce::ScopedLock sl(ipLock);
    if (scoreLocalIpv4(localIp) < 0) return {};
    return "mixchecker://pair?v=1&host=" + localIp
         + "&port=49320&session=" + juce::String((int)currentSessionId.load())
         + "&token=" + pairingToken;
}

juce::StringArray MixCheckerProcessor::getAvailableNetworkAddresses() const {
    juce::StringArray result;
    for (const auto& address : juce::IPAddress::getAllAddresses(false)) {
        const auto ip = address.toString();
        if (scoreLocalIpv4(ip) >= 0) result.addIfNotAlreadyThere(ip);
    }
    return result;
}

void MixCheckerProcessor::selectNetworkAddress(const juce::String& address) {
    if (address.isNotEmpty() && !getAvailableNetworkAddresses().contains(address)) return;
    juce::ScopedLock sl(ipLock);
    selectedNetworkAddress = address;
    lastIpRefreshMs = 0;
}

juce::String MixCheckerProcessor::getNetworkProblem() const {
    if (!socketBound)
        return "Cannot reserve UDP 49320. Close other MixChecker instances/DAWs, then reload this plugin. Other software may also own the port.";
    juce::ScopedLock sl(ipLock);
    if (scoreLocalIpv4(localIp) < 0)
        return "No usable IPv4 address. Connect to your trusted LAN or choose another address using Network / IP.";
    return {};
}

juce::String MixCheckerProcessor::getPairingToken() const { return pairingToken; }
uint16_t MixCheckerProcessor::getSessionId() const { return currentSessionId; }
int MixCheckerProcessor::getActiveQualityMode() const { return activeQualityMode.load(); }

int MixCheckerProcessor::getPacketFramesForActiveMode() const {
    return packetFramesForMode(qualityModeFromWire(activeQualityMode.load()));
}

float MixCheckerProcessor::nextDebugSample(DebugTestSource source, int frame, int channel, int channels, double sampleRate) {
    const auto twoPi = juce::MathConstants<double>::twoPi;
    const double safeRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float sine = static_cast<float>(std::sin(twoPi * 1000.0 * static_cast<double>(frame) / safeRate) * 0.5);

    switch (source) {
        case DebugTestSource::Silence:
            return 0.0f;
        case DebugTestSource::Sine1k:
            return sine;
        case DebugTestSource::PinkNoise: {
            debugNoiseState = debugNoiseState * 1664525u + 1013904223u;
            const float white = (static_cast<float>((debugNoiseState >> 8) & 0x00FFFFFF) / 8388608.0f) - 1.0f;
            return white * 0.2f;
        }
        case DebugTestSource::LeftOnly:
            return channel == 0 ? sine : 0.0f;
        case DebugTestSource::RightOnly:
            return channel == juce::jmin(1, channels - 1) ? sine : 0.0f;
        case DebugTestSource::StereoPhase:
            return channel == 0 ? sine : -sine;
        case DebugTestSource::FullScalePeak:
            return (frame % 64 == 0) ? (channel == 0 ? 1.0f : -1.0f) : 0.0f;
        case DebugTestSource::Off:
        default:
            return 0.0f;
    }
}

juce::AudioProcessorEditor* MixCheckerProcessor::createEditor() {
    return new MixCheckerEditor(*this);
}
bool MixCheckerProcessor::hasEditor() const { return true; }

const juce::String MixCheckerProcessor::getName() const { return JucePlugin_Name; }
bool MixCheckerProcessor::acceptsMidi() const { return false; }
bool MixCheckerProcessor::producesMidi() const { return false; }
bool MixCheckerProcessor::isMidiEffect() const { return false; }
double MixCheckerProcessor::getTailLengthSeconds() const { return 0.0; }
int MixCheckerProcessor::getNumPrograms() { return 1; }
int MixCheckerProcessor::getCurrentProgram() { return 0; }
void MixCheckerProcessor::setCurrentProgram(int) {}
const juce::String MixCheckerProcessor::getProgramName(int) { return {}; }
void MixCheckerProcessor::changeProgramName(int, const juce::String&) {}
void MixCheckerProcessor::getStateInformation(juce::MemoryBlock&) {}
void MixCheckerProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MixCheckerProcessor();
}
