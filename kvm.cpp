#include <iostream>
#include <ctime>
#include <fstream>
#include <string>
#include <windows.h>
#include <wtsapi32.h>
#include <dbt.h>
#include <setupapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "wtsapi32.lib")

struct Config {
    std::string VID;
    std::string PID;
    DWORD inputSource;
};

bool readConfig(const std::string& filename, Config& config) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }

    file >> config.VID >> config.PID >> config.inputSource;
    if (file.fail()) {
        std::cerr << "Failed to read config file: " << filename << std::endl;
        file.close();
        return false;
    }

    file.close();
    std::cout << "Config loaded: VID=" << config.VID << ", PID=" << config.PID << ", inputSource=" << config.inputSource << std::endl;
    return true;
}

bool isTargetDevice(const Config& config, const DEV_BROADCAST_HDR* dbhdr) {
    if (dbhdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
        return false;
    }

    const DEV_BROADCAST_DEVICEINTERFACE* dbi = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE*>(dbhdr);
        std::string devicePath(dbi->dbcc_name);

        if (devicePath.find(config.VID) != std::string::npos && devicePath.find(config.PID) != std::string::npos) {
            return true;
        }

    return false;
}

void PressKey(WORD key) {
    // 创建一个输入结构体数组
    INPUT inputs[2] = {};

    // 填充输入结构体以模拟按下按键
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = key;  // 按键的虚拟键码
    inputs[0].ki.dwFlags = 0;  // 按下键

    // 填充输入结构体以模拟释放按键
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = key;  // 按键的虚拟键码
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;  // 释放键

    // 发送输入事件
    UINT uSent = SendInput(2, inputs, sizeof(INPUT));
    if (uSent != 2) {
        std::cerr << "SendInput failed: " << GetLastError() << std::endl;
    }
}

bool IsWorkStationLocked()
{
    WTS_SESSION_INFO* pSessionInfo = nullptr;
    DWORD sessionCount = 0;
    bool isLocked = false;

    if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &sessionCount))
    {
        for (DWORD i = 0; i < sessionCount; ++i)
        {
            WTSINFO* pWTSInfo = nullptr;
            DWORD bytesReturned = 0;

            if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, pSessionInfo[i].SessionId, WTSSessionInfo, reinterpret_cast<LPTSTR*>(&pWTSInfo), &bytesReturned))
            {
                if (pWTSInfo->State == WTS_SESSIONSTATE_LOCK)
                {
                    isLocked = true;
                }
                WTSFreeMemory(pWTSInfo);
            }
        }
        WTSFreeMemory(pSessionInfo);
    }

    return isLocked;
}

bool changeDisplayInputSource(DWORD inputSource) {

    DWORD numMonitors;
    HMONITOR hMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (!hMonitor) {
        std::cerr << "Failed to get primary monitor handle." << std::endl;
        return false;
    }

    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &numMonitors)) {
        std::cerr << "Failed to get number of physical monitors." << std::endl;
        return false;
    }

    PHYSICAL_MONITOR* pPhysicalMonitors = new PHYSICAL_MONITOR[numMonitors];
        if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, numMonitors, pPhysicalMonitors)) {
        std::cerr << "Failed to get physical monitors." << std::endl;
        delete[] pPhysicalMonitors;
        return false;
    }

    // 获取当前时间
    std::time_t now = std::time(nullptr);
    std::tm* timeinfo = std::localtime(&now);
    // 格式化时间戳
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    bool success = false;
    for (DWORD i = 0; i < numMonitors; ++i) {
        if (SetVCPFeature(pPhysicalMonitors[i].hPhysicalMonitor, 0x60, inputSource)) {
            std::cout << "[" << buffer << "] Display input source changed to: " << inputSource << std::endl;
            success = true;
        }
    }

    // 唤醒显示器
    if (IsWorkStationLocked()) {
        // 屏幕已锁定，输入向上方向键
        PressKey(VK_UP);
        std::cout << "Locked" << std::endl;
    } else {
        // 屏幕未锁定，输入Alt键
        PressKey(VK_MENU);
        std::cout << "Unlocked" << std::endl;
    }

    DestroyPhysicalMonitors(numMonitors, pPhysicalMonitors);
    delete[] pPhysicalMonitors;

    if (!success) {
        std::cerr << "Failed to set VCP feature." << std::endl;
        return false;
    }

    return true;
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static Config config;
    static bool configLoaded = false;

    if (!configLoaded) {
        if (readConfig("config.txt", config)) {
            configLoaded = true;
        } else {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    switch (msg) {
        case WM_DEVICECHANGE:
            if (wParam == DBT_DEVICEARRIVAL) {
                const DEV_BROADCAST_HDR* dbhdr = reinterpret_cast<const DEV_BROADCAST_HDR*>(lParam);
                if (isTargetDevice(config, dbhdr)) {
                    if (changeDisplayInputSource(config.inputSource)) {
                        // 获取屏幕的宽度和高度
                        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

                        // 计算屏幕中心的坐标
                        int centerX = screenWidth / 2;
                        int centerY = screenHeight / 2;

                        // 调用 SetCursorPos 函数移动鼠标到屏幕中心
                        SetCursorPos(centerX, centerY);
                    }
                }
            }
            break;
            case WM_DESTROY:
                PostQuitMessage(0);
            break;
            default:
                return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    return 0;
}

int main() {
    const char* className = "USBDeviceListener";
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, className, nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(className, "USB Device Listener", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);

    GUID GUID_DEVINTERFACE_USB_DEVICE = { 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

    DEV_BROADCAST_DEVICEINTERFACE notificationFilter;
    ZeroMemory(&notificationFilter, sizeof(notificationFilter));
    notificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

    HDEVNOTIFY hDevNotify = RegisterDeviceNotification(hwnd, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!hDevNotify) {
        std::cerr << "Failed to register device notification." << std::endl;
        return 1;
    }

    ShowWindow(hwnd, SW_HIDE);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterDeviceNotification(hDevNotify);
    DestroyWindow(hwnd);
    UnregisterClass(className, wc.hInstance);

    return 0;
}
