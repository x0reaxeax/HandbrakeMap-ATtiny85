#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>

static VOID PrintLastError(
    LPCSTR szFunctionName
) {
    fprintf(
        stderr, 
        "[-] %s failed: E%lu\n", 
        szFunctionName, 
        GetLastError()
    );
}
static std::wstring GetDeviceName(
    HANDLE hDevice
) {
    UINT uSize = 0;
    if (0 != GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nullptr, &uSize)) {
        return L"";
    }

    std::wstring wsName(uSize, L'\0');
    UINT ret = GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, wsName.data(), &uSize);
    if (ret == (UINT)-1)
        return L"";

    // trim trailing null if present
    if (!wsName.empty() && wsName.back() == L'\0')
        wsName.pop_back();

    return wsName;
}

static VOID DumpRawInputDevices(VOID) {
    UINT uCount = 0;
    if (0 != GetRawInputDeviceList(
        nullptr, 
        &uCount, 
        sizeof(RAWINPUTDEVICELIST)
    )) {
        PrintLastError("GetRawInputDeviceList(count)");
        return;
    }

    std::vector<RAWINPUTDEVICELIST> vRawInputDeviceList(uCount);
    if ((UINT) -1 == GetRawInputDeviceList(
        vRawInputDeviceList.data(),
        &uCount, 
        sizeof(RAWINPUTDEVICELIST)
    )) {
        PrintLastError("GetRawInputDeviceList(data)");
        return;
    }

    printf("=== Raw Input Devices (%u) ===\n", uCount);

    for (UINT i = 0; i < uCount; i++) {
        HANDLE hDev = vRawInputDeviceList[i].hDevice;
        DWORD dwType = vRawInputDeviceList[i].dwType;

        std::wstring devName = GetDeviceName(hDev);

        printf("[%u] hDevice=%p type=", i, hDev);
        switch (dwType) {
            case RIM_TYPEMOUSE:    
                printf("MOUSE"); 
                break;
            case RIM_TYPEKEYBOARD: 
                printf("KEYBOARD"); 
                break;
            case RIM_TYPEHID:      
                printf("HID"); 
                break;
            default:               
                printf("UNKNOWN(%lu)", dwType); 
                break;
        }
        printf("\n");

        if (!devName.empty()) {
            wprintf(L"    Name: %ls\n", devName.c_str());
        }

        if (RIM_TYPEHID == dwType) {
            RID_DEVICE_INFO info = {};
            info.cbSize = sizeof(info);
            UINT cbSize = sizeof(info);

            if ((UINT)-1 != GetRawInputDeviceInfoW(
                hDev, 
                RIDI_DEVICEINFO, 
                &info, 
                &cbSize
            )) {
                printf(
                    "    HID: Vendor=0x%04X Product=0x%04X Version=0x%04X UsagePage=0x%04X Usage=0x%04X\n",
                    info.hid.dwVendorId,
                    info.hid.dwProductId,
                    info.hid.dwVersionNumber,
                    info.hid.usUsagePage,
                    info.hid.usUsage);
            } else {
                PrintLastError("GetRawInputDeviceInfo(RIDI_DEVICEINFO)");
            }
        }
    }

    printf("=== End device list ===\n\n");
}

LRESULT CALLBACK WndProc(
    HWND hWnd, 
    UINT uMsg, 
    WPARAM wParam, 
    LPARAM lParam
) {
    if (WM_INPUT == uMsg) {
        UINT uSize = 0;
        if (0 != GetRawInputData(
            (HRAWINPUT)lParam, 
            RID_INPUT, 
            nullptr, 
            &uSize, 
            sizeof(RAWINPUTHEADER)
        )) {
            PrintLastError("GetRawInputData(size)");
            return 0;
        }

        std::vector<BYTE> vBuf(uSize);
        UINT uRead = GetRawInputData(
            (HRAWINPUT)lParam, 
            RID_INPUT, 
            vBuf.data(), 
            &uSize, 
            sizeof(RAWINPUTHEADER)
        );
        if ((UINT)-1 == uRead) {
            PrintLastError("GetRawInputData(data)");
            return 0;
        }

        RAWINPUT* rawInput = reinterpret_cast<RAWINPUT*>(vBuf.data());
        if (rawInput->header.dwType == RIM_TYPEHID) {
            const RAWHID& rawHid = rawInput->data.hid;

            RID_DEVICE_INFO info = {};
            info.cbSize = sizeof(info);
            UINT cbSize = sizeof(info);

            printf("\n[WM_INPUT] hDevice=%p ", rawInput->header.hDevice);

            if ((UINT)-1 != GetRawInputDeviceInfoW(
                rawInput->header.hDevice,
                RIDI_DEVICEINFO, 
                &info, 
                &cbSize
            )) {
                printf("VID=0x%04X PID=0x%04X UsagePage=0x%04X Usage=0x%04X\n",
                    info.hid.dwVendorId,
                    info.hid.dwProductId,
                    info.hid.usUsagePage,
                    info.hid.usUsage);
            } else {
                printf("(device info unavailable)\n");
            }

            printf("  Report count=%u, report size=%u\n", rawHid.dwCount, rawHid.dwSizeHid);

            LPBYTE lpData = (LPBYTE) rawHid.bRawData;
            for (DWORD r = 0; r < rawHid.dwCount; r++) {
                printf("  Report[%lu]: ", r);
                for (DWORD i = 0; i < rawHid.dwSizeHid; i++) {
                    printf("%02X ", lpData[r * rawHid.dwSizeHid + i]);
                }
                printf("\n");
            }
        } else if (RIM_TYPEKEYBOARD == rawInput->header.dwType) {
            const RAWKEYBOARD& rawKeyboard = rawInput->data.keyboard;
            printf(
                "\n[WM_INPUT KEYBOARD] hDevice=%p MakeCode=0x%X VKey=0x%X Flags=0x%X Message=0x%X\n",
                rawInput->header.hDevice, 
                rawKeyboard.MakeCode, 
                rawKeyboard.VKey, 
                rawKeyboard.Flags, 
                rawKeyboard.Message
            );
        }

        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int main(void) {
    DumpRawInputDevices();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"hb_probe_class";

    if (0 == RegisterClassW(&wc)) {
        PrintLastError("RegisterClassW");
        return EXIT_FAILURE;
    }

    HWND hWnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"hb_probe",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        nullptr,
        nullptr);

    if (nullptr == hWnd) {
        PrintLastError("CreateWindowExW");
        return EXIT_FAILURE;
    }

    RAWINPUTDEVICE aRawInputDevices[] = {
        // Generic Desktop / Joystick
        { 0x01, 0x04, RIDEV_INPUTSINK, hWnd },

        // Generic Desktop / Game Pad
        { 0x01, 0x05, RIDEV_INPUTSINK, hWnd },

        // Generic Desktop / Multi-axis Controller
        { 0x01, 0x08, RIDEV_INPUTSINK, hWnd },

        // Generic Desktop / Keyboard
        { 0x01, 0x06, RIDEV_INPUTSINK, hWnd },
    };

    if (!RegisterRawInputDevices(
        aRawInputDevices, 
        ARRAYSIZE(aRawInputDevices), 
        sizeof(aRawInputDevices[0])
    )) {
        PrintLastError("RegisterRawInputDevices");
        return EXIT_FAILURE;
    }

    printf("[+] Registered for Raw Input. Pull the handbrake and watch for reports...\n");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return EXIT_SUCCESS;
}