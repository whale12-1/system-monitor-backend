#include <windows.h>
#include <winternl.h>
#include "WinGPUProvider.h"
#include <iostream>
#include <vector>
#include <string>

// Windows & Graphics Headers
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>

// Direct Linking
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")

// ============================================================================
// D3DKMT Definitions (Direct3D Kernel Mode Thunk for Low-Level VRAM)
// ============================================================================
typedef UINT D3DKMT_HANDLE;

enum D3DKMT_MEMORY_SEGMENT_GROUP {
    D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL = 0,
    D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL = 1
};

struct D3DKMT_QUERYVIDEOMEMORYINFO {
    D3DKMT_HANDLE hProcess;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_MEMORY_SEGMENT_GROUP MemorySegmentGroup;
    UINT64 Budget;
    UINT64 CurrentUsage;
    UINT64 CurrentReservation;
    UINT64 AvailableForReservation;
};

typedef NTSTATUS(APIENTRY* PFND3DKMT_QUERYVIDEOMEMORYINFO)(D3DKMT_QUERYVIDEOMEMORYINFO*);

// ============================================================================
// NVIDIA NVAPI Definitions
// ============================================================================
#define NVAPI_INITIALIZE_ID                  0x0150E828
#define NVAPI_ENUM_PHYSICAL_GPUS_ID          0xE5AC921F
#define NVAPI_GET_FULL_NAME_ID               0xCEEE8E9F
#define NVAPI_GET_THERMAL_SETTINGS_ID        0xE56D326F
#define NVAPI_GET_DYNAMIC_PSTATES_INFO_EX_ID 0x60DED2ED
#define NVAPI_GET_MEMORY_INFO_EX_ID          0x65431003

struct NV_GPU_THERMAL_SETTINGS {
    unsigned int version;
    unsigned int count;
    struct {
        int controller;
        int defaultMinTemp;
        int defaultMaxTemp;
        int currentTemp;
        int target;
    } sensor[3];
};

struct NV_GPU_DYNAMIC_PSTATES_INFO_EX {
    unsigned int version;
    struct {
        struct {
            unsigned int bIsActive;
            unsigned int percentage;
        } percentage[8];
    } utilization;
};

struct NV_GPU_MEMORY_INFO_EX {
    unsigned int version;
    unsigned int dedicatedVideoMemory; // KB
    unsigned int availableDedicatedVideoMemory; // KB
    unsigned int systemVideoMemory;
    unsigned int sharedSystemMemory;
    unsigned int curAvailableDedicatedVideoMemory; // KB
    unsigned int dedicatedVideoMemoryDedicated;
    unsigned int dedicatedVideoMemoryShared;
};

// ============================================================================
// AMD ADL Definitions
// ============================================================================
struct AdapterInfo {
    int iSize;
    int iAdapterIndex;
    char strAdapterName[256];
    char strDisplayName[256];
    int iBusNumber;
    int iDeviceNumber;
    int iFunctionNumber;
    int iVendorNumber;
};

struct ADLPMActivity {
    int iSize;
    int iEngineClock;
    int iMemoryClock;
    int iVActivity;
    int iPerformanceLevel;
};

// Колбэк-аллокатор памяти для AMD ADL
static void* __stdcall ADL_Main_Memory_Alloc(int iSize) {
    return malloc(iSize);
}

// ============================================================================
// Realization
// ============================================================================

WinGPUProvider::WinGPUProvider() {
    InitNvApi();
    InitAmdAdl();
}

WinGPUProvider::~WinGPUProvider() {
    if (m_hNvApi) {
        FreeLibrary(m_hNvApi);
    }
    if (m_hAmdDll) {
        if (ADL_Main_Control_Destroy) {
            ADL_Main_Control_Destroy();
        }
        FreeLibrary(m_hAmdDll);
    }
}

// ----------------------------------------------------------------------------
// NVIDIA Initialization & Extraction
// ----------------------------------------------------------------------------
void WinGPUProvider::InitNvApi() {
    m_hNvApi = LoadLibraryW(L"nvapi64.dll");
    if (!m_hNvApi) {
        m_hNvApi = LoadLibraryW(L"nvapi.dll");
    }
    if (!m_hNvApi) return;

    NvAPI_QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(m_hNvApi, "nvapi_QueryInterface");
    if (!NvAPI_QueryInterface) return;

    NvAPI_Initialize = (NvAPI_Initialize_t)NvAPI_QueryInterface(NVAPI_INITIALIZE_ID);
    NvAPI_EnumPhysicalGPUs = (NvAPI_EnumPhysicalGPUs_t)NvAPI_QueryInterface(NVAPI_ENUM_PHYSICAL_GPUS_ID);
    NvAPI_GPU_GetFullName = (NvAPI_GPU_GetFullName_t)NvAPI_QueryInterface(NVAPI_GET_FULL_NAME_ID);
    NvAPI_GPU_GetThermalSettings = (NvAPI_GPU_GetThermalSettings_t)NvAPI_QueryInterface(NVAPI_GET_THERMAL_SETTINGS_ID);
    NvAPI_GPU_GetDynamicPstatesInfoEx = (NvAPI_GPU_GetDynamicPstatesInfoEx_t)NvAPI_QueryInterface(NVAPI_GET_DYNAMIC_PSTATES_INFO_EX_ID);
    NvAPI_GPU_GetMemoryInfoEx = (NvAPI_GPU_GetMemoryInfoEx_t)NvAPI_QueryInterface(NVAPI_GET_MEMORY_INFO_EX_ID);

    if (NvAPI_Initialize && NvAPI_Initialize() == 0) {
        m_NvApiInitialized = true;
    }
}

std::vector<GPUMetrics> WinGPUProvider::GetNvidiaMetrics() const {
    std::vector<GPUMetrics> result;
    if (!m_NvApiInitialized || !NvAPI_EnumPhysicalGPUs) return result;

    int* gpuHandles[64] = { 0 };
    unsigned int gpuCount = 0;

    if (NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount) == 0 && gpuCount > 0) {
        for (unsigned int i = 0; i < gpuCount; ++i) {
            GPUMetrics metrics;
            metrics.IsAvailable = true;

            // Имя
            char nameBuf[64] = { 0 };
            if (NvAPI_GPU_GetFullName && NvAPI_GPU_GetFullName(gpuHandles[i], nameBuf) == 0) {
                metrics.Name = nameBuf;
            }

            // Температура
            if (NvAPI_GPU_GetThermalSettings) {
                NV_GPU_THERMAL_SETTINGS thermal = { 0 };
                thermal.version = sizeof(NV_GPU_THERMAL_SETTINGS) | (1 << 16);
                if (NvAPI_GPU_GetThermalSettings(gpuHandles[i], 15, &thermal) == 0) {
                    metrics.Temperature = thermal.sensor[0].currentTemp;
                }
            }

            // Загрузка (%)
            if (NvAPI_GPU_GetDynamicPstatesInfoEx) {
                NV_GPU_DYNAMIC_PSTATES_INFO_EX pstates = { 0 };
                pstates.version = sizeof(NV_GPU_DYNAMIC_PSTATES_INFO_EX) | (1 << 16);
                if (NvAPI_GPU_GetDynamicPstatesInfoEx(gpuHandles[i], &pstates) == 0) {
                    metrics.UsagePercent = pstates.utilization.percentage[0].percentage;
                }
            }

            // Память VRAM (байты)
            if (NvAPI_GPU_GetMemoryInfoEx) {
                NV_GPU_MEMORY_INFO_EX memInfo = { 0 };
                memInfo.version = sizeof(NV_GPU_MEMORY_INFO_EX) | (1 << 16);
                if (NvAPI_GPU_GetMemoryInfoEx(gpuHandles[i], &memInfo) == 0) {
                    metrics.MemoryTotal = static_cast<uint64_t>(memInfo.dedicatedVideoMemory) * 1024;
                    uint64_t freeBytes = static_cast<uint64_t>(memInfo.curAvailableDedicatedVideoMemory) * 1024;
                    metrics.MemoryUsed = (metrics.MemoryTotal > freeBytes) ? (metrics.MemoryTotal - freeBytes) : 0;
                }
            }

            result.push_back(metrics);
        }
    }
    return result;
}

// ----------------------------------------------------------------------------
// AMD Initialization & Extraction
// ----------------------------------------------------------------------------
void WinGPUProvider::InitAmdAdl() {
    m_hAmdDll = LoadLibraryW(L"atiadlxx.dll");
    if (!m_hAmdDll) {
        m_hAmdDll = LoadLibraryW(L"atiadlxy.dll");
    }
    if (!m_hAmdDll) return;

    ADL_Main_Control_Create = (ADL_Main_Control_Create_t)GetProcAddress(m_hAmdDll, "ADL_Main_Control_Create");
    ADL_Main_Control_Destroy = (ADL_Main_Control_Destroy_t)GetProcAddress(m_hAmdDll, "ADL_Main_Control_Destroy");
    ADL_Adapter_NumberOfAdapters_Get = (ADL_Adapter_NumberOfAdapters_Get_t)GetProcAddress(m_hAmdDll, "ADL_Adapter_NumberOfAdapters_Get");
    ADL_Adapter_AdapterInfo_Get = (ADL_Adapter_AdapterInfo_Get_t)GetProcAddress(m_hAmdDll, "ADL_Adapter_AdapterInfo_Get");
    ADL_Overdrive5_Temperature_Get = (ADL_Overdrive5_Temperature_Get_t)GetProcAddress(m_hAmdDll, "ADL_Overdrive5_Temperature_Get");
    ADL_Overdrive5_CurrentActivity_Get = (ADL_Overdrive5_CurrentActivity_Get_t)GetProcAddress(m_hAmdDll, "ADL_Overdrive5_CurrentActivity_Get");

    if (ADL_Main_Control_Create && ADL_Main_Control_Create(ADL_Main_Memory_Alloc, 1) == 0) {
        m_AmdInitialized = true;
    }
}


std::vector<GPUMetrics> WinGPUProvider::GetAmdMetrics() const {
    std::vector<GPUMetrics> result;

    // ------------------------------------------------------------------------
    // 1. Получение имени и температуры через AMD ADL
    // ------------------------------------------------------------------------
    if (m_AmdInitialized && ADL_Adapter_NumberOfAdapters_Get && ADL_Adapter_AdapterInfo_Get) {
        int numAdapters = 0;
        if (ADL_Adapter_NumberOfAdapters_Get(&numAdapters) == 0 && numAdapters > 0) {
            std::vector<AdapterInfo> adapters(numAdapters);
            for (auto& a : adapters) a.iSize = sizeof(AdapterInfo);

            if (ADL_Adapter_AdapterInfo_Get(adapters.data(), sizeof(AdapterInfo) * numAdapters) == 0) {
                for (int i = 0; i < numAdapters; ++i) {
                    std::string adapterName = adapters[i].strAdapterName;

                    if (adapterName.find("AMD") == std::string::npos &&
                        adapterName.find("Radeon") == std::string::npos) {
                        continue;
                    }

                    bool isDuplicate = false;
                    for (const auto& existing : result) {
                        if (existing.Name == adapterName) {
                            isDuplicate = true;
                            break;
                        }
                    }
                    if (isDuplicate) continue;

                    GPUMetrics metrics;
                    metrics.Name = adapterName;
                    metrics.IsAvailable = true;

                    // Температура (работает только на дискретных картах AMD)
                    int temp = 0;
                    if (ADL_Overdrive5_Temperature_Get &&
                        ADL_Overdrive5_Temperature_Get(adapters[i].iAdapterIndex, 0, &temp) == 0) {
                        metrics.Temperature = temp / 1000.0;
                    }

                    result.push_back(metrics);
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // 2. DXGI Fallback: имя и Total Memory
    // ------------------------------------------------------------------------
    if (result.empty()) {
        IDXGIFactory1* pFactory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
            IDXGIAdapter1* pAdapter = nullptr;
            for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 desc;
                if (SUCCEEDED(pAdapter->GetDesc1(&desc)) && desc.VendorId == 0x1002) {
                    GPUMetrics metrics;
                    char nameBuf[128] = { 0 };
                    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nameBuf, sizeof(nameBuf), NULL, NULL);

                    metrics.Name = nameBuf;
                    metrics.IsAvailable = true;

                    result.push_back(metrics);
                    pAdapter->Release();
                    break;
                }
                pAdapter->Release();
            }
            pFactory->Release();
        }
    }

    if (!result.empty()) {
        // Забираем общий объем памяти из DXGI
        IDXGIFactory1* pFactory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
            IDXGIAdapter1* pAdapter = nullptr;
            for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 desc;
                if (SUCCEEDED(pAdapter->GetDesc1(&desc)) && desc.VendorId == 0x1002) {
                    result[0].MemoryTotal = (desc.DedicatedVideoMemory > 0)
                        ? desc.DedicatedVideoMemory
                        : desc.SharedSystemMemory;
                    pAdapter->Release();
                    break;
                }
                pAdapter->Release();
            }
            pFactory->Release();
        }

        // --------------------------------------------------------------------
        // 3. Сбор MemoryUsed через PDH (GPU Adapter Memory)
        // --------------------------------------------------------------------
        static PDH_HQUERY hMemQuery = NULL;
        static PDH_HCOUNTER hSharedMemCounter = NULL;
        static PDH_HCOUNTER hLocalMemCounter = NULL;
        static bool pdhMemInit = false;

        if (!pdhMemInit) {
            if (PdhOpenQuery(NULL, 0, &hMemQuery) == ERROR_SUCCESS) {
                // Shared usage (для iGPU / APU)
                PdhAddEnglishCounterW(hMemQuery, L"\\GPU Adapter Memory(*)\\Shared Usage", 0, &hSharedMemCounter);
                // Dedicated/Local usage (для dGPU)
                PdhAddEnglishCounterW(hMemQuery, L"\\GPU Adapter Memory(*)\\Local Usage", 0, &hLocalMemCounter);

                PdhCollectQueryData(hMemQuery);
                pdhMemInit = true;
            }
        }

        if (pdhMemInit && PdhCollectQueryData(hMemQuery) == ERROR_SUCCESS) {
            uint64_t totalUsedBytes = 0;

            // Считываем Shared Usage
            if (hSharedMemCounter) {
                DWORD dwSize = 0, dwCount = 0;
                PdhGetFormattedCounterArrayW(hSharedMemCounter, PDH_FMT_LARGE, &dwSize, &dwCount, NULL);
                if (dwSize > 0) {
                    std::vector<BYTE> buf(dwSize);
                    auto pItems = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
                    if (PdhGetFormattedCounterArrayW(hSharedMemCounter, PDH_FMT_LARGE, &dwSize, &dwCount, pItems) == ERROR_SUCCESS) {
                        for (DWORD k = 0; k < dwCount; ++k) {
                            if (pItems[k].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                                totalUsedBytes += pItems[k].FmtValue.largeValue;
                            }
                        }
                    }
                }
            }

            // Считываем Local Usage
            if (hLocalMemCounter) {
                DWORD dwSize = 0, dwCount = 0;
                PdhGetFormattedCounterArrayW(hLocalMemCounter, PDH_FMT_LARGE, &dwSize, &dwCount, NULL);
                if (dwSize > 0) {
                    std::vector<BYTE> buf(dwSize);
                    auto pItems = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
                    if (PdhGetFormattedCounterArrayW(hLocalMemCounter, PDH_FMT_LARGE, &dwSize, &dwCount, pItems) == ERROR_SUCCESS) {
                        for (DWORD k = 0; k < dwCount; ++k) {
                            if (pItems[k].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                                totalUsedBytes += pItems[k].FmtValue.largeValue;
                            }
                        }
                    }
                }
            }

            result[0].MemoryUsed = totalUsedBytes;
        }

        // --------------------------------------------------------------------
        // 4. Сбор утилизации 3D-ядра (%) через PDH
        // --------------------------------------------------------------------
        static PDH_HQUERY hQuery = NULL;
        static PDH_HCOUNTER hCounter = NULL;
        static bool pdhInitialized = false;

        if (!pdhInitialized) {
            if (PdhOpenQuery(NULL, 0, &hQuery) == ERROR_SUCCESS) {
                PDH_STATUS status = PdhAddEnglishCounterW(
                    hQuery,
                    L"\\GPU Engine(*)\\Utilization Percentage",
                    0,
                    &hCounter
                );
                if (status == ERROR_SUCCESS) {
                    PdhCollectQueryData(hQuery);
                    pdhInitialized = true;
                }
            }
        }

        if (pdhInitialized) {
            if (PdhCollectQueryData(hQuery) == ERROR_SUCCESS) {
                DWORD dwBufferSize = 0;
                DWORD dwItemCount = 0;

                PdhGetFormattedCounterArrayW(hCounter, PDH_FMT_DOUBLE, &dwBufferSize, &dwItemCount, NULL);

                if (dwBufferSize > 0) {
                    std::vector<BYTE> buffer(dwBufferSize);
                    auto pItemBuffer = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());

                    if (PdhGetFormattedCounterArrayW(hCounter, PDH_FMT_DOUBLE, &dwBufferSize, &dwItemCount, pItemBuffer) == ERROR_SUCCESS) {
                        double totalUsage = 0.0;
                        for (DWORD idx = 0; idx < dwItemCount; ++idx) {
                            if (pItemBuffer[idx].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                                totalUsage += pItemBuffer[idx].FmtValue.doubleValue;
                            }
                        }
                        result[0].UsagePercent = static_cast<float>(totalUsage > 100.0 ? 100.0 : totalUsage);
                    }
                }
            }
        }
    }

    return result;
}
// ----------------------------------------------------------------------------
// Intel Fallback (Via DXGI)
// ----------------------------------------------------------------------------
std::vector<GPUMetrics> WinGPUProvider::GetIntelDxgiMetrics() const {
    std::vector<GPUMetrics> result;

    IDXGIFactory* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
        return result;
    }

    IDXGIAdapter* pAdapter = nullptr;
    for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
            // VendorID Intel = 0x8086
            if (desc.VendorId == 0x8086) {
                GPUMetrics metrics;

                char nameBuf[128] = { 0 };
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nameBuf, sizeof(nameBuf), NULL, NULL);

                metrics.Name = nameBuf;
                metrics.MemoryTotal = desc.DedicatedVideoMemory;
                metrics.IsAvailable = true;

                result.push_back(metrics);
            }
        }
        pAdapter->Release();
    }
    pFactory->Release();

    return result;
}

// ----------------------------------------------------------------------------
// Public Main Method
// ----------------------------------------------------------------------------
std::vector<GPUMetrics> WinGPUProvider::GetGPUMetrics() const {
    std::vector<GPUMetrics> totalGpus;

    // 1. Снимаем NVIDIA
    auto nv = GetNvidiaMetrics();
    totalGpus.insert(totalGpus.end(), nv.begin(), nv.end());

    // 2. Снимаем AMD
    auto amd = GetAmdMetrics();
    totalGpus.insert(totalGpus.end(), amd.begin(), amd.end());

    // 3. Снимаем Intel
    auto intel = GetIntelDxgiMetrics();
    totalGpus.insert(totalGpus.end(), intel.begin(), intel.end());

    return totalGpus;
}