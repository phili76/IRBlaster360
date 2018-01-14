#include <FS.h>   // Include the SPIFFS library
#include <WebSocketsServer.h>

File fsUploadFile;              // a File object to temporarily store the received file


void startWebSocket() { // Start a WebSocket server
  webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  Serial.println("WebSocket server started.");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:                     // if new text data is received
      Serial.printf("[%u] get Text: %s\n", num, payload);
      if (payload[0] == 'P') {
        webSocket.sendTXT(num, "Pong");
      } else if (payload[0] == '1') {
      } else if (payload[0] == '2') {
      }
      break;
  }
}

/*
*
*   Webserver get Files from SPIFFS
*
*/

void webservernotfound()
{
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });
}

String getContentType(String filename) // convert the file extension to the MIME type
{
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path)  // send the right file to the client (if it exists)
{
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";           // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){  // If the file exists, either as a compressed archive, or normal
    if(SPIFFS.exists(pathWithGz))                          // If there's a compressed version available
      path += ".gz";                                         // Use the compressed version
    File file = SPIFFS.open(path, "r");                    // Open the file
    if (server.hasArg("download")) contentType = "application/octet-stream";
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}

void handleFileUpload() // upload a new file to the SPIFFS
{
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if(upload.status == UPLOAD_FILE_END) {
    if(fsUploadFile) {                                    // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      Serial.print("handleFileUpload Size: ");
      Serial.println(upload.totalSize);
      server.sendHeader("Location","/success.html");      // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

void getSPIFFScontent()
{
  SPIFFS.begin();                             // Start the SPI Flash File System (SPIFFS)
  Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
}

String formatBytes(size_t bytes) // convert sizes in bytes to KB and MB
{
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

void printDirectory()
{
  if(!server.hasArg("dir")) return returnFail("BAD ARGS");
  String path = server.arg("dir");
  if(path != "/" && !SPIFFS.exists((char *)path.c_str())) return returnFail("BAD PATH");
  Dir dir = SPIFFS.openDir((char *)path.c_str());
  path = String();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  server.sendContent("[");
  int cnt = 0;
  while (dir.next()) {                      // List the file system contents
    String fileName = (dir.fileName()).substring(1);
    size_t fileSize = dir.fileSize();
    String output;
    if (cnt > 0)
      output = ',';

    output += "{\"type\":\"";
    output += (false) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += fileName;
    output += "\"";
    output += "}";
    cnt++;
    server.sendContent(output);
 }
 server.sendContent("]");
}

void webserverupload()
{
  server.on("/uploadfile", HTTP_GET, []()                   // if the client requests the upload page
  {
    if (!handleFileRead("/upload.html"))                // send it if it exists
      server.send(200, "text/html", "<html><body><form method=\"post\" enctype=\"multipart/form-data\">"
      "<input type=\"file\" name=\"name\"><input class=\"button\" type=\"submit\" value=\"Upload\"></form></body></html>");
  });

  server.on("/uploadfile", HTTP_POST,                       // if the client posts to the upload page
    [](){ server.send(200); },                          // Send status 200 (OK) to tell the client we are ready to receive
    handleFileUpload                                    // Receive and save the file
  );
}

void webgetSPIFFS()
{

  server.on("/list", printDirectory);                   // if the client requests the upload page
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on("/edit", HTTP_POST, [](){ returnOK(); }, handleFileUpload);
  server.on("/edit", HTTP_GET, [](){
    server.sendHeader("Location","/edit.html");      // Redirect the client to the success page
    server.send(303);
  });

}

void handleDelete()
{
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/") {
    returnFail("BAD PATH");
    return;
  }
  (SPIFFS.remove(path)) ? returnOK():returnFail("File not deleted");
}

void handleCreate()
{
  if(server.args() == 0) return returnFail("BAD ARGS");
  String path = server.arg(0);
  if(path == "/") {
    returnFail("BAD PATH");
    return;
  }

  if(path.indexOf('.') > 0){
    File file = SPIFFS.open((char *)path.c_str(), "w");
    if(file){
      file.write(0);
      file.close();
    }
  }
  returnOK();
}

void returnFail(String msg)
{
  server.send(500, "text/plain", msg + "\r\n");
}

void returnOK()
{
  server.send(200, "text/plain", "");
}
