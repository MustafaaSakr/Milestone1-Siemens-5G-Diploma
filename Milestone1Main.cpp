#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <cmath>

using namespace std;

// Function to calculate CRC32
uint32_t calculateCRC(const vector<uint8_t>& data) {
    uint32_t crc = 0x00000000;
    const uint32_t polynomial = 0x814141AB;
    size_t totalBits = data.size() * 8;

    for (size_t i = 0; i < totalBits + 32; i++) {
        uint8_t currentBit;
        if (i < totalBits) {
            size_t byteIndex = i / 8;
            size_t bitIndex = 7 - (i % 8);
            currentBit = (data[byteIndex] >> bitIndex) & 1;
        } else {
            currentBit = 0;
        }

        if ((crc & 0x80000000) != 0) {
            crc = (crc << 1) ^ polynomial;
        } else {
            crc <<= 1;
        }

        crc ^= currentBit;
    }

    return crc;
}

// Function to convert hex string to byte array
vector<uint8_t> convertHexToBytes(const string& hex) {
    vector<uint8_t> byteArray;
    for (size_t i = 0; i < hex.length(); i += 2) {
        string byteString = hex.substr(i, 2);
        uint8_t byte = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
        byteArray.push_back(byte);
    }
    return byteArray;
}

// Function to print a frame in groups of 4 bytes
void printFrameInGroupsOf4(const vector<uint8_t>& frame) {
    size_t totalSize = frame.size();
    size_t i = 0;

    while (i < totalSize) {
        for (size_t j = 0; j < 4 && i < totalSize; ++j, ++i) {
            cout << hex << setfill('0') << setw(2) << (int)frame[i] << " ";
        }
        cout << endl;
    }
}

// Function to build and show Ethernet frame
void buildAndShowFrame(const vector<uint8_t>& preamble,
                       const uint8_t sfd,
                       const vector<uint8_t>& destMac,
                       const vector<uint8_t>& srcMac,
                       const vector<uint8_t>& etherType,
                       const vector<uint8_t>& payload) {
    vector<uint8_t> ethernetFrame;

    ethernetFrame.insert(ethernetFrame.end(), preamble.begin(), preamble.end());
    ethernetFrame.push_back(sfd);
    ethernetFrame.insert(ethernetFrame.end(), destMac.begin(), destMac.end());
    ethernetFrame.insert(ethernetFrame.end(), srcMac.begin(), srcMac.end());
    ethernetFrame.insert(ethernetFrame.end(), etherType.begin(), etherType.end());
    ethernetFrame.insert(ethernetFrame.end(), payload.begin(), payload.end());

    // Compute CRC and append
    uint32_t payloadCRC = calculateCRC(payload);
    ethernetFrame.push_back((payloadCRC >> 24) & 0xFF);
    ethernetFrame.push_back((payloadCRC >> 16) & 0xFF);
    ethernetFrame.push_back((payloadCRC >> 8) & 0xFF);
    ethernetFrame.push_back(payloadCRC & 0xFF);

    const vector<uint8_t> interFrameGap = {0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07};
    ethernetFrame.insert(ethernetFrame.end(), interFrameGap.begin(), interFrameGap.end());

    size_t frameSize = ethernetFrame.size();
    size_t padding = 4 - (frameSize % 4);
    if (padding != 0) {
        ethernetFrame.insert(ethernetFrame.end(), padding, 0x07);
    }

    cout << "Ethernet frame in groups of 4 bytes (with IFG padding):" << endl;
    printFrameInGroupsOf4(ethernetFrame);
}

// Function to send Inter Frame Gap (IFG) bytes one at a time
void transmitIFGs(int totalIFGs) {
    const vector<uint8_t> ifgBytes = {0x07,  0x07, 0x07, 0x07};
    size_t bytesSent = 0;
    size_t totalBytesSent = 0;

    while (totalBytesSent < totalIFGs) {
        cout << hex << setfill('0') << setw(2) << (int)ifgBytes[bytesSent] << " ";
        bytesSent++;
        if (bytesSent == 4) {
            cout << endl;
            bytesSent = 0;
        }
        totalBytesSent++;
    }

    if (bytesSent > 0) {
        cout << endl;
    }
}

int main() {
    ofstream outputFile("outputfile.txt");
    if (!outputFile.is_open()) {
        cerr << "Failed to open output file!" << endl;
        return 1;
    }

    streambuf* consoleBuffer = cout.rdbuf(); 
    cout.rdbuf(outputFile.rdbuf());          

    double lineRate;
    int minIFGsPerPacket;
    size_t maxPacketSize;
    int captureDurationMs;
    int burstCount;
    int burstPeriodUs;
    string destMacAddr, srcMacAddr, etherType, payload;
    const vector<uint8_t> etherTypeBytes = {0xDD, 0xDD};

    // Reading config values from input file
    ifstream configFile("configrationfile.txt");
    if (!configFile.is_open()) {
        cerr << "Error: Unable to open configuration file." << endl;
        return -1;
    }

    string line;
    if (getline(configFile, line)) istringstream(line) >> lineRate;
    if (getline(configFile, line)) istringstream(line) >> captureDurationMs;
    if (getline(configFile, line)) istringstream(line) >> minIFGsPerPacket;
    getline(configFile, destMacAddr);
    getline(configFile, srcMacAddr);
    if (getline(configFile, line)) istringstream(line) >> maxPacketSize;
    if (getline(configFile, line)) istringstream(line) >> burstCount;
    if (getline(configFile, line)) istringstream(line) >> burstPeriodUs;
    configFile.close();

    vector<uint8_t> destMacBytes = convertHexToBytes(destMacAddr);
    vector<uint8_t> srcMacBytes = convertHexToBytes(srcMacAddr);

    const vector<uint8_t> preamble = {0xFB, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};  
    const uint8_t sfd = 0xD5;

    // Calculate various sizes
    float captureTimeUs = captureDurationMs * 1000;
    size_t totalCaptureBytes = ceil((captureTimeUs * (lineRate / 1000000)) / 8);
    size_t burstCycleCount = captureTimeUs / burstPeriodUs;
    float packetTimeUs = (1512 * 8) / (lineRate / 1000000);
    float ifgIntervalUs = burstPeriodUs - (packetTimeUs * burstCount);
    size_t totalIFGBytesPerBurst = ceil((lineRate / 8000000) * ifgIntervalUs);
    size_t totalIFGBytes = ceil(totalIFGBytesPerBurst * burstCycleCount);
    size_t totalPacketBytes = ceil(totalCaptureBytes - totalIFGBytes);
    size_t totalPackets = ceil(totalPacketBytes / 1512);
    size_t payloadSize = ceil(totalPackets * (1500 - 26));

    vector<uint8_t> payloadData(payloadSize, 0x00); 

    int payloadIndex = 0;
    int packetIndex = 0;
    float burstsProcessed = 0;

    // Process bursts and packets
    while (burstsProcessed < burstCycleCount) { 
        for (size_t i = 0; i < burstCount; ++i) {
            vector<uint8_t> packetFragment(payloadData.begin() + payloadIndex, payloadData.begin() + payloadIndex + 1474);
            cout << dec << "Building and showing Packet " << (packetIndex + 1) << ":" << endl;
            buildAndShowFrame(preamble, sfd, destMacBytes, srcMacBytes, etherTypeBytes, packetFragment);
            packetIndex++;
            payloadIndex += 1474;
        }
        
        burstsProcessed++;
        cout << "Transmitting IFG bytes during interval of " << dec << ifgIntervalUs << " microseconds..." << endl;
        transmitIFGs(totalIFGBytesPerBurst);
    }

    cout.rdbuf(consoleBuffer);
    outputFile.close();

    return 0;
}
