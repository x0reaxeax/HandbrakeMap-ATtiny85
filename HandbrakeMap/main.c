/**
    @author     x0reaxeax
    @brief      Minimal Raw Input USB handbrake remapper

    Listens for a Digistump Digispark ATtiny85-based USB handbrake exposed as a HID gamepad and
     remaps its press/release state to a keyboard key via SendInput().

    Expected report pattern:
        Pressed : 00 80 80 80 80 80 80 01 00
        Released: 00 80 80 80 80 80 80 00 00
*/

#include <Windows.h>

#include <stdio.h>
#include <stdlib.h>

#define TARGET_VID              0x16C0U
#define TARGET_PID              0x27DCU
#define TARGET_USAGE_PAGE       0x0001U
#define TARGET_USAGE            0x0005U
#define TARGET_REPORT_SIZE_MIN  8U
#define TARGET_STATE_INDEX      7U
#define TARGET_STATE_MASK       0x01U

#define REMAP_VK                VK_SPACE

static BOOL g_bPrevPressed = FALSE;

static VOID PrintLastError(
    IN LPCSTR szFunctionName
) {
    fprintf(
        stderr,
        "[-] %s failed: E%lu\n",
        szFunctionName,
        GetLastError()
    );
}

static VOID SendVirtualKey(
    IN WORD wVk,
    IN BOOL bKeyDown
) {
    INPUT input = { 0 };

    input.type = INPUT_KEYBOARD;
    input.ki.wVk = wVk;

    if (FALSE == bKeyDown) {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    }

    if (0 == SendInput(1, &input, sizeof(input))) {
        PrintLastError("SendInput");
    }
}

static BOOL IsTargetDevice(
    IN HANDLE hDevice
) {
    RID_DEVICE_INFO devInfo = { 0 };
    UINT cbDevInfo = sizeof(devInfo);

    devInfo.cbSize = sizeof(devInfo);

    if ((UINT)-1 == GetRawInputDeviceInfoA(
        hDevice,
        RIDI_DEVICEINFO,
        &devInfo,
        &cbDevInfo
    )) {
        PrintLastError(
            "GetRawInputDeviceInfoA(RIDI_DEVICEINFO)"
        );
        return FALSE;
    }

    if (RIM_TYPEHID != devInfo.dwType) {
        return FALSE;
    }

    if (TARGET_VID != devInfo.hid.dwVendorId) {
        return FALSE;
    }

    if (TARGET_PID != devInfo.hid.dwProductId) {
        return FALSE;
    }

    if (TARGET_USAGE_PAGE != devInfo.hid.usUsagePage) {
        return FALSE;
    }

    if (TARGET_USAGE != devInfo.hid.usUsage) {
        return FALSE;
    }

    return TRUE;
}

static VOID HandleHandbrakeReport(
    IN PBYTE pbReport,
    IN DWORD cbReport
) {
    BOOL bPressed = FALSE;

    if (NULL == pbReport) {
        return;
    }

    if (TARGET_REPORT_SIZE_MIN > cbReport) {
        return;
    }

    bPressed = (0 != (pbReport[TARGET_STATE_INDEX] & TARGET_STATE_MASK)) ? TRUE : FALSE;

    if (g_bPrevPressed == bPressed) {
        return;
    }

    g_bPrevPressed = bPressed;

#ifdef _DEBUG
    printf("[*] Handbrake %s\n", (TRUE == bPressed) ? "PRESSED" : "RELEASED");
#endif
    SendVirtualKey(REMAP_VK, bPressed);
}

static VOID ProcessRawInput(
    IN LPARAM lParam
) {
    UINT cbRawInput = 0;
    PRAWINPUT pRawInput = NULL;
    UINT cbRead = 0;

    if (0 != GetRawInputData(
        (HRAWINPUT)lParam,
        RID_INPUT,
        NULL,
        &cbRawInput,
        sizeof(RAWINPUTHEADER)
    )) {
        PrintLastError("GetRawInputData(size)");
        return;
    }

    if (0 == cbRawInput) {
        return;
    }

    pRawInput = malloc(cbRawInput);
    if (NULL == pRawInput) {
        fprintf(
            stderr, 
            "[-] malloc() failed: E%d\n", 
            errno
        );
        return;
    }

    ZeroMemory(pRawInput, cbRawInput);

    cbRead = GetRawInputData(
        (HRAWINPUT)lParam,
        RID_INPUT,
        pRawInput,
        &cbRawInput,
        sizeof(RAWINPUTHEADER)
    );

    if ((UINT)-1 == cbRead) {
        PrintLastError("GetRawInputData(data)");
        goto _FINAL;
    }

    if (RIM_TYPEHID != pRawInput->header.dwType) {
        goto _FINAL;
    }

    if (FALSE == IsTargetDevice(pRawInput->header.hDevice)) {
        goto _FINAL;
    }

    if (0 == pRawInput->data.hid.dwCount) {
        goto _FINAL;
    }

    HandleHandbrakeReport(
        (PBYTE)pRawInput->data.hid.bRawData,
        pRawInput->data.hid.dwSizeHid
    );

_FINAL:
    free(pRawInput);
}

static BOOL RegisterForRawInput(
    IN HWND hWnd
) {
    // my specific lil fella is registered as 'Game Pad', change if needed
    RAWINPUTDEVICE aRid[] = {
        //{ 0x01, 0x04, RIDEV_INPUTSINK, hWnd }, // Joystick
        { 0x01, 0x05, RIDEV_INPUTSINK, hWnd }, // Game Pad
        //{ 0x01, 0x08, RIDEV_INPUTSINK, hWnd }, // Multi-axis Controller
    };

    if (FALSE == RegisterRawInputDevices(
        aRid,
        ARRAYSIZE(aRid),
        sizeof(aRid[0])
    )) {
        PrintLastError("RegisterRawInputDevices");
        return FALSE;
    }

    return TRUE;
}

static LRESULT CALLBACK WndProc(
    IN HWND hWnd,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam
) {
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(wParam);

    switch (uMsg) {
        case WM_INPUT:
            ProcessRawInput(lParam);
            return 0;
    }

    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

int main(void) {
    WNDCLASSA wc = { 0 };
    HWND hWnd = NULL;
    MSG msg = { 0 };

    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "UsbHandbrakeRemapperWindowClass";

    if (0 == RegisterClassA(&wc)) {
        PrintLastError("RegisterClassA");
        return EXIT_FAILURE;
    }

    hWnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "UsbHandbrakeRemapper",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        NULL,
        NULL,
        NULL
    );

    if (NULL == hWnd) {
        PrintLastError("CreateWindowExA");
        return EXIT_FAILURE;
    }

    if (FALSE == RegisterForRawInput(hWnd)) {
        return EXIT_FAILURE;
    }

    printf("[+] Handbrake remapper started\n");
    printf(
        "[*] Target VID=0x%04X PID=0x%04X UsagePage=0x%04X Usage=0x%04X\n",
        TARGET_VID,
        TARGET_PID,
        TARGET_USAGE_PAGE,
        TARGET_USAGE
    );
    printf(
        "[*] Remapping handbrake to VK 0x%02X %s\n",
        REMAP_VK,
        (VK_SPACE == REMAP_VK) ? "(Space)" : ""
    );

    while (0 != GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return EXIT_SUCCESS;
}