#include <cstdint>
#include <cstddef>
#include <cstring>

struct PrinterInfoList
{
    int totalNum;
    char infoList[100][256];
};

struct tagPrintJobProperty
{
    uint32_t Signature;
    uint32_t XDPI;
    uint32_t YDPI;
    uint32_t BytesPerLine;
    uint32_t Height;
    uint32_t Width;
    uint32_t PaperWidth;
    uint32_t Colors;
    uint32_t Bits;
    uint32_t Pass;
    uint32_t VsdMode;
    uint32_t Reserved;
};

static_assert(sizeof(tagPrintJobProperty) == 48);
static_assert(offsetof(tagPrintJobProperty, Colors) == 28);
static_assert(offsetof(tagPrintJobProperty, Bits) == 32);
static_assert(offsetof(tagPrintJobProperty, Pass) == 36);

struct stJobSettings
{
    uint16_t PrintDirection;
    uint16_t PrintSpeed;
    uint16_t WCSequence;
    uint16_t EclosionGrade;
    uint16_t HeadSelect;
    uint16_t WInkPercent;
    uint16_t WInkPassCount;
    uint16_t VInkPercent;
    uint16_t VInkPassCount;
    uint16_t HeadVoltage;
    unsigned char DisableUVLights[6];
    uint16_t CarReset;
    uint16_t stripBlank;
    uint16_t blankDistance;
};

static_assert(sizeof(stJobSettings) == 32);

enum eAlignmentPatternTypes
{
    E_NOZZLE_CHECK = 0
};

static bool selectedPrinter = false;
static bool connectedPrinter = false;
static unsigned int expectedLineSize = 0;
static unsigned int expectedLineCount = 0;
static unsigned int receivedLineCount = 0;
static uint16_t configuredHeadSelect = 0;
static uint16_t configuredPrintDirection = 0;
static uint16_t configuredEclosionGrade = 0;
static uint16_t configuredWcSequence = 0;
static uint16_t configuredWhiteInkPercent = 0;
static uint16_t configuredWhiteInkPassCount = 0;
static uint16_t configuredPrintHeight = 0;
static uint32_t configuredPrintX = 0;
static uint32_t configuredPrintY = 0;
static bool printerInitialized = false;
static bool originSetAfterInit = false;
static unsigned int statusPollCount = 0;
static bool endPrintSignaled = false;
static unsigned int activeColors = 0;

extern "C" int StartPrint(tagPrintJobProperty*);
extern "C" int WriteRipData(char*, unsigned int);
extern "C" int EndRipData();
extern "C" int ExitPrinter();

int API_SearchPrinter(PrinterInfoList* printers, int size)
{
    if (!printers || size < static_cast<int>(sizeof(PrinterInfoList)))
        return 0;

    std::memset(printers, 0, sizeof(*printers));
    printers->totalNum = 1;
    std::strncpy(printers->infoList[0], "Fake x64 SDK Printer", 255);
    return 1;
}

int API_SelectPrinter(int) { selectedPrinter = true; connectedPrinter = false; return 1; }
int API_ConnectPrinter() { connectedPrinter = selectedPrinter; return connectedPrinter ? 1 : 0; }
int API_ContinuePrint() { return 1; }
int API_InitPrinter()
{
    printerInitialized = true;
    originSetAfterInit = false;
    return 1;
}
int API_StartPrint(tagPrintJobProperty* property)
{
    if (!connectedPrinter || !property)
        return 0;
    const bool valid = property->Signature == 0x00005555u
            && property->XDPI == 720
            && property->YDPI == 720
            && property->BytesPerLine > 0
            && property->Height > 0
            && property->Colors > 0
            && property->Bits == 1
            && property->Pass == 1
            && property->VsdMode == 0
            && (property->Colors == 1
                || (property->Colors == 4 && configuredPrintDirection == 1
                    && configuredEclosionGrade == 2
                    && configuredHeadSelect == 0
                    && configuredPrintHeight == 550
                    && printerInitialized
                    && originSetAfterInit
                    && configuredPrintX == 1200
                    && configuredPrintY == 3400)
                || (property->Colors == 6 && configuredPrintDirection == 1
                    && configuredEclosionGrade == 2
                    && configuredHeadSelect == 0
                    && configuredWcSequence == 1
                    && configuredWhiteInkPercent == 3
                    && configuredWhiteInkPassCount == 2
                    && configuredPrintHeight == 550
                    && printerInitialized
                    && originSetAfterInit
                    && configuredPrintX == 1200
                    && configuredPrintY == 3400));
    if (!valid)
        return 0;

    expectedLineSize = property->BytesPerLine;
    expectedLineCount = property->Height * property->Colors;
    receivedLineCount = 0;
    activeColors = property->Colors;
    statusPollCount = 0;
    endPrintSignaled = false;
    return 1;
}
int API_PrintALine(char* data, unsigned int size)
{
    if (!data || size != expectedLineSize)
        return 0;

    if (activeColors == 4 || activeColors == 6) {
        static constexpr unsigned char expectedYmckww[] = {3, 2, 1, 4, 5, 5};
        const unsigned int plane = receivedLineCount % activeColors;
        if (static_cast<unsigned char>(data[0]) != expectedYmckww[plane])
            return 0;
    }

    ++receivedLineCount;
    return static_cast<int>(size);
}
int API_AbortPrint() { return 1; }
int API_PausePrint() { return 1; }
int API_EndPrint()
{
    endPrintSignaled = receivedLineCount == expectedLineCount;
    return endPrintSignaled ? 1 : 0;
}
int API_ClosePrint() { return endPrintSignaled ? 1 : 0; }
int API_SetJobSettings(stJobSettings* settings, int size)
{
    if (!settings || size != static_cast<int>(sizeof(stJobSettings)))
        return 0;
    configuredPrintDirection = settings->PrintDirection;
    configuredEclosionGrade = settings->EclosionGrade;
    configuredHeadSelect = settings->HeadSelect;
    configuredWcSequence = settings->WCSequence;
    configuredWhiteInkPercent = settings->WInkPercent;
    configuredWhiteInkPassCount = settings->WInkPassCount;
    return 1;
}

int API_SetPrintHeight(uint16_t heightMm)
{
    configuredPrintHeight = heightMm;
    return 1;
}

int API_GetPrintHeight(uint16_t* heightMm)
{
    if (!heightMm)
        return 0;
    *heightMm = configuredPrintHeight;
    return 1;
}

int API_SetPrintXYValue(uint32_t xMm, uint32_t yMm)
{
    configuredPrintX = xMm;
    configuredPrintY = yMm;
    originSetAfterInit = printerInitialized;
    return 1;
}

int API_GetPrintXYValue(uint32_t* xMm, uint32_t* yMm)
{
    if (!xMm || !yMm)
        return 0;
    *xMm = configuredPrintX;
    *yMm = configuredPrintY;
    return 1;
}

struct stPrinterStatus
{
    uint16_t PrintStatus;
    uint16_t CleanStatus;
};

int API_GetPrinterStatus(stPrinterStatus* status, int size)
{
    if (!status || size != static_cast<int>(sizeof(stPrinterStatus)))
        return 0;
    status->CleanStatus = 0;
    status->PrintStatus = !endPrintSignaled ? 5 : (statusPollCount++ == 0 ? 1 : 0);
    return 1;
}

int API_PrintAlignmentPattern(eAlignmentPatternTypes)
{
    tagPrintJobProperty property{};
    property.Signature = 0x00005555u;
    property.XDPI = 720;
    property.YDPI = 720;
    property.BytesPerLine = 1;
    property.Height = 1;
    property.Width = 4;
    property.PaperWidth = 4;
    property.Colors = 1;
    property.Bits = 1;
    property.Pass = 1;
    configuredHeadSelect = 2;
    char row = 0;
    return StartPrint(&property) == 1 &&
           WriteRipData(&row, 1) == 1 &&
           EndRipData() == 1 &&
           ExitPrinter() == 1
        ? 1
        : 0;
}
