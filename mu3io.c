#include <windows.h>
#include <winusb.h>
#include <setupapi.h>
#include <shlwapi.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <initguid.h>
#include <usbiodef.h>

#include "mu3io/mu3io.h"
#include "mu3io/config.h"
#include "mu3io/ledoutput.h"

#include "util/dprintf.h"
#include "util/env.h"

static uint8_t mu3_opbtn;
static uint8_t mu3_left_btn;
static uint8_t mu3_right_btn;
static int16_t mu3_lever_pos;
static struct mu3_io_config mu3_io_cfg;
static bool mu3_io_coin;

static HANDLE hDevice = INVALID_HANDLE_VALUE;
static WINUSB_INTERFACE_HANDLE hWinUsb = NULL;

#define ONTROLLER_VID 0x0E8F
#define ONTROLLER_PID 0x1216
#define INPUT_PIPE_ID 0x84

typedef struct {
    uint8_t left_btn;
    uint8_t right_btn;
    uint8_t op_btn;
    int16_t lever_pos;
} mu3_shared_data_t;

static mu3_shared_data_t* shared_io = NULL;
static HANDLE hMapFile = NULL;

static uint8_t board1_rgb_cache[18];
static uint8_t poll_cycles_since_led_refresh = 0;

static void init_shared_memory() {
    hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(mu3_shared_data_t), L"Local\\Mu3IO_SharedMem");
    dprintf("Ontroller: Created shared memory for button data");
    if (hMapFile == NULL) return;
    shared_io = (mu3_shared_data_t*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(mu3_shared_data_t));
}

static bool is_amdaemon() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    PathStripPathW(path);
    return _wcsicmp(path, L"amdaemon.exe") == 0;
}

static bool open_ontroller() {
    HDEVINFO device_info_set;
    SP_DEVICE_INTERFACE_DATA device_interface_data;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W device_interface_detail_data = NULL;
    DWORD buffer_size = 0;

    // Get all devices matching the WinUSB/USB device class
    device_info_set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info_set == INVALID_HANDLE_VALUE) return false;

    device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (int i = 0; SetupDiEnumDeviceInterfaces(device_info_set, NULL, &GUID_DEVINTERFACE_USB_DEVICE, i, &device_interface_data); i++) {
        SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, NULL, 0, &buffer_size, NULL);
        device_interface_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(buffer_size);
        device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data, buffer_size, NULL, NULL)) {
            if (wcsstr(device_interface_detail_data->DevicePath, L"vid_0e8f") && wcsstr(device_interface_detail_data->DevicePath, L"pid_1216")) {
                hDevice = CreateFileW(device_interface_detail_data->DevicePath, GENERIC_READ | GENERIC_WRITE, 
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
                                     FILE_FLAG_OVERLAPPED, NULL);
                
                if (hDevice != INVALID_HANDLE_VALUE) {
                    if (WinUsb_Initialize(hDevice, &hWinUsb)) {
                        free(device_interface_detail_data);
                        SetupDiDestroyDeviceInfoList(device_info_set);
                        return true;
                    }
                }
            }
        }
        free(device_interface_detail_data);
    }

    SetupDiDestroyDeviceInfoList(device_info_set);
    return false;
}

static void send_led_command(uint8_t board, const uint8_t* rgb, size_t led_count) {
    if (hWinUsb == NULL) return;

    uint8_t packet[64] = {0};
    packet[0] = 0x44;
    packet[1] = 0x4C;
    packet[2] = 1;

    size_t data_size = led_count * 3;
    memcpy(&packet[3], rgb, data_size);

    ULONG bytesWritten;
    WinUsb_WritePipe(hWinUsb, 0x03, packet, sizeof(packet), &bytesWritten, NULL);
}

uint16_t mu3_io_get_api_version(void)
{
    return 0x0101;
}

HRESULT mu3_io_init(void)
{
    dprintf("Ontroller: Starting init hook\n");
    init_shared_memory();
    if (!shared_io) return E_FAIL;

    mu3_io_config_load(&mu3_io_cfg, get_config_path());

    mu3_led_init_mutex = CreateMutex(
        NULL,              // default security attributes
        FALSE,             // initially not owned
        NULL);             // unnamed mutex
    
    if (mu3_led_init_mutex == NULL)
    {
        return E_FAIL;
    }

    dprintf("Ontroller: Initializing hardware (VID: 0x%04X)...\n", ONTROLLER_VID);
    if (is_amdaemon()) {
        dprintf("Ontroller: [amdaemon] Setting up controller as master program...\n");
        if (open_ontroller()) {
            dprintf("Ontroller: [amdaemon] Successfully set up controller.\n");
        } else {
            dprintf("Ontroller: [amdaemon] Failed to find controller.\n");
        }
    } else {
        dprintf("Ontroller: [mu3] Acting as Client...\n");
    }

    return mu3_led_output_init(&mu3_io_cfg);
}

HRESULT mu3_io_poll(void)
{
    if (!shared_io) return S_OK;

    if (is_amdaemon()) {

        uint8_t buffer[64];
        ULONG bytesRead = 0;
        if (hWinUsb == NULL) return S_OK;

        if (WinUsb_ReadPipe(hWinUsb, INPUT_PIPE_ID, buffer, sizeof(buffer), &bytesRead, NULL)) {
            if (buffer[0] == 0x44 && buffer[1] == 0x44 && buffer[2] == 0x54) {
                
                mu3_left_btn = 0;
                mu3_right_btn = 0;
                mu3_opbtn = 0;

                if (buffer[3] & 0x20) mu3_left_btn |= MU3_IO_GAMEBTN_1;
                if (buffer[3] & 0x10) mu3_left_btn |= MU3_IO_GAMEBTN_2;
                if (buffer[3] & 0x08) mu3_left_btn |= MU3_IO_GAMEBTN_3;
                if (buffer[3] & 0x04) mu3_right_btn |= MU3_IO_GAMEBTN_1;
                if (buffer[3] & 0x02) mu3_right_btn |= MU3_IO_GAMEBTN_2;
                if (buffer[3] & 0x01) mu3_right_btn |= MU3_IO_GAMEBTN_3;

                if (buffer[4] & 0x80) mu3_left_btn  |= MU3_IO_GAMEBTN_SIDE;
                if (buffer[4] & 0x40) mu3_right_btn |= MU3_IO_GAMEBTN_SIDE;
                
                if (buffer[4] & 0x20) mu3_left_btn  |= MU3_IO_GAMEBTN_MENU;
                if (buffer[4] & 0x10) mu3_right_btn |= MU3_IO_GAMEBTN_MENU;
                
                if (buffer[4] & 0x08) mu3_opbtn     |= MU3_IO_OPBTN_TEST;
                if (buffer[4] & 0x04) mu3_opbtn     |= MU3_IO_OPBTN_SERVICE;

                int16_t raw_lever = (int16_t)((buffer[5] << 8) | buffer[6]);

                if (mu3_opbtn == 3)
                    dprintf("Ontroller: [amdaemon] Ontroller Lever Value: %d\n", raw_lever);
                
                float center = (mu3_io_cfg.lever_max + mu3_io_cfg.lever_min) / 2.0f;
                float range = (mu3_io_cfg.lever_max - mu3_io_cfg.lever_min) / 2.0f;
                
                if (range > 0) {
                    float norm = (raw_lever - center) / range;
                    if (norm > 1.0f) norm = 1.0f;
                    if (norm < -1.0f) norm = -1.0f;
                    mu3_lever_pos = (int16_t)(32767.0f * norm);
                }

                shared_io->left_btn = mu3_left_btn;
                shared_io->right_btn = mu3_right_btn;
                shared_io->op_btn = mu3_opbtn;
                shared_io->lever_pos = mu3_lever_pos;
            }
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_test) & 0x8000) {
            mu3_opbtn |= MU3_IO_OPBTN_TEST;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_service) & 0x8000) {
            mu3_opbtn |= MU3_IO_OPBTN_SERVICE;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_coin) & 0x8000) {
            if (!mu3_io_coin) {
                mu3_io_coin = true;
                mu3_opbtn |= MU3_IO_OPBTN_COIN;
            }
        } else {
            mu3_io_coin = false;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_left_1)) {
            mu3_left_btn |= MU3_IO_GAMEBTN_1;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_left_2)) {
            mu3_left_btn |= MU3_IO_GAMEBTN_2;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_left_3)) {
            mu3_left_btn |= MU3_IO_GAMEBTN_3;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_right_1)) {
            mu3_right_btn |= MU3_IO_GAMEBTN_1;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_right_2)) {
            mu3_right_btn |= MU3_IO_GAMEBTN_2;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_right_3)) {
            mu3_right_btn |= MU3_IO_GAMEBTN_3;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_left_menu)) {
            mu3_left_btn |= MU3_IO_GAMEBTN_MENU;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_right_menu)) {
            mu3_right_btn |= MU3_IO_GAMEBTN_MENU;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_left_side)) {
            mu3_left_btn |= MU3_IO_GAMEBTN_SIDE;
        }

        if (GetAsyncKeyState(mu3_io_cfg.vk_right_side)) {
            mu3_right_btn |= MU3_IO_GAMEBTN_SIDE;
        }
    } else {
        // If program is mu3, read shared memory for controller values
        mu3_left_btn = shared_io->left_btn;
        mu3_right_btn = shared_io->right_btn;
        mu3_opbtn = shared_io->op_btn;
        mu3_lever_pos = shared_io->lever_pos;
    }

    // Constantly resend rgb data every few cycles as Ontroller rgbs will timeout otherwise
    if (++poll_cycles_since_led_refresh > 10) {
        poll_cycles_since_led_refresh = 0;
        send_led_command(1, board1_rgb_cache, 6);
    }
    return S_OK;
}

void mu3_io_get_opbtns(uint8_t *opbtn)
{
    if (opbtn != NULL) {
        *opbtn = mu3_opbtn;
    }
}

void mu3_io_get_gamebtns(uint8_t *left, uint8_t *right)
{
    if (left != NULL) {
        *left = mu3_left_btn;
    }

    if (right != NULL ){
        *right = mu3_right_btn;
    }
}

void mu3_io_get_lever(int16_t *pos)
{
    if (pos != NULL) {
        *pos = mu3_lever_pos;
    }
}

HRESULT mu3_io_led_init(void)
{
    return S_OK;
}

void mu3_io_led_set_colors(uint8_t board, uint8_t *rgb)
{
    if (is_amdaemon()) {
        // Only handle board 1 led data, although this shouldn't
        // be an issue if only using Ontroller
        if (board == 1) {
            send_led_command(1, rgb, 6);
            memcpy(board1_rgb_cache, rgb, 18);
        }
    } else {
        mu3_led_output_update(board, rgb);
    }
}
