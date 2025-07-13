#include "EcmSimulation.h"
#include <string.h>
#include <Windows.h>
#include <stdio.h> // For sprintf

// J1939 Address
#define ECM_ADDR 0x00

// J1939 PGNs
#define PGN_REQUEST 0xEA00
#define PGN_COMPONENT_ID 0xFEEE
#define PGN_TP_CM 0xEC00
#define PGN_TP_DT 0xEB00
#define PGN_ADDRESS_CLAIM 0xEE00
#define PGN_COMPONENT_ID_J1939 0xFEEB
#define PGN_VIN_J1939 0xFEEC

// Message Queue
#define MAX_QUEUE_SIZE 50
typedef struct {
    char data[1024];
    short size;
} QueuedMessage;

static QueuedMessage messageQueue[MAX_QUEUE_SIZE];
static int queueHead = 0;
static int queueTail = 0;

// Simulated Data
static const char* VIN = "123456789VIN1234567";
static const char* ESN = "AABB1234";

// Helper for OutputDebugString
void debug_log(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

// Function to add a message to the queue
void enqueueMessage(char* message, short size) {
    debug_log("enqueueMessage called: size=%d", size);
    if ((queueHead + 1) % MAX_QUEUE_SIZE != queueTail) {
        // Correctly format the message with a 4-byte timestamp
        char buffer[1024];
        unsigned long timestamp = GetTickCount(); // Example timestamp
        buffer[0] = (char)(timestamp & 0xFF);
        buffer[1] = (char)((timestamp >> 8) & 0xFF);
        buffer[2] = (char)((timestamp >> 16) & 0xFF);
        buffer[3] = (char)((timestamp >> 24) & 0xFF);
        memcpy(buffer + 4, message, size);

        memcpy(messageQueue[queueHead].data, buffer, size + 4);
        messageQueue[queueHead].size = size + 4;
        queueHead = (queueHead + 1) % MAX_QUEUE_SIZE;
        debug_log("Message enqueued. New queueHead=%d", queueHead);
        Sleep(10); // Add a 10ms delay
    } else {
        debug_log("Message queue full. Message not enqueued.");
    }
}

// Function to handle a request for a specific PGN
void handlePgnRequest(short nClientID, char* requestData) {
    debug_log("handlePgnRequest called: nClientID=%d", nClientID);
    int requestedPgn = (requestData[0] & 0xFF) | ((requestData[1] & 0xFF) << 8) | ((requestData[2] & 0xFF) << 16);
    debug_log("Requested PGN: 0x%X", requestedPgn);

    if (requestedPgn == PGN_COMPONENT_ID) {
        // Respond with Component ID
        char response[100];
        memset(response, 0, sizeof(response));
        response[0] = EXTENDED_CAN; // CAN Message Type
        response[1] = (PGN_COMPONENT_ID >> 16) & 0xFF;
        response[2] = (PGN_COMPONENT_ID >> 8) & 0xFF;
        response[3] = PGN_COMPONENT_ID & 0xFF;
        response[4] = ECM_ADDR; // Source Address

        // VIN
        memcpy(&response[5], VIN, strlen(VIN));
        // ESN
        memcpy(&response[5 + strlen(VIN)], ESN, strlen(ESN));

        enqueueMessage(response, (short)(5 + strlen(VIN) + strlen(ESN)));
        debug_log("Responded with Component ID (VIN: %s, ESN: %s)", VIN, ESN);
    }
}

short Sim_ClientConnect(short nClientID) {
    debug_log("Sim_ClientConnect called: nClientID=%d", nClientID);
    // Claim Address
    char claimMsg[13];
    memset(claimMsg, 0, sizeof(claimMsg));
    claimMsg[0] = EXTENDED_CAN;
    claimMsg[1] = (PGN_ADDRESS_CLAIM >> 16) & 0xFF;
    claimMsg[2] = (PGN_ADDRESS_CLAIM >> 8) & 0xFF;
    claimMsg[3] = PGN_ADDRESS_CLAIM & 0xFF;
    claimMsg[4] = ECM_ADDR;
    // Dummy J1939 Name (8 bytes)
    claimMsg[5] = 0x01; // Byte 0: Industry Group (e.g., 1 for On-Highway)
    claimMsg[6] = 0x02; // Byte 1: Vehicle System Instance, Vehicle System
    claimMsg[7] = 0x03; // Byte 2: Function
    claimMsg[8] = 0x04; // Byte 3: Function Instance, ECU Instance
    claimMsg[9] = 0x05; // Byte 4: Manufacturer Code (LSB)
    claimMsg[10] = 0x06; // Byte 5: Manufacturer Code (MSB)
    claimMsg[11] = 0x07; // Byte 6: Identity Number (LSB)
    claimMsg[12] = 0x08; // Byte 7: Identity Number (MSB)
    enqueueMessage(claimMsg, sizeof(claimMsg));

    // Send Component Identification (0xFEEB) message
    char compIdMsg[13];
    memset(compIdMsg, 0, sizeof(compIdMsg));
    compIdMsg[0] = EXTENDED_CAN;
    compIdMsg[1] = (PGN_COMPONENT_ID_J1939 >> 16) & 0xFF;
    compIdMsg[2] = (PGN_COMPONENT_ID_J1939 >> 8) & 0xFF;
    compIdMsg[3] = PGN_COMPONENT_ID_J1939 & 0xFF;
    compIdMsg[4] = ECM_ADDR;
    // Dummy data for Component Identification (8 bytes)
    compIdMsg[5] = 0x01;
    compIdMsg[6] = 0x02;
    compIdMsg[7] = 0x03;
    compIdMsg[8] = 0x04;
    compIdMsg[9] = 0x05;
    compIdMsg[10] = 0x06;
    compIdMsg[11] = 0x07;
    compIdMsg[12] = 0x08;
    enqueueMessage(compIdMsg, sizeof(compIdMsg));

    // Send VIN (0xFEEC) message
    char vinMsg[22];
    memset(vinMsg, 0, sizeof(vinMsg));
    vinMsg[0] = EXTENDED_CAN;
    vinMsg[1] = (PGN_VIN_J1939 >> 16) & 0xFF;
    vinMsg[2] = (PGN_VIN_J1939 >> 8) & 0xFF;
    vinMsg[3] = PGN_VIN_J1939 & 0xFF;
    vinMsg[4] = ECM_ADDR;
    // Dummy data for VIN (17 bytes ASCII)
    memcpy(&vinMsg[5], VIN, 17);
    enqueueMessage(vinMsg, sizeof(vinMsg));

    debug_log("Sim_ClientConnect returning: %d", nClientID);
    return nClientID; // Indicate success
}

void Sim_ClientDisconnect(short nClientID) {
    debug_log("Sim_ClientDisconnect called: nClientID=%d", nClientID);
    queueHead = 0;
    queueTail = 0;
    debug_log("Message queue cleared.");
}

void Sim_SendMessage(short nClientID, char *fpchClientMessage, short nMessageSize) {
    debug_log("Sim_SendMessage called: nClientID=%d, nMessageSize=%d", nClientID, nMessageSize);
    if (nMessageSize > 4) { // Likely a J1939 message
        int pgn = (fpchClientMessage[1] & 0xFF) | ((fpchClientMessage[2] & 0xFF) << 8) | ((fpchClientMessage[3] & 0xFF) << 16);
        debug_log("Received J1939 message with PGN: 0x%X", pgn);
        if (pgn == PGN_REQUEST) {
            handlePgnRequest(nClientID, &fpchClientMessage[5]);
        } else if (((pgn >> 8) == PGN_ADDRESS_CLAIM) && ((pgn & 0xFF) == 0xFF)) {
            debug_log("Received J1939 Address Claim message (Broadcast). Responding with own claim.");
            // Re-enqueue the simulator's own address claim message
            char claimMsg[9];
            memset(claimMsg, 0, sizeof(claimMsg));
            claimMsg[0] = EXTENDED_CAN;
            claimMsg[1] = (0xEE00 >> 16) & 0xFF;
            claimMsg[2] = (0xEE00 >> 8) & 0xFF;
            claimMsg[3] = 0xEE00 & 0xFF;
            claimMsg[4] = ECM_ADDR;
            claimMsg[5] = 0x80; // Arbitrary Address Capable
            claimMsg[6] = 0x00;
            claimMsg[7] = 0x00;
            claimMsg[8] = 0x00;
            enqueueMessage(claimMsg, sizeof(claimMsg));
        }
    }
}

short Sim_ReadMessage(short nClientID, char *fpchAPIMessage, short nBufferSize, short nBlockOnRead) {
    debug_log("Sim_ReadMessage called: nClientID=%d, nBufferSize=%d, nBlockOnRead=%d", nClientID, nBufferSize, nBlockOnRead);
    if (queueTail != queueHead) {
        short messageSize = messageQueue[queueTail].size;
        if ((size_t)nBufferSize >= (size_t)messageSize) {
            memcpy(fpchAPIMessage, messageQueue[queueTail].data, messageSize);
            queueTail = (queueTail + 1) % MAX_QUEUE_SIZE;
            debug_log("Message read from queue. Size=%d, New queueTail=%d", (int)messageSize, queueTail);
            return messageSize;
        }
    }
    debug_log("No message in queue. Returning 0.");
    return 0; // No message
}

void Sim_SendCommand(short nCommandNumber, short nClientID, char* fpchClientMessage) {
    debug_log("Sim_SendCommand called: nCommandNumber=%d, nClientID=%d", nCommandNumber, nClientID);
    // For now, we'll just clear the queue on a filter command
    if (nCommandNumber == RP1210_Set_All_Filters_States_to_Pass) {
        queueHead = 0;
        queueTail = 0;
        debug_log("Queue cleared by RP1210_Set_All_Filters_States_to_Pass command.");
    } else if (nCommandNumber == RP1210_Get_Protocol_Connection_Speed) {
        debug_log("Received RP1210_Get_Protocol_Connection_Speed command.");
        // The application wants to know the baud rate.
        // Let's respond with the baud rate we are using.
        unsigned long baudRate = 250000; // Simulated baud rate
        debug_log("Responding with baud rate: %lu", baudRate);
        char response[4];
        response[0] = (char)(baudRate & 0xFF);
        response[1] = (char)((baudRate >> 8) & 0xFF);
        response[2] = (char)((baudRate >> 16) & 0xFF);
        response[3] = (char)((baudRate >> 24) & 0xFF);
        enqueueMessage(response, sizeof(response));
    } else if (nCommandNumber == RP1210_Set_Message_Filtering_For_J1939) {
        debug_log("Received RP1210_Set_Message_Filtering_For_J1939 command. Acknowledging.");
    } else if (nCommandNumber == RP1210_Set_Message_Receive) {
        debug_log("Received RP1210_Set_Message_Receive command. Acknowledging.");
    } else if (nCommandNumber == RP1210_Set_All_Filters_States_to_Discard) {
        queueHead = 0;
        queueTail = 0;
        debug_log("Queue cleared by RP1210_Set_All_Filters_States_to_Discard command.");
    }
}