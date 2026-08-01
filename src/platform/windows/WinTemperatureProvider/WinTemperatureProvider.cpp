//#define _CRT_SECURE_NO_WARNINGS
//#define WIN32_LEAN_AND_MEAN
//#include "WinTemperatureProvider.h"
//
//#include <windows.h>
//#include <wininet.h>
//#include <comdef.h>
//#include <Wbemidl.h>
//#include <crow/json.h>
//#include <algorithm>
//#include <cmath>
//
//#pragma comment(lib, "wininet.lib")
//#pragma comment(lib, "wbemuuid.lib")
//
//static std::string FetchLhmJson() {
//    HINTERNET hInternet = InternetOpenA("SystemMonitor", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
//    if (!hInternet) return "";
//
//    DWORD timeout = 500;
//    InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
//    InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
//
//    HINTERNET hConnect = InternetOpenUrlA(hInternet, "http://localhost:8085/data.json", NULL, 0, INTERNET_FLAG_RELOAD, 0);
//    if (!hConnect) {
//        InternetCloseHandle(hInternet);
//        return "";
//    }
//
//    std::string response;
//    char buffer[8192];
//    DWORD bytesRead = 0;
//
//    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
//        response.append(buffer, bytesRead);
//    }
//
//    InternetCloseHandle(hConnect);
//    InternetCloseHandle(hInternet);
//    return response;
//}
//
//static void ParseLhmTemperatures(const crow::json::rvalue& node, std::vector<TemperatureMetrics>& temps) {
//    if (node.t() != crow::json::type::Object) return;
//
//    if (node.has("Text") && node.has("Value")) {
//        std::string valStr = node["Value"].s();
//
//        if (valStr.find("°C") != std::string::npos) {
//            std::string sensorName = node["Text"].s();
//
//            std::string numStr = valStr;
//            size_t degPos = numStr.find("°C");
//            if (degPos != std::string::npos) {
//                numStr = numStr.substr(0, degPos);
//            }
//            std::replace(numStr.begin(), numStr.end(), ',', '.');
//
//            try {
//                double celsius = std::stod(numStr);
//                celsius = std::trunc(celsius * 10.0) / 10.0;
//                if (celsius > 0.0 && celsius < 120.0) {
//                    TemperatureMetrics tm;
//                    tm.SensorName = sensorName;
//                    tm.Celsius = std::round(celsius * 10.0) / 10.0;
//                    temps.push_back(tm);
//                }
//            }
//            catch (...) {}
//        }
//    }
//
//    if (node.has("Children")) {
//        for (const auto& child : node["Children"]) {
//            ParseLhmTemperatures(child, temps);
//        }
//    }
//}
//
//std::vector<TemperatureMetrics> WinTemperatureProvider::GetTemperatures(double fallbackCpuUsage) const {
//    std::vector<TemperatureMetrics> temps;
//
//    std::string jsonStr = FetchLhmJson();
//    if (!jsonStr.empty()) {
//        auto parsed = crow::json::load(jsonStr);
//        if (parsed) {
//            ParseLhmTemperatures(parsed, temps);
//            if (!temps.empty()) {
//                return temps;
//            }
//        }
//    }
//
//    double estimatedTemp = 37.0 + (fallbackCpuUsage * 0.45);
//    TemperatureMetrics tm;
//    tm.SensorName = "CPU Package (Estimated)";
//    tm.Celsius = std::trunc(estimatedTemp * 10.0) / 10.0;
//    temps.push_back(tm);
//
//    return temps;
//}
//
//std::vector<TemperatureMetrics> WinTemperatureProvider::GetTemperaturesUsingWinApi() const {
//    std::vector<TemperatureMetrics> temps;
//
//    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
//    bool coInitialized = SUCCEEDED(hr);
//
//    hr = CoInitializeSecurity(
//        NULL, -1, NULL, NULL,
//        RPC_C_AUTHN_LEVEL_DEFAULT,
//        RPC_C_IMP_LEVEL_IMPERSONATE,
//        NULL, EOAC_NONE, NULL
//    );
//
//    IWbemLocator* pLoc = NULL;
//    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
//
//    if (SUCCEEDED(hr) && pLoc) {
//        IWbemServices* pSvc = NULL;
//        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
//
//        if (SUCCEEDED(hr) && pSvc) {
//            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
//
//            IEnumWbemClassObject* pEnumerator = NULL;
//            hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT * FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
//
//            if (pEnumerator) {
//                IWbemClassObject* pclsObj = NULL;
//                ULONG uReturn = 0;
//
//                while (pEnumerator) {
//                    hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
//                    if (0 == uReturn) break;
//
//                    VARIANT vtProp;
//                    VariantInit(&vtProp);
//                    hr = pclsObj->Get(L"Temperature", 0, &vtProp, 0, 0);
//                    if (SUCCEEDED(hr) && vtProp.vt == VT_I4) {
//                        double kelvinTenths = static_cast<double>(vtProp.lVal);
//                        double rawCelsius = (kelvinTenths / 10.0) - 273.15;
//                        double celsius = std::round(rawCelsius * 10.0) / 10.0;
//
//                        if (celsius > -50.0 && celsius < 150.0) {
//                            TemperatureMetrics tm;
//                            tm.SensorName = "ACPI CPU Thermal Zone";
//                            tm.Celsius = celsius;
//                            temps.push_back(tm);
//                        }
//                    }
//                    VariantClear(&vtProp);
//                    pclsObj->Release();
//                }
//                pEnumerator->Release();
//            }
//            pSvc->Release();
//        }
//        pLoc->Release();
//    }
//
//    if (coInitialized) {
//        CoUninitialize();
//    }
//
//    return temps;
//}


#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "WinTemperatureProvider.h"

#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <crow/json.h>
#include <algorithm>
#include <cmath>
#include <iostream>

#pragma comment(lib, "wbemuuid.lib")

// Объявляем типы функций из DLL
typedef bool(*InitHardwareMonitorFn)();
typedef char* (*GetTemperaturesJsonFn)();
typedef void(*FreeJsonBufferFn)(char*);

// Вспомогательный класс-синглтон для работы с DLL
class LhmDllLoader {
private:
    HMODULE m_hDll = nullptr;
    InitHardwareMonitorFn InitFn = nullptr;
    GetTemperaturesJsonFn GetJsonFn = nullptr;
    FreeJsonBufferFn FreeFn = nullptr;
    bool m_initialized = false;

    LhmDllLoader() {
        m_hDll = LoadLibraryA("HardwareWrapper.dll");
        if (m_hDll) {
            InitFn = (InitHardwareMonitorFn)GetProcAddress(m_hDll, "InitHardwareMonitor");
            GetJsonFn = (GetTemperaturesJsonFn)GetProcAddress(m_hDll, "GetTemperaturesJson");
            FreeFn = (FreeJsonBufferFn)GetProcAddress(m_hDll, "FreeJsonBuffer");

            if (InitFn && GetJsonFn && FreeFn) {
                m_initialized = InitFn();
            }
        }
    }

public:
    static LhmDllLoader& Instance() {
        static LhmDllLoader instance;
        return instance;
    }

    std::string FetchJson() {
        if (!m_initialized || !GetJsonFn || !FreeFn) return "";

        char* rawPtr = GetJsonFn();
        if (!rawPtr) return "";

        std::string jsonStr(rawPtr);
        FreeFn(rawPtr); // Обязательно освобождаем память C#
        return jsonStr;
    }

    ~LhmDllLoader() {
        if (m_hDll) {
            FreeLibrary(m_hDll);
        }
    }
};

static std::string FetchLhmJsonDirect() {
    return LhmDllLoader::Instance().FetchJson();
}

static void ParseLhmTemperatures(const crow::json::rvalue& root, std::vector<TemperatureMetrics>& temps) {
    if (root.t() != crow::json::type::List) return;

    for (const auto& item : root) {
        if (item.has("name") && item.has("value")) {
            std::string sensorName = item["name"].s();
            double celsius = item["value"].d();

            celsius = std::trunc(celsius * 10.0) / 10.0;
            if (celsius > 0.0 && celsius < 120.0) {
                TemperatureMetrics tm;
                tm.SensorName = sensorName;
                tm.Celsius = std::round(celsius * 10.0) / 10.0;
                temps.push_back(tm);
            }
        }
    }
}

std::vector<TemperatureMetrics> WinTemperatureProvider::GetTemperatures(double fallbackCpuUsage) const {
    std::vector<TemperatureMetrics> temps;

    // Прямой запрос к DLL вместо HTTP!
    std::string jsonStr = FetchLhmJsonDirect();
    if (!jsonStr.empty()) {
        auto parsed = crow::json::load(jsonStr);
        if (parsed) {
            ParseLhmTemperatures(parsed, temps);
            if (!temps.empty()) {
                return temps;
            }
        }
    }

    // Резервный расчет, если DLL не загрузилась или запуск был без прав Админа
    double estimatedTemp = 37.0 + (fallbackCpuUsage * 0.45);
    TemperatureMetrics tm;
    tm.SensorName = "CPU Package (Estimated)";
    tm.Celsius = std::trunc(estimatedTemp * 10.0) / 10.0;
    temps.push_back(tm);

    return temps;
}

std::vector<TemperatureMetrics> WinTemperatureProvider::GetTemperaturesUsingWinApi() const {
    std::vector<TemperatureMetrics> temps;

    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);

    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL
    );

    IWbemLocator* pLoc = NULL;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);

    if (SUCCEEDED(hr) && pLoc) {
        IWbemServices* pSvc = NULL;
        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc);

        if (SUCCEEDED(hr) && pSvc) {
            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

            IEnumWbemClassObject* pEnumerator = NULL;
            hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT * FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);

            if (pEnumerator) {
                IWbemClassObject* pclsObj = NULL;
                ULONG uReturn = 0;

                while (pEnumerator) {
                    hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                    if (0 == uReturn) break;

                    VARIANT vtProp;
                    VariantInit(&vtProp);
                    hr = pclsObj->Get(L"Temperature", 0, &vtProp, 0, 0);
                    if (SUCCEEDED(hr) && vtProp.vt == VT_I4) {
                        double kelvinTenths = static_cast<double>(vtProp.lVal);
                        double rawCelsius = (kelvinTenths / 10.0) - 273.15;
                        double celsius = std::round(rawCelsius * 10.0) / 10.0;

                        if (celsius > -50.0 && celsius < 150.0) {
                            TemperatureMetrics tm;
                            tm.SensorName = "ACPI CPU Thermal Zone";
                            tm.Celsius = celsius;
                            temps.push_back(tm);
                        }
                    }
                    VariantClear(&vtProp);
                    pclsObj->Release();
                }
                pEnumerator->Release();
            }
            pSvc->Release();
        }
        pLoc->Release();
    }

    if (coInitialized) {
        CoUninitialize();
    }

    return temps;
}