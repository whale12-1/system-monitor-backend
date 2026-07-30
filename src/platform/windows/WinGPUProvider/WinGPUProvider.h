#pragma once
#include <vector>
#include <windows.h>
#include <dxgi.h>
#include "core/SystemMetricsTypes.h"

class WinGPUProvider {
private:
    // ------------------------------------------------------------------------
    // NVIDIA NVAPI Types & Handles
    // ------------------------------------------------------------------------
    HMODULE m_hNvApi = NULL;
    bool m_NvApiInitialized = false;

    typedef void* (*NvAPI_QueryInterface_t)(unsigned int offset);
    typedef int (*NvAPI_Initialize_t)();
    typedef int (*NvAPI_EnumPhysicalGPUs_t)(int** handles, unsigned int* count);
    typedef int (*NvAPI_GPU_GetFullName_t)(int* handle, char* name);
    typedef int (*NvAPI_GPU_GetThermalSettings_t)(int* handle, unsigned int sensorIndex, void* pThermalSettings);
    typedef int (*NvAPI_GPU_GetDynamicPstatesInfoEx_t)(int* handle, void* pDynamicPstatesInfoEx);
    typedef int (*NvAPI_GPU_GetMemoryInfoEx_t)(int* handle, void* pMemoryInfo);

    NvAPI_QueryInterface_t               NvAPI_QueryInterface = nullptr;
    NvAPI_Initialize_t                   NvAPI_Initialize = nullptr;
    NvAPI_EnumPhysicalGPUs_t             NvAPI_EnumPhysicalGPUs = nullptr;
    NvAPI_GPU_GetFullName_t              NvAPI_GPU_GetFullName = nullptr;
    NvAPI_GPU_GetThermalSettings_t       NvAPI_GPU_GetThermalSettings = nullptr;
    NvAPI_GPU_GetDynamicPstatesInfoEx_t  NvAPI_GPU_GetDynamicPstatesInfoEx = nullptr;
    NvAPI_GPU_GetMemoryInfoEx_t          NvAPI_GPU_GetMemoryInfoEx = nullptr;

    // ------------------------------------------------------------------------
    // AMD ADL Types & Handles
    // ------------------------------------------------------------------------
    HMODULE m_hAmdDll = NULL;
    bool m_AmdInitialized = false;

    typedef void* (__stdcall* ADL_MAIN_MALLOC_CALLBACK)(int);
    typedef int (*ADL_Main_Control_Create_t)(ADL_MAIN_MALLOC_CALLBACK, int);
    typedef int (*ADL_Main_Control_Destroy_t)();
    typedef int (*ADL_Adapter_NumberOfAdapters_Get_t)(int*);
    typedef int (*ADL_Adapter_AdapterInfo_Get_t)(void*, int);
    typedef int (*ADL_Overdrive5_Temperature_Get_t)(int, int, int*);
    typedef int (*ADL_Overdrive5_CurrentActivity_Get_t)(int, void*);

    ADL_Main_Control_Create_t            ADL_Main_Control_Create = nullptr;
    ADL_Main_Control_Destroy_t           ADL_Main_Control_Destroy = nullptr;
    ADL_Adapter_NumberOfAdapters_Get_t   ADL_Adapter_NumberOfAdapters_Get = nullptr;
    ADL_Adapter_AdapterInfo_Get_t        ADL_Adapter_AdapterInfo_Get = nullptr;
    ADL_Overdrive5_Temperature_Get_t     ADL_Overdrive5_Temperature_Get = nullptr;
    ADL_Overdrive5_CurrentActivity_Get_t ADL_Overdrive5_CurrentActivity_Get = nullptr;

    // ------------------------------------------------------------------------
    // Внутренние методы инициализации и сбора
    // ------------------------------------------------------------------------
    void InitNvApi();
    void InitAmdAdl();

    std::vector<GPUMetrics> GetNvidiaMetrics() const;
    std::vector<GPUMetrics> GetAmdMetrics() const;
    std::vector<GPUMetrics> GetIntelDxgiMetrics() const;

public:
    WinGPUProvider();
    ~WinGPUProvider();

    std::vector<GPUMetrics> GetGPUMetrics() const;
};