#ifndef CONNECTOR_H
#define CONNECTOR_H

extern int connSocket;

void signalHandlerConnector(int signalKey);
void connectSocket(void);
void performConnection(char gameID[], int playerNo);
void connectorCleanUp(void);

#endif