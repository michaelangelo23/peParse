#include "../include/PESecurityAnalyzer.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>
PESecurityAnalyzer::PESecurityAnalyzer(PPE_FILE_INFO pFileInfo) : pFileInfo_(pFileInfo) {
    entropyResults_.clear();
    securityFeatures_ = {};
    packerInfo_ = {};
    overlayInfo_ = {};
    anomalies_.clear();
}
std::vector<PESecurityAnalyzer::EntropyResult> PESecurityAnalyzer::calculateSectionEntropy() {
    entropyResults_.clear();
    if (!pFileInfo_ || !pFileInfo_->ntHeader) {
        return entropyResults_;
    }
    PIMAGE_SECTION_HEADER sectionHeader;
    if (pFileInfo_->is64Bit) {
        auto pNtHeader64 = (PIMAGE_NT_HEADERS64)pFileInfo_->ntHeader;
        sectionHeader = (PIMAGE_SECTION_HEADER)((DWORD_PTR)pNtHeader64 + 4 + sizeof(IMAGE_FILE_HEADER) + pNtHeader64->FileHeader.SizeOfOptionalHeader);
    } else {
        auto pNtHeader32 = (PIMAGE_NT_HEADERS32)pFileInfo_->ntHeader;
        sectionHeader = (PIMAGE_SECTION_HEADER)((DWORD_PTR)pNtHeader32 + 4 + sizeof(IMAGE_FILE_HEADER) + pNtHeader32->FileHeader.SizeOfOptionalHeader);
    }
    for (int i = 0; i < pFileInfo_->ntHeader->FileHeader.NumberOfSections; i++) {
        EntropyResult result;
        char sectionName[9] = {0};
        memcpy(sectionName, sectionHeader[i].Name, 8);
        result.sectionName = std::string(sectionName);
        result.virtualAddress = sectionHeader[i].VirtualAddress;
        result.size = sectionHeader[i].SizeOfRawData;
        if (sectionHeader[i].SizeOfRawData > 0 && sectionHeader[i].PointerToRawData > 0) {
            BYTE* sectionData = (BYTE*)((DWORD_PTR)pFileInfo_->dosHeader + sectionHeader[i].PointerToRawData);
            result.entropy = calculateEntropy(sectionData, sectionHeader[i].SizeOfRawData);
            result.isPacked = isHighEntropy(result.entropy);
        } else {
            result.entropy = 0.0;
            result.isPacked = false;
        }
        entropyResults_.push_back(result);
    }
    return entropyResults_;
}
double PESecurityAnalyzer::calculateEntropy(const BYTE* data, size_t size) {
    if (!data || size == 0) return 0.0;
    unsigned int frequency[256] = {0};
    for (size_t i = 0; i < size; i++) {
        frequency[data[i]]++;
    }
    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (frequency[i] > 0) {
            double probability = (double)frequency[i] / size;
            entropy -= probability * log2(probability);
        }
    }
    return entropy;
}
PESecurityAnalyzer::SecurityFeatures PESecurityAnalyzer::extractSecurityFeatures() {
    securityFeatures_ = {};
    if (!pFileInfo_ || !pFileInfo_->ntHeader) {
        return securityFeatures_;
    }
    WORD dllCharacteristics;
    if (pFileInfo_->is64Bit) {
        auto pNtHeader64 = (PIMAGE_NT_HEADERS64)pFileInfo_->ntHeader;
        dllCharacteristics = pNtHeader64->OptionalHeader.DllCharacteristics;
    } else {
        auto pNtHeader32 = (PIMAGE_NT_HEADERS32)pFileInfo_->ntHeader;
        dllCharacteristics = pNtHeader32->OptionalHeader.DllCharacteristics;
    }
    securityFeatures_.aslr = (dllCharacteristics & 0x0040) != 0;
    securityFeatures_.dep = (dllCharacteristics & 0x0100) != 0;
    securityFeatures_.seh = (dllCharacteristics & 0x0400) != 0;
    securityFeatures_.cfg = (dllCharacteristics & 0x4000) != 0;
    securityFeatures_.isolationAware = (dllCharacteristics & 0x0200) != 0;
    securityFeatures_.nxCompat = (dllCharacteristics & 0x0100) != 0;
    securityFeatures_.dynamicBase = (dllCharacteristics & 0x0040) != 0;
    securityFeatures_.forceIntegrity = (dllCharacteristics & 0x0080) != 0;
    securityFeatures_.terminalServer = (dllCharacteristics & 0x8000) != 0;
    securityFeatures_.largeAddress = (dllCharacteristics & 0x0020) != 0;
    securityFeatures_.hasReturnFlowGuard = false;
    securityFeatures_.hasIntelCET = false;
    securityFeatures_.hasKernelCFI = false;
    return securityFeatures_;
}
PESecurityAnalyzer::PackerInfo PESecurityAnalyzer::detectPacker() {
    packerInfo_ = {};
    if (!pFileInfo_ || !pFileInfo_->ntHeader) {
        return packerInfo_;
    }
    calculateSectionEntropy();
    int highEntropySections = 0;
    for (const auto& result : entropyResults_) {
        if (result.isPacked) {
            highEntropySections++;
        }
    }
    bool hasHighEntropyText = false;
    bool hasSmallNumberOfImports = false;
    bool hasSuspiciousEntryPoint = false;
    (void)hasSmallNumberOfImports;
    (void)hasSuspiciousEntryPoint;
    for (const auto& result : entropyResults_) {
        if (result.sectionName == ".text" && result.entropy > 7.0) {
            hasHighEntropyText = true;
            break;
        }
    }
    packerInfo_.isPacked = (highEntropySections > 0) || hasHighEntropyText;
    packerInfo_.confidence = highEntropySections * 25.0;
    if (packerInfo_.isPacked) {
        packerInfo_.packerName = "Unknown Packer";
        packerInfo_.indicators = "High entropy sections detected";
    } else {
        packerInfo_.packerName = "None";
        packerInfo_.indicators = "No packing detected";
    }
    return packerInfo_;
}
PESecurityAnalyzer::OverlayInfo PESecurityAnalyzer::detectOverlay() {
    overlayInfo_ = {};
    if (!pFileInfo_ || !pFileInfo_->ntHeader) {
        return overlayInfo_;
    }
    DWORD fileSize = getFileSize();
    DWORD lastSectionEnd = getLastSectionEnd();
    if (fileSize > lastSectionEnd) {
        overlayInfo_.hasOverlay = true;
        overlayInfo_.overlayOffset = lastSectionEnd;
        overlayInfo_.overlaySize = fileSize - lastSectionEnd;
        BYTE* overlayData = (BYTE*)((DWORD_PTR)pFileInfo_->dosHeader + lastSectionEnd);
        overlayInfo_.overlayEntropy = calculateEntropy(overlayData, overlayInfo_.overlaySize);
    }
    return overlayInfo_;
}
std::vector<std::string> PESecurityAnalyzer::detectAnomalies() {
    anomalies_.clear();
    if (!pFileInfo_ || !pFileInfo_->ntHeader) {
        return anomalies_;
    }
    calculateSectionEntropy();
    extractSecurityFeatures();
    detectOverlay();
    for (const auto& result : entropyResults_) {
        if (result.entropy > 7.8) {
            anomalies_.push_back("Section " + result.sectionName + " has extremely high entropy (" +
                               std::to_string(result.entropy) + ")");
        }
    }
    if (!securityFeatures_.aslr) {
        anomalies_.push_back("ASLR is disabled - potential security risk");
    }
    if (!securityFeatures_.dep) {
        anomalies_.push_back("DEP is disabled - potential security risk");
    }
    if (overlayInfo_.hasOverlay && overlayInfo_.overlayEntropy > 7.0) {
        anomalies_.push_back("High entropy overlay detected - possible packed data");
    }
    for (const auto& result : entropyResults_) {
        if (result.sectionName.find("UPX") != std::string::npos ||
            result.sectionName.find("FSG") != std::string::npos ||
            result.sectionName.find("PACK") != std::string::npos) {
            anomalies_.push_back("Suspicious section name detected: " + result.sectionName);
        }
    }
    return anomalies_;
}
void PESecurityAnalyzer::printEntropyAnalysis() {
    printf("\n[+] ENTROPY ANALYSIS\n");
    if (entropyResults_.empty()) {
        calculateSectionEntropy();
    }
    for (const auto& result : entropyResults_) {
        printf("\tSection: %-8s | Entropy: %.2f | Size: 0x%08X | %s\n",
               result.sectionName.c_str(),
               result.entropy,
               result.size,
               result.isPacked ? "HIGH ENTROPY" : "NORMAL");
    }
}
void PESecurityAnalyzer::printSecurityFeatures() {
    printf("\n[+] SECURITY FEATURES\n");
    if (securityFeatures_.aslr == false && securityFeatures_.dep == false) {
        extractSecurityFeatures();
    }
    printf("\tASLR (Address Space Layout Randomization): %s\n", securityFeatures_.aslr ? "ENABLED" : "DISABLED");
    printf("\tDEP (Data Execution Prevention): %s\n", securityFeatures_.dep ? "ENABLED" : "DISABLED");
    printf("\tSEH (Structured Exception Handling): %s\n", securityFeatures_.seh ? "DISABLED" : "ENABLED");
    printf("\tCFG (Control Flow Guard): %s\n", securityFeatures_.cfg ? "ENABLED" : "DISABLED");
    printf("\tIsolation Aware: %s\n", securityFeatures_.isolationAware ? "DISABLED" : "ENABLED");
    printf("\tTerminal Server Aware: %s\n", securityFeatures_.terminalServer ? "ENABLED" : "DISABLED");
    printf("\tLarge Address Aware: %s\n", securityFeatures_.largeAddress ? "ENABLED" : "DISABLED");
}
void PESecurityAnalyzer::printPackerInfo() {
    printf("\n[+] PACKER DETECTION\n");
    if (packerInfo_.packerName.empty()) {
        detectPacker();
    }
    printf("\tPacker Detected: %s\n", packerInfo_.isPacked ? "YES" : "NO");
    printf("\tPacker Name: %s\n", packerInfo_.packerName.c_str());
    printf("\tConfidence: %.1f%%\n", packerInfo_.confidence);
    printf("\tIndicators: %s\n", packerInfo_.indicators.c_str());
}
void PESecurityAnalyzer::printOverlayInfo() {
    printf("\n[+] OVERLAY ANALYSIS\n");
    if (overlayInfo_.overlayOffset == 0) {
        detectOverlay();
    }
    if (overlayInfo_.hasOverlay) {
        printf("\tOverlay Detected: YES\n");
        printf("\tOverlay Offset: 0x%08X\n", overlayInfo_.overlayOffset);
        printf("\tOverlay Size: 0x%08X (%d bytes)\n", overlayInfo_.overlaySize, overlayInfo_.overlaySize);
        printf("\tOverlay Entropy: %.2f\n", overlayInfo_.overlayEntropy);
    } else {
        printf("\tOverlay Detected: NO\n");
    }
}
void PESecurityAnalyzer::printAnomalies() {
    printf("\n[+] ANOMALY DETECTION\n");
    if (anomalies_.empty()) {
        detectAnomalies();
    }
    if (anomalies_.empty()) {
        printf("\tNo anomalies detected.\n");
    } else {
        printf("\tAnomalies found: %zu\n", anomalies_.size());
        for (size_t i = 0; i < anomalies_.size(); i++) {
            printf("\t[%zu] %s\n", i + 1, anomalies_[i].c_str());
        }
    }
}
bool PESecurityAnalyzer::isHighEntropy(double entropy) {
    return entropy > 7.0;
}
bool PESecurityAnalyzer::isLowEntropy(double entropy) {
    return entropy < 3.0;
}
DWORD PESecurityAnalyzer::getFileSize() {
    if (!pFileInfo_ || !pFileInfo_->dosHeader) return 0;
    return 0;
}
DWORD PESecurityAnalyzer::getLastSectionEnd() {
    if (!pFileInfo_ || !pFileInfo_->ntHeader) return 0;
    PIMAGE_SECTION_HEADER sectionHeader;
    if (pFileInfo_->is64Bit) {
        auto pNtHeader64 = (PIMAGE_NT_HEADERS64)pFileInfo_->ntHeader;
        sectionHeader = (PIMAGE_SECTION_HEADER)((DWORD_PTR)pNtHeader64 + 4 + sizeof(IMAGE_FILE_HEADER) + pNtHeader64->FileHeader.SizeOfOptionalHeader);
    } else {
        auto pNtHeader32 = (PIMAGE_NT_HEADERS32)pFileInfo_->ntHeader;
        sectionHeader = (PIMAGE_SECTION_HEADER)((DWORD_PTR)pNtHeader32 + 4 + sizeof(IMAGE_FILE_HEADER) + pNtHeader32->FileHeader.SizeOfOptionalHeader);
    }
    DWORD lastEnd = 0;
    for (int i = 0; i < pFileInfo_->ntHeader->FileHeader.NumberOfSections; i++) {
        DWORD sectionEnd = sectionHeader[i].PointerToRawData + sectionHeader[i].SizeOfRawData;
        if (sectionEnd > lastEnd) {
            lastEnd = sectionEnd;
        }
    }
    return lastEnd;
}
