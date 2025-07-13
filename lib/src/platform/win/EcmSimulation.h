#ifndef ECM_SIMULATION_H
#define ECM_SIMULATION_H

#include "OpenRP1210/RP1210.h"

short Sim_ClientConnect(short nClientID);
void Sim_ClientDisconnect(short nClientID);
void Sim_SendMessage(short nClientID, char *fpchClientMessage, short nMessageSize);
short Sim_ReadMessage(short nClientID, char *fpchAPIMessage, short nBufferSize, short nBlockOnRead);
void Sim_SendCommand(short nCommandNumber, short nClientID, char* fpchClientMessage);

#endif // ECM_SIMULATION_H
