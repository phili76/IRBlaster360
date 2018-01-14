#include <WebSocketsServer.h>

void startWebSocket(); // Start a WebSocket server
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght); // When a WebSocket message is received
void webservernotfound();
bool handleFileRead(String path); // send the right file to the client (if it exists)
void handleFileUpload(); // upload a new file to the SPIFFS
void webserverupload();
void getSPIFFScontent();
String formatBytes(size_t bytes); // convert sizes in bytes to KB and MB
void printDirectory();
void webgetSPIFFS();
void handleDelete();
void handleCreate();
void returnFail(String msg);
void returnOK();
