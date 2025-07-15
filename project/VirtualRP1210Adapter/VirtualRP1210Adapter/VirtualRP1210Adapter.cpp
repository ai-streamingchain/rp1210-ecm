//------------------------------------------------------------------------------
//  Virtual RP1210 Adapter Implementation
//  This file implements a virtual RP1210 adapter for testing and simulation
//------------------------------------------------------------------------------
#include "pch.h"
#include "framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

//------------------------------------------------------------------------------
// RP1210C Constants and Definitions
//------------------------------------------------------------------------------
#define DLLEXPORT __declspec(dllexport)

// RP1210 Command Defines
#define RP1210_Reset_Device                           0
#define RP1210_Set_All_Filters_States_to_Pass         3
#define RP1210_Set_Message_Filtering_For_CAN          5
#define RP1210_Set_All_Filters_States_to_Discard     17
#define RP1210_Echo_Transmitted_Messages             16

// RP1210 Constants
#define BLOCKING_IO              1
#define NON_BLOCKING_IO          0
#define STANDARD_CAN             0x00
#define EXTENDED_CAN             0x01

// RP1210 Error Codes
#define NO_ERRORS                                  0
#define ERR_DLL_NOT_INITIALIZED                  128
#define ERR_INVALID_CLIENT_ID                    129
#define ERR_CLIENT_ALREADY_CONNECTED             130
#define ERR_CLIENT_AREA_FULL                     131
#define ERR_INVALID_DEVICE                       134
#define ERR_DEVICE_IN_USE                        135
#define ERR_INVALID_PROTOCOL                     136
#define ERR_TX_QUEUE_FULL                        137
#define ERR_RX_QUEUE_FULL                        139
#define ERR_MESSAGE_TOO_LONG                     141
#define ERR_HARDWARE_NOT_RESPONDING              142
#define ERR_COMMAND_NOT_SUPPORTED                143
#define ERR_INVALID_COMMAND                      144

//------------------------------------------------------------------------------
// Virtual Adapter Configuration
//------------------------------------------------------------------------------
#define MAX_CLIENTS 16
#define MAX_DEVICES 4
#define MAX_MESSAGE_QUEUE_SIZE 1000
#define VIRTUAL_ADAPTER_NAME "VirtualRP1210"
#define VIRTUAL_ADAPTER_VERSION "1.0.0"

//------------------------------------------------------------------------------
// Data Structures
//------------------------------------------------------------------------------
typedef enum {
    PROTOCOL_CAN,
    PROTOCOL_J1939,
    PROTOCOL_J1708,
    PROTOCOL_ISO15765,
    PROTOCOL_UNKNOWN
} ProtocolType;

typedef struct {
    DWORD timestamp;       // 4 bytes timestamp
    BYTE message_type;     // 1 byte message type (STANDARD_CAN/EXTENDED_CAN)
    BYTE data[1800];       // Message data (CAN ID + payload)
    WORD length;           // Total message length
} RP1210Message;

typedef struct {
    RP1210Message messages[MAX_MESSAGE_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    CRITICAL_SECTION lock;
} MessageQueue;

typedef struct {
    short client_id;
    short device_id;
    ProtocolType protocol;
    DWORD baud_rate;
    BOOL connected;
    BOOL echo_mode;
    BOOL pass_all_filters;

    MessageQueue rx_queue;
    MessageQueue tx_queue;

    HWND notify_window;
    HANDLE thread_handle;
    BOOL thread_running;
} ClientConnection;

typedef struct {
    short device_id;
    char name[64];
    char description[128];
    BOOL in_use;
    DWORD baud_rate;
    BOOL bus_active;
} VirtualDevice;

//------------------------------------------------------------------------------
// Global State
//------------------------------------------------------------------------------
static VirtualDevice g_devices[MAX_DEVICES];
static ClientConnection g_clients[MAX_CLIENTS];
static BOOL g_initialized = FALSE;
static CRITICAL_SECTION g_global_lock;
static HANDLE g_simulation_thread = NULL;
static BOOL g_simulation_running = FALSE;
static DWORD g_response_delay_ms = 0; // Default: no delay

//------------------------------------------------------------------------------
// Function Prototypes
//------------------------------------------------------------------------------
BOOL InitializeVirtualAdapter(void);
void ShutdownVirtualAdapter(void);
ClientConnection* GetClient(short client_id);
ClientConnection* AllocateClient(void);
void FreeClient(short client_id);
BOOL IsClientValid(short client_id);
BOOL IsDeviceValid(short device_id);
ProtocolType ParseProtocolString(const char* protocol_string, DWORD* baud_rate);
BOOL QueueMessage(MessageQueue* queue, const RP1210Message* message);
BOOL DequeueMessage(MessageQueue* queue, RP1210Message* message);
void ClearMessageQueue(MessageQueue* queue);
DWORD GetCurrentTimestamp(void);
DWORD WINAPI SimulationThread(LPVOID param);
void GenerateSimulatedMessages(void);

//------------------------------------------------------------------------------
// Helper Functions Implementation
//------------------------------------------------------------------------------

BOOL InitializeVirtualAdapter(void)
{
    if (g_initialized) {
        return TRUE;
    }

    InitializeCriticalSection(&g_global_lock);

    // Read delay from INI file
    g_response_delay_ms = GetPrivateProfileIntA(
        "VirtualAdapter", "ResponseDelayMs", 0, ".\\VirtualRP1210Adapter.ini");

    // Initialize devices
    for (int i = 0; i < MAX_DEVICES; i++) {
        g_devices[i].device_id = i + 1;
        sprintf_s(g_devices[i].name, sizeof(g_devices[i].name), "Virtual CAN Device %d", i + 1);
        sprintf_s(g_devices[i].description, sizeof(g_devices[i].description), "Virtual RP1210 CAN Device %d", i + 1);
        g_devices[i].in_use = FALSE;
        g_devices[i].baud_rate = 250000; // Default 250K
        g_devices[i].bus_active = FALSE;
    }

    // Initialize clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&g_clients[i], 0, sizeof(ClientConnection));
        g_clients[i].client_id = -1; // Mark as unused
        InitializeCriticalSection(&g_clients[i].rx_queue.lock);
        InitializeCriticalSection(&g_clients[i].tx_queue.lock);
    }

    g_initialized = TRUE;
    return TRUE;
}

void ShutdownVirtualAdapter(void)
{
    if (!g_initialized) {
        return;
    }

    EnterCriticalSection(&g_global_lock);

    // Stop simulation thread
    if (g_simulation_thread) {
        g_simulation_running = FALSE;
        WaitForSingleObject(g_simulation_thread, 5000);
        CloseHandle(g_simulation_thread);
        g_simulation_thread = NULL;
    }

    // Cleanup clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].client_id >= 0) {
            FreeClient(g_clients[i].client_id);
        }
        DeleteCriticalSection(&g_clients[i].rx_queue.lock);
        DeleteCriticalSection(&g_clients[i].tx_queue.lock);
    }

    g_initialized = FALSE;
    LeaveCriticalSection(&g_global_lock);
    DeleteCriticalSection(&g_global_lock);
}

ClientConnection* GetClient(short client_id)
{
    if (client_id < 0 || client_id >= MAX_CLIENTS) {
        return NULL;
    }

    if (g_clients[client_id].client_id != client_id) {
        return NULL;
    }

    return &g_clients[client_id];
}

ClientConnection* AllocateClient(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].client_id < 0) {
            memset(&g_clients[i], 0, sizeof(ClientConnection));
            g_clients[i].client_id = i;
            g_clients[i].connected = FALSE;
            g_clients[i].echo_mode = FALSE;
            g_clients[i].pass_all_filters = TRUE;

            // Initialize message queues
            g_clients[i].rx_queue.head = 0;
            g_clients[i].rx_queue.tail = 0;
            g_clients[i].rx_queue.count = 0;
            g_clients[i].tx_queue.head = 0;
            g_clients[i].tx_queue.tail = 0;
            g_clients[i].tx_queue.count = 0;

            return &g_clients[i];
        }
    }
    return NULL;
}

void FreeClient(short client_id)
{
    ClientConnection* client = GetClient(client_id);
    if (client) {
        if (client->thread_handle) {
            client->thread_running = FALSE;
            WaitForSingleObject(client->thread_handle, 1000);
            CloseHandle(client->thread_handle);
        }

        ClearMessageQueue(&client->rx_queue);
        ClearMessageQueue(&client->tx_queue);

        client->client_id = -1; // Mark as unused
        client->connected = FALSE;
    }
}

BOOL IsClientValid(short client_id)
{
    return GetClient(client_id) != NULL;
}

BOOL IsDeviceValid(short device_id)
{
    return (device_id >= 1 && device_id <= MAX_DEVICES);
}

ProtocolType ParseProtocolString(const char* protocol_string, DWORD* baud_rate)
{
    if (!protocol_string || !baud_rate) {
        return PROTOCOL_UNKNOWN;
    }

    *baud_rate = 250000; // Default

    if (strstr(protocol_string, "CAN")) {
        // Parse baud rate from string like "CAN:Baud=500"
        const char* baud_pos = strstr(protocol_string, "Baud=");
        if (baud_pos) {
            int baud_k = atoi(baud_pos + 5);
            if (baud_k > 0) {
                *baud_rate = baud_k * 1000;
            }
        }
        return PROTOCOL_CAN;
    }
    else if (strstr(protocol_string, "J1939")) {
        return PROTOCOL_J1939;
    }
    else if (strstr(protocol_string, "J1708")) {
        return PROTOCOL_J1708;
    }
    else if (strstr(protocol_string, "ISO15765")) {
        return PROTOCOL_ISO15765;
    }

    return PROTOCOL_UNKNOWN;
}

DWORD GetCurrentTimestamp(void)
{
    return GetTickCount();
}

BOOL QueueMessage(MessageQueue* queue, const RP1210Message* message)
{
    if (!queue || !message) {
        return FALSE;
    }

    EnterCriticalSection(&queue->lock);

    if (queue->count >= MAX_MESSAGE_QUEUE_SIZE) {
        LeaveCriticalSection(&queue->lock);
        return FALSE; // Queue full
    }

    queue->messages[queue->tail] = *message;
    queue->tail = (queue->tail + 1) % MAX_MESSAGE_QUEUE_SIZE;
    queue->count++;

    LeaveCriticalSection(&queue->lock);
    return TRUE;
}

BOOL DequeueMessage(MessageQueue* queue, RP1210Message* message)
{
    if (!queue || !message) {
        return FALSE;
    }

    EnterCriticalSection(&queue->lock);

    if (queue->count == 0) {
        LeaveCriticalSection(&queue->lock);
        return FALSE; // Queue empty
    }

    *message = queue->messages[queue->head];
    queue->head = (queue->head + 1) % MAX_MESSAGE_QUEUE_SIZE;
    queue->count--;

    LeaveCriticalSection(&queue->lock);
    return TRUE;
}

void ClearMessageQueue(MessageQueue* queue)
{
    if (!queue) {
        return;
    }

    EnterCriticalSection(&queue->lock);
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    LeaveCriticalSection(&queue->lock);
}

//------------------------------------------------------------------------------
// RP1210 API Functions Implementation
//------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// RP1210_ClientConnect
short DLLEXPORT WINAPI RP1210_ClientConnect(
    HWND hwndClient,
    short nDeviceId,
    const char* fpchProtocol,
    long lSendBuffer,
    long lReceiveBuffer,
    short nIsAppPacketizingIncomingMsgs)
{
    if (!InitializeVirtualAdapter()) {
        return ERR_DLL_NOT_INITIALIZED;
    }

    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    if (!IsDeviceValid(nDeviceId)) {
        return ERR_INVALID_DEVICE;
    }

    if (!fpchProtocol) {
        return ERR_INVALID_PROTOCOL;
    }

    EnterCriticalSection(&g_global_lock);

    // Check if device is already in use
    if (g_devices[nDeviceId - 1].in_use) {
        LeaveCriticalSection(&g_global_lock);
        return ERR_DEVICE_IN_USE;
    }

    // Allocate a client
    ClientConnection* client = AllocateClient();
    if (!client) {
        LeaveCriticalSection(&g_global_lock);
        return ERR_CLIENT_AREA_FULL;
    }

    // Parse protocol string
    DWORD baud_rate;
    ProtocolType protocol = ParseProtocolString(fpchProtocol, &baud_rate);
    if (protocol == PROTOCOL_UNKNOWN) {
        FreeClient(client->client_id);
        LeaveCriticalSection(&g_global_lock);
        return ERR_INVALID_PROTOCOL;
    }

    // Initialize client
    client->device_id = nDeviceId;
    client->protocol = protocol;
    client->baud_rate = baud_rate;
    client->connected = TRUE;
    client->notify_window = hwndClient;

    // Mark device as in use
    g_devices[nDeviceId - 1].in_use = TRUE;
    g_devices[nDeviceId - 1].baud_rate = baud_rate;
    g_devices[nDeviceId - 1].bus_active = TRUE;

    // Start simulation thread if not already running
    if (!g_simulation_running) {
        g_simulation_running = TRUE;
        g_simulation_thread = CreateThread(NULL, 0, SimulationThread, NULL, 0, NULL);
    }

    short client_id = client->client_id;
    LeaveCriticalSection(&g_global_lock);

    return client_id;
}

// RP1210_ClientDisconnect
short DLLEXPORT WINAPI RP1210_ClientDisconnect(short nClientID)
{
    if (!g_initialized) {
        return ERR_DLL_NOT_INITIALIZED;
    }

    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    ClientConnection* client = GetClient(nClientID);
    if (!client) {
        return ERR_INVALID_CLIENT_ID;
    }

    EnterCriticalSection(&g_global_lock);

    // Mark device as not in use
    if (IsDeviceValid(client->device_id)) {
        g_devices[client->device_id - 1].in_use = FALSE;
        g_devices[client->device_id - 1].bus_active = FALSE;
    }

    FreeClient(nClientID);

    LeaveCriticalSection(&g_global_lock);

    return NO_ERRORS;
}

// RP1210_SendMessage
short DLLEXPORT WINAPI RP1210_SendMessage(
    short nClientID,
    char* fpchClientMessage,
    short nMessageSize,
    short nNotifyStatusOnTx,
    short nBlockOnSend)
{
    if (!g_initialized) {
        return ERR_DLL_NOT_INITIALIZED;
    }

    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    ClientConnection* client = GetClient(nClientID);
    if (!client || !client->connected) {
        return ERR_INVALID_CLIENT_ID;
    }

    if (!fpchClientMessage || nMessageSize <= 0) {
        return ERR_MESSAGE_TOO_LONG;
    }

    // Create RP1210 message for transmission
    RP1210Message msg;
    msg.timestamp = GetCurrentTimestamp();
    msg.length = nMessageSize;

    if (nMessageSize > sizeof(msg.data)) {
        return ERR_MESSAGE_TOO_LONG;
    }

    memcpy(msg.data, fpchClientMessage, nMessageSize);

    // Queue message for transmission
    if (!QueueMessage(&client->tx_queue, &msg)) {
        return ERR_TX_QUEUE_FULL;
    }

    // If echo mode is enabled, also queue to RX
    if (client->echo_mode) {
        QueueMessage(&client->rx_queue, &msg);
    }

    return NO_ERRORS;
}

// RP1210_ReadMessage
short DLLEXPORT WINAPI RP1210_ReadMessage(
    short nClientID,
    char* fpchAPIMessage,
    short nBufferSize,
    short nBlockOnRead)
{
    if (!g_initialized) {
        return ERR_DLL_NOT_INITIALIZED;
    }

    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    ClientConnection* client = GetClient(nClientID);
    if (!client || !client->connected) {
        return ERR_INVALID_CLIENT_ID;
    }

    if (!fpchAPIMessage || nBufferSize <= 0) {
        return ERR_MESSAGE_TOO_LONG;
    }

    RP1210Message msg;

    // Try to dequeue a message
    if (!DequeueMessage(&client->rx_queue, &msg)) {
        // No message available
        if (nBlockOnRead == NON_BLOCKING_IO) {
            return 0; // No data available
        }
        else {
            // For blocking mode, we would wait here
            // For simplicity, just return no data for now
            return 0;
        }
    }

    // Check if buffer is large enough
    if (msg.length > nBufferSize) {
        return ERR_MESSAGE_TOO_LONG;
    }

    // Copy message to output buffer
    memcpy(fpchAPIMessage, msg.data, msg.length);

    return msg.length;
}

// RP1210_SendCommand
short DLLEXPORT WINAPI RP1210_SendCommand(
    short nCommandNumber,
    short nClientID,
    char* fpchClientCommand,
    short nMessageSize)
{
    if (!g_initialized) {
        return ERR_DLL_NOT_INITIALIZED;
    }

    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    ClientConnection* client = GetClient(nClientID);
    if (!client || !client->connected) {
        return ERR_INVALID_CLIENT_ID;
    }

    switch (nCommandNumber) {
        case RP1210_Reset_Device:
            // Reset device - clear queues
            ClearMessageQueue(&client->rx_queue);
            ClearMessageQueue(&client->tx_queue);
            return NO_ERRORS;

        case RP1210_Set_All_Filters_States_to_Pass:
            // Set all filters to pass
            client->pass_all_filters = TRUE;
            return NO_ERRORS;

        case RP1210_Set_All_Filters_States_to_Discard:
            // Set all filters to discard
            client->pass_all_filters = FALSE;
            return NO_ERRORS;

        case RP1210_Echo_Transmitted_Messages:
            // Enable/disable echo mode
            if (nMessageSize >= 1 && fpchClientCommand) {
                client->echo_mode = (fpchClientCommand[0] != 0);
                return NO_ERRORS;
            }
            return ERR_INVALID_COMMAND;

        case RP1210_Set_Message_Filtering_For_CAN:
            // CAN filtering - for now just return success
            return NO_ERRORS;

        default:
            return ERR_COMMAND_NOT_SUPPORTED;
    }
}

// RP1210_ReadVersion
void DLLEXPORT WINAPI RP1210_ReadVersion(
    char* fpchDLLMajorVersion,
    char* fpchDLLMinorVersion,
    char* fpchAPIMajorVersion,
    char* fpchAPIMinorVersion)
{
    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    if (fpchDLLMajorVersion) *fpchDLLMajorVersion = '1';
    if (fpchDLLMinorVersion) *fpchDLLMinorVersion = '0';
    if (fpchAPIMajorVersion) *fpchAPIMajorVersion = '1';
    if (fpchAPIMinorVersion) *fpchAPIMinorVersion = '0';
}

// RP1210_ReadDetailedVersion
short DLLEXPORT WINAPI RP1210_ReadDetailedVersion(
    short nClientID,
    char* fpchAPIVersionInfo,
    char* fpchDLLVersionInfo,
    char* fpchFWVersionInfo)
{
    if (!g_initialized) {
        return ERR_DLL_NOT_INITIALIZED;
    }

    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    if (fpchAPIVersionInfo) {
        strcpy_s(fpchAPIVersionInfo, 80, "Virtual RP1210 Adapter API v1.0.0");
    }
    if (fpchDLLVersionInfo) {
        strcpy_s(fpchDLLVersionInfo, 80, "Virtual RP1210 Adapter DLL v1.0.0");
    }
    if (fpchFWVersionInfo) {
        strcpy_s(fpchFWVersionInfo, 80, "Virtual Firmware v1.0.0");
    }

    return NO_ERRORS;
}

// RP1210_GetHardwareStatus
short DLLEXPORT WINAPI RP1210_GetHardwareStatus(
    short nClientID,
    char* fpchClientInfo,
    short nInfoSize,
    short nBlockOnRequest)
{
    if (!g_initialized) {
        return ERR_DLL_NOT_INITIALIZED;
    }

    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    ClientConnection* client = GetClient(nClientID);
    if (!client || !client->connected) {
        return ERR_INVALID_CLIENT_ID;
    }

    if (fpchClientInfo && nInfoSize > 0) {
        // Return simple status info
        const char* status = "Virtual adapter connected and operational";
        int len = min(strlen(status), nInfoSize - 1);
        strncpy_s(fpchClientInfo, nInfoSize, status, len);
        fpchClientInfo[len] = '\0';
        return len;
    }

    return ERR_INVALID_COMMAND;
}

// RP1210_GetErrorMsg
short DLLEXPORT WINAPI RP1210_GetErrorMsg(short ErrorCode, char* fpchMessage)
{
    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    if (!fpchMessage) {
        return ERR_INVALID_COMMAND;
    }

    const char* error_msg = "Unknown error";

    switch (ErrorCode) {
        case NO_ERRORS:
            error_msg = "No errors";
            break;
        case ERR_DLL_NOT_INITIALIZED:
            error_msg = "DLL not initialized";
            break;
        case ERR_INVALID_CLIENT_ID:
            error_msg = "Invalid client ID";
            break;
        case ERR_CLIENT_ALREADY_CONNECTED:
            error_msg = "Client already connected";
            break;
        case ERR_CLIENT_AREA_FULL:
            error_msg = "Client area full";
            break;
        case ERR_INVALID_DEVICE:
            error_msg = "Invalid device";
            break;
        case ERR_DEVICE_IN_USE:
            error_msg = "Device in use";
            break;
        case ERR_INVALID_PROTOCOL:
            error_msg = "Invalid protocol";
            break;
        case ERR_TX_QUEUE_FULL:
            error_msg = "Transmit queue full";
            break;
        case ERR_RX_QUEUE_FULL:
            error_msg = "Receive queue full";
            break;
        case ERR_MESSAGE_TOO_LONG:
            error_msg = "Message too long";
            break;
        case ERR_HARDWARE_NOT_RESPONDING:
            error_msg = "Hardware not responding";
            break;
        case ERR_COMMAND_NOT_SUPPORTED:
            error_msg = "Command not supported";
            break;
        case ERR_INVALID_COMMAND:
            error_msg = "Invalid command";
            break;
        default:
            error_msg = "Unknown error code";
            break;
    }

    strcpy_s(fpchMessage, 80, error_msg);
    return NO_ERRORS;
}

// RP1210_GetLastErrorMsg
short DLLEXPORT WINAPI RP1210_GetLastErrorMsg(
    short ErrorCode,
    int* SubErrorCode,
    char* fpchMessage,
    short nClientID)
{
    if (g_response_delay_ms > 0) Sleep(g_response_delay_ms);

    if (SubErrorCode) {
        *SubErrorCode = 0;
    }

    return RP1210_GetErrorMsg(ErrorCode, fpchMessage);
}

#ifdef __cplusplus
}
#endif

//------------------------------------------------------------------------------
// Simulation Thread Implementation
//------------------------------------------------------------------------------

DWORD WINAPI SimulationThread(LPVOID param)
{
    UNREFERENCED_PARAMETER(param);

    while (g_simulation_running) {
        GenerateSimulatedMessages();
        Sleep(100); // Generate messages every 100ms
    }

    return 0;
}

void GenerateSimulatedMessages(void)
{
    static DWORD message_counter = 0;

    // Generate some simulated CAN messages for connected clients
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ClientConnection* client = &g_clients[i];

        if (client->client_id >= 0 && client->connected && client->pass_all_filters) {
            // Create a simulated CAN message
            RP1210Message msg;
            msg.timestamp = GetCurrentTimestamp();

            if (client->protocol == PROTOCOL_CAN || client->protocol == PROTOCOL_J1939) {
                // Standard CAN message format
                msg.message_type = STANDARD_CAN;

                // Timestamp (4 bytes)
                *(DWORD*)&msg.data[0] = msg.timestamp;

                // Message type (1 byte)
                msg.data[4] = STANDARD_CAN;

                // CAN ID (2 bytes for standard CAN)
                WORD can_id = 0x123 + (message_counter % 8);
                msg.data[5] = (BYTE)(can_id >> 8);
                msg.data[6] = (BYTE)(can_id & 0xFF);

                // Data payload (8 bytes max)
                msg.data[7] = (BYTE)(message_counter & 0xFF);
                msg.data[8] = (BYTE)((message_counter >> 8) & 0xFF);
                msg.data[9] = (BYTE)((message_counter >> 16) & 0xFF);
                msg.data[10] = (BYTE)((message_counter >> 24) & 0xFF);
                msg.data[11] = 0x55; // Test pattern
                msg.data[12] = 0xAA; // Test pattern
                msg.data[13] = 0x00;
                msg.data[14] = 0x00;

                msg.length = 15; // 4 (timestamp) + 1 (type) + 2 (ID) + 8 (data)

                // Queue the message if there's space
                QueueMessage(&client->rx_queue, &msg);
            }
        }
    }

    message_counter++;
}