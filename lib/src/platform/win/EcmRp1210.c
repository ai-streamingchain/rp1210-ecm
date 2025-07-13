#include "OpenRP1210/RP1210.h"
#include "EcmSimulation.h"
#include <Windows.h>
#include <stdio.h> // Keep for sprintf/vsprintf
#include <time.h> // Keep for time/localtime/strftime
#include <string.h> // For strcpy
#include <stdarg.h> // For va_list

// Simple logging function using OutputDebugString
void log_message(const char* format, ...) {
    char buffer[1024]; // A reasonable buffer size for debug messages
    time_t timer;
    char time_buffer[26];
    struct tm* tm_info;

    time(&timer);
    tm_info = localtime(&timer);

    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    // Format the message with timestamp
    int offset = sprintf(buffer, "[%s] ", time_buffer);

    va_list args;
    va_start(args, format);
    vsprintf(buffer + offset, format, args);
    va_end(args);

    OutputDebugStringA(buffer);
    OutputDebugStringA("\n"); // Add newline for readability in DebugView
}

short WINAPI RP1210_ClientConnect(HWND hwndClient, short nDeviceID, const char *fpchProtocol, long lSendBuffer, long lReceiveBuffer, short nIsAppPacketizingIncomingMsgs)
{
    log_message("RP1210_ClientConnect called: DeviceID=%d, Protocol=%s", nDeviceID, fpchProtocol);
    short result = Sim_ClientConnect(nDeviceID);
    log_message("RP1210_ClientConnect returning: %d", result);
    return result;
}

short WINAPI RP1210_ClientDisconnect(short nClientID)
{
    log_message("RP1210_ClientDisconnect called: ClientID=%d", nClientID);
    Sim_ClientDisconnect(nClientID);
    log_message("RP1210_ClientDisconnect returning: 0");
    return 0;
}

short WINAPI RP1210_SendMessage(short nClientID, char *fpchClientMessage, short nMessageSize, short nNotifyStatusOnTx, short nBlockOnSend)
{
    log_message("RP1210_SendMessage called: ClientID=%d, MessageSize=%d", nClientID, nMessageSize);
    Sim_SendMessage(nClientID, fpchClientMessage, nMessageSize);
    log_message("RP1210_SendMessage returning: 0");
    return 0;
}

short WINAPI RP1210_ReadMessage(short nClientID, char *fpchAPIMessage, short nBufferSize, short nBlockOnRead)
{
    log_message("RP1210_ReadMessage called: ClientID=%d, BufferSize=%d, BlockOnRead=%d", nClientID, nBufferSize, nBlockOnRead);
    short result = Sim_ReadMessage(nClientID, fpchAPIMessage, nBufferSize, nBlockOnRead);
    log_message("RP1210_ReadMessage returning: %d", result);
    return result;
}

short WINAPI RP1210_SendCommand(short nCommandNumber, short nClientID, char *fpchClientCommand, short nMessageSize)
{
    log_message("RP1210_SendCommand called: CommandNumber=%d, ClientID=%d, MessageSize=%d", nCommandNumber, nClientID, nMessageSize);
    Sim_SendCommand(nCommandNumber, nClientID, fpchClientCommand);
    log_message("RP1210_SendCommand returning: 0");
    return 0;
}

void WINAPI RP1210_ReadVersion(char *fpchDLLMajorVersion, char *fpchDLLMinorVersion, char *fpchAPIMajorVersion, char *fpchAPIMinorVersion)
{
    log_message("RP1210_ReadVersion called");
    // Dummy version numbers
    strcpy(fpchDLLMajorVersion, "1");
    strcpy(fpchDLLMinorVersion, "0");
    strcpy(fpchAPIMajorVersion, "1");
    strcpy(fpchAPIMinorVersion, "0");
    log_message("RP1210_ReadVersion returning: DLL %s.%s, API %s.%s", fpchDLLMajorVersion, fpchDLLMinorVersion, fpchAPIMajorVersion, fpchAPIMinorVersion);
}

short WINAPI RP1210_ReadDetailedVersion(short nClientID, char *fpchAPIVersionInfo, char *fpchDLLVersionInfo, char *fpchFWVersionInfo)
{
    log_message("RP1210_ReadDetailedVersion called: ClientID=%d", nClientID);
    // Dummy implementation
    strcpy(fpchAPIVersionInfo, "API Version 1.0");
    strcpy(fpchDLLVersionInfo, "DLL Version 1.0");
    strcpy(fpchFWVersionInfo, "FW Version 1.0");
    log_message("RP1210_ReadDetailedVersion returning: 0");
    return 0;
}

short WINAPI RP1210_GetHardwareStatus(short nClientID, char *fpchClientInfo, short nInfoSize, short nBlockOnRequest)
{
    log_message("RP1210_GetHardwareStatus called: ClientID=%d, InfoSize=%d, BlockOnRequest=%d", nClientID, nInfoSize, nBlockOnRequest);
    // Dummy implementation
    if (fpchClientInfo && nInfoSize > 0) {
        strncpy(fpchClientInfo, "Hardware Status: OK", nInfoSize);
        fpchClientInfo[nInfoSize - 1] = '\0'; // Ensure null termination
    }
    log_message("RP1210_GetHardwareStatus returning: 0");
    return 0;
}

short WINAPI RP1210_GetErrorMsg(short err_code, char *fpchMessage)
{
    log_message("RP1210_GetErrorMsg called: ErrorCode=%d", err_code);
    // Dummy implementation
    if (fpchMessage) {
        sprintf(fpchMessage, "Error %d: Unknown error", err_code);
    }
    log_message("RP1210_GetErrorMsg returning: 0");
    return 0;
}

short WINAPI RP1210_GetLastErrorMsg(short nClientID, int *pErrCode, char *fpchMessage, short nErrorMsgSize)
{
    log_message("RP1210_GetLastErrorMsg called: ClientID=%d, ErrorMsgSize=%d", nClientID, nErrorMsgSize);
    // Dummy implementation
    if (pErrCode) *pErrCode = 0;
    if (fpchMessage && nErrorMsgSize > 0) {
        strncpy(fpchMessage, "No last error", nErrorMsgSize);
        fpchMessage[nErrorMsgSize - 1] = '\0'; // Ensure null termination
    }
    log_message("RP1210_GetLastErrorMsg returning: 0");
    return 0;
}