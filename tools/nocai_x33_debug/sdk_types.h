#pragma once

#include <cstddef>
#include <cstdint>

constexpr int kMaxPrinters = 100;
constexpr std::uint32_t kX33PrnSignature = 0x00005555u;

struct PrinterInfoList
{
    int totalNum = 0;
    char infoList[kMaxPrinters][256] = {};
};

#pragma pack(push, 1)
struct X33DiskHeader
{
    std::uint32_t Signature = 0;
    std::uint32_t XDPI = 0;
    std::uint32_t YDPI = 0;
    std::uint32_t BytesPerLine = 0;
    std::uint32_t Height = 0;
    std::uint32_t Width = 0;
    std::uint32_t PaperWidth = 0;
    std::uint16_t Colors = 0;
    std::uint16_t Bits = 0;
    std::uint32_t Pass = 0;
    std::uint32_t VsdMode = 0;
    std::uint32_t Reserved[2] = {0, 0};
};
#pragma pack(pop)

// ABI passed to API_StartPrint. The vendor demo widens the two WORD fields in
// X33DiskHeader so all 12 members are DWORDs.
struct SdkPrintJobProperty
{
    std::uint32_t Signature = 0;
    std::uint32_t XDPI = 0;
    std::uint32_t YDPI = 0;
    std::uint32_t BytesPerLine = 0;
    std::uint32_t Height = 0;
    std::uint32_t Width = 0;
    std::uint32_t PaperWidth = 0;
    std::uint32_t Colors = 0;
    std::uint32_t Bits = 0;
    std::uint32_t Pass = 0;
    std::uint32_t VsdMode = 0;
    std::uint32_t Reserved = 0;
};

struct JobSettings
{
    std::uint16_t PrintDirection = 0;
    std::uint16_t PrintSpeed = 1;
    std::uint16_t WCSequence = 0;
    std::uint16_t EclosionGrade = 0;
    std::uint16_t HeadSelect = 0;
    std::uint16_t WInkPercent = 0;
    std::uint16_t WInkPassCount = 0;
    std::uint16_t VInkPercent = 0;
    std::uint16_t VInkPassCount = 0;
    std::uint16_t HeadVoltage = 512;
    unsigned char DisableUVLights[6] = {0, 0, 0, 0, 0, 0};
    std::uint16_t CarReset = 1;
    std::uint16_t stripBlank = 0;
    std::uint16_t blankDistance = 0;
};

struct AlignmentValues
{
    std::uint32_t StepValue = 0;
    unsigned char BidiValue = 0;
    std::int16_t HorizontalSpacing[4] = {};
    std::int16_t VerticalSpacing[4] = {};
    unsigned char HorizontalAlignReference = 0;
    unsigned char VerticalAlignReference = 0;
    char LeftChannelAlign_H1[8] = {};
    char LeftChannelAlign_H2[8] = {};
    char LeftChannelAlign_H3[8] = {};
    char LeftChannelAlign_H4[8] = {};
    char RightChannelAlign_H1[8] = {};
    char RightChannelAlign_H2[8] = {};
    char RightChannelAlign_H3[8] = {};
    char RightChannelAlign_H4[8] = {};
};

struct PrinterStatus
{
    std::uint16_t PrintStatus = 0;
    std::uint16_t CleanStatus = 0;
};

struct PrinterInfo
{
    std::uint16_t Mainboard_fpgaVer = 0;
    unsigned char Mainboard_fpgaExVer = 0;
    unsigned char Mainboard_fpgaSubVer = 0;
    std::uint16_t Carboard_fpgaVer = 0;
    unsigned char Carboard_fpgaExVer = 0;
    unsigned char Carboard_fpgaSubVer = 0;
    std::uint16_t Mainboard_cpuVer = 0;
    unsigned char Mainboard_cpuExVer = 0;
    unsigned char Mainboard_cpuSubVer = 0;
    std::uint16_t Carboard_cpuVer = 0;
    unsigned char Carboard_cpuExVer = 0;
    unsigned char Carboard_cpuSubVer = 0;
    std::uint32_t CarParaCRC = 0;
    std::uint16_t UI_CRC = 0;
    std::uint16_t UI_CRC2 = 0;
    unsigned char ID1 = 0;
    unsigned char ID2 = 0;
};

struct UVParamValues
{
    std::int16_t RightR2LOffset = 0;
    std::int16_t RightL2ROffset = 0;
    std::int16_t LeftR2LOffset = 0;
    std::int16_t LeftL2ROffset = 0;
    std::int16_t LampL2ROffset = 0;
};

struct NewUVParamValues
{
    std::int16_t UVLampLeftStartOffset = 0;
    std::int16_t UVLampLeftEndOffset = 0;
    std::int16_t UVLampLeftMinOffset = 0;
    std::int16_t UVLampRightStartOffset = 0;
    std::int16_t UVLampRightEndOffset = 0;
    std::int16_t UVLampRightMinOffset = 0;
    std::int16_t UVLampDelayDistance = 0;
};

static_assert(sizeof(X33DiskHeader) == 48, "X-33 disk header must remain packed");
static_assert(offsetof(X33DiskHeader, Colors) == 28, "disk Colors offset changed");
static_assert(offsetof(X33DiskHeader, Bits) == 30, "disk Bits offset changed");
static_assert(sizeof(SdkPrintJobProperty) == 48, "SDK header ABI changed");
static_assert(offsetof(SdkPrintJobProperty, Colors) == 28, "SDK Colors offset changed");
static_assert(offsetof(SdkPrintJobProperty, Bits) == 32, "SDK Bits offset changed");
static_assert(sizeof(JobSettings) == 32, "SDK JobSettings ABI changed");
static_assert(sizeof(AlignmentValues) == 88, "SDK alignment ABI changed");
static_assert(sizeof(PrinterStatus) == 4, "SDK status ABI changed");
static_assert(sizeof(PrinterInfo) == 28, "SDK printer info ABI changed");
static_assert(sizeof(UVParamValues) == 10, "SDK UV ABI changed");
static_assert(sizeof(NewUVParamValues) == 14, "SDK new UV ABI changed");
