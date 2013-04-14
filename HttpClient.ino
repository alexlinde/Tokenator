#include <SPI.h>
#include <Dhcp.h>
#include <Dns.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>
#include <NBEthernet.h>
#include <util.h>
#include <EEPROM.h>
#include <TrueRandom.h>
#include <String.h>

void bodyComplete(String &aData);

const char* kUserAgent = "Arduino/2.0";
const char* kContentLengthPrefix = "Content-Length: ";
const char* kTransferEncodingPrefix = "Transfer-Encoding: ";
const char* kConnectionPrefix = "Connection: ";
const char* statusPrefix = "HTTP/*.* ";
const char* kMethodGET = "GET";
const char* kMethodPOST = "POST";

const int MAX_RESPONSE = 128;

typedef enum {
    eNothing,
    eDhcpSent,
    eIdle,
    eRequestStarted,
    eRequestSent,
    eReadingStatusCode,
    eStatusCodeRead,
    eReadingHeader,
    eSkipToEndOfHeader,
    eLineStartingCRFound,
    eReadChunk,
    eChunkStartingCRFound,
    eChunkSkipToEnd,
    eReadingBody,
    eComplete
} tHttpState;

EthernetClient iClient;
tHttpState iHttpState = eNothing;
uint16_t iStatusCode;
uint16_t iContentLength;
uint16_t iBodyLengthConsumed;
const char* statusPtr;
String iServerName;
String iURLPath;
String iBuffer;
const char* iMethod;
String iData;
boolean iChunked;
uint16_t iChunkLength;
boolean iKeepAlive;

byte mac[6] = { 0x90, 0xA2, 0xDA, 0x00, 0x00, 0x00 };
void setupMac() {
 if (EEPROM.read(1) == '#') {
    for (int i = 3; i < 6; i++) {
      mac[i] = EEPROM.read(i);
    }
  } else {
    for (int i = 3; i < 6; i++) {
      mac[i] = TrueRandom.randomByte();
      EEPROM.write(i, mac[i]);
    }
    EEPROM.write(1, '#');
  }
}

// todo: does not deal with chunked transfer encoding
int startRequest(const char* aServerName, const uint16_t aPort, const char* aURLPath, const char* aMethod, const char* aData) {
  if (eIdle != iHttpState)
    return HTTP_ERROR_API;

  iURLPath = aURLPath;
  iMethod = aMethod;
  iData = aData;
  
  if (iClient.connected() && iKeepAlive) {
//    Serial.println("still connected..");      
    // should also check port
    if (iServerName.equals(String(aServerName))) {
      // keep-alive
//      Serial.println("sending headers on same connection");      
      sendHeaders();
      statusPtr = statusPrefix;
      iStatusCode = 0;
      iHttpState = eRequestSent;
      iBodyLengthConsumed = 0;
      iContentLength = -1;
      return HTTP_SUCCESS;
    } else {
      iClient.stop();
    }
  }

  iServerName = aServerName;
  
  // todo: we should keep-alive    
  if (!iClient.initConnection(aServerName, aPort)) {
    return HTTP_ERROR_CONNECTION_FAILED;
  }
  
  iHttpState = eRequestStarted;
  return HTTP_SUCCESS;
}

boolean isReady() {
  return (iHttpState == eIdle);
}

boolean isComplete() {
    return (iHttpState == eComplete);
}

void sendHeaders() {
  iClient.print(iMethod);
  iClient.print(" ");
  iClient.print(iURLPath);
  iClient.println(" HTTP/1.1");
  iClient.print("Host: ");
  iClient.println(iServerName);
  iClient.print("User-Agent: ");
  iClient.println(kUserAgent);
  if (iMethod == kMethodPOST) {
    iClient.println("Content-Type: text/plain");
    iClient.print("Content-Length: ");
    iClient.println(iData.length());
  }
  iClient.println("Connection: keep-alive");
  iClient.println();
  if (iMethod == kMethodPOST) {
    iClient.println(iData);
  }
}

int read() {
    int ret = iClient.read();
    if (ret >= 0) {
//      Serial.print((char)ret);
      if (iHttpState == eReadingBody) {
        iBodyLengthConsumed++;
      }
    }
    return ret;
}

boolean endOfBodyReached()
{
    if ((iHttpState == eReadingBody) && (iContentLength != -1))
    {
        // We've got to the body and we know how long it will be
        return (iBodyLengthConsumed >= iContentLength);
    }
    return false;
}

int available() {
  return iClient.available();
}

int pollHttp() {  
  int result;
  switch(iHttpState) {
    case eNothing:
      setupMac();
      Ethernet.begin(mac);
      iHttpState = eDhcpSent;
      return HTTP_SUCCESS;
    case eDhcpSent:
      result = Ethernet.initialized();
      // 0 = still going, 1 = done, 2 = error
      if (1 == result) {      
        // done
        iKeepAlive = false;
        iHttpState = eIdle;
//        Serial.print("obtained address: ");
//        Serial.println(Ethernet.localIP());
        return HTTP_SUCCESS;
      } else if (2 == result) {
        Serial.println("DHCP failed");
        iHttpState = eNothing;
        return HTTP_ERROR_CONNECTION_FAILED;
      }
      // still going
      return HTTP_SUCCESS;
    case eIdle:
      // todo: check if dhcp is still current      
      Serial.println("idle");
      return HTTP_SUCCESS;
    case eRequestStarted:
//      Serial.println("eRequestStarted");
      result = iClient.finishedConnecting();
      if (1 == result) {
          sendHeaders();
          iHttpState = eRequestSent;
          statusPtr = statusPrefix;
          iStatusCode = 0;
          return HTTP_SUCCESS;
      } else if (0 != result) {
          iHttpState = eIdle;
          return HTTP_ERROR_CONNECTION_FAILED;
      }
      // still waiting
      return HTTP_SUCCESS;
    default:
      break;
  }
    
  // todo: check timeout

  // get next character
  while (available()) {
    char c = read();
      
    switch (iHttpState) {
      case eRequestSent:
        if ( (c == '\r') || (c == '\n') ) {
          // consume - this is between keepalives
        } else if ( (*statusPtr == '*') || (*statusPtr == c) ) {
          // This character matches, just move along
          statusPtr++;
          if (*statusPtr == '\0') {
            // We've reached the end of the prefix
            iHttpState = eReadingStatusCode;
          }
        } else {
          Serial.println("failed to match http");
          return HTTP_ERROR_INVALID_RESPONSE;
        }
        break;
      case eReadingStatusCode:
        if (isdigit(c)) {
          // This assumes we won't get more than the 3 digits we
          // want
          iStatusCode = iStatusCode*10 + (c - '0');
        } else {
          // We've reached the end of the status code
          // We could sanity check it here or double-check for ' '
          // rather than anything else, but let's be lenient
          iHttpState = eStatusCodeRead;
        }
        break;
      case eStatusCodeRead:
        if (c == '\n') {
          iHttpState = eReadingHeader;
          iBuffer = String();
          iChunked = false;
          iKeepAlive = false;
        }
        break;
      case eReadingHeader:
        if (c == '\r') {
          // end of header, match
          if (0 == iBuffer.length()) {
            // probably end of headers
            iHttpState = eLineStartingCRFound;
          } else {
            if (iBuffer.startsWith(kContentLengthPrefix)) {
              iBuffer = iBuffer.substring(iBuffer.indexOf(':') + 2);
              iContentLength = iBuffer.toInt();
  //            Serial.print("content size=");
  //            Serial.println(iContentLength);
            } else if (iBuffer.startsWith(kTransferEncodingPrefix)) {
              iChunked = (toupper(iBuffer.charAt(iBuffer.indexOf(':') + 2)) == 'C');
              iChunkLength = 0;
            } else if (iBuffer.startsWith(kConnectionPrefix)) {
              iKeepAlive = (toupper(iBuffer.charAt(iBuffer.indexOf(':') + 2)) == 'K');
            }
            iHttpState = eSkipToEndOfHeader;
          }            
        } else {
          iBuffer += c;
        }
        break;
      case eLineStartingCRFound:
        if (c == '\n') {
          iHttpState = (iChunked ? eReadChunk : eReadingBody);
          iBuffer = String();
        }
        break;
      case eSkipToEndOfHeader:
        if (c == '\n') {
          iBuffer = String();
          iHttpState = eReadingHeader;
        }
        break;
      case eReadChunk:
        if (isalnum(c)) {
          iChunkLength = iChunkLength << 4;
          if(c >= '0' && c <= '9')
            iChunkLength += (c - '0');
          else 
            iChunkLength += (c - 'A' + 10);
        } else {
          iHttpState = eChunkStartingCRFound;
//          Serial.print("chunk size=");
//          Serial.println(iContentLength);
        }
        break;
      case eChunkStartingCRFound:
        if (c == '\n') {
          if (0 == iChunkLength) {
            // this was the last chunk
//Serial.println("end of last chunk");
            iHttpState = eIdle;
            bodyComplete(iBuffer);
          } else {
            iHttpState = eReadingBody;
          }
        }
        break;
      case eChunkSkipToEnd:
        if (c == '\n') {
          // start next chunk
          iHttpState = eReadChunk;
        }
        break;
      case eReadingBody:
        if (iBodyLengthConsumed < MAX_RESPONSE) {
          iBuffer += c;
        }
        if (iChunked) {
          iChunkLength--;
          if (!iChunkLength) {
            iHttpState = eChunkSkipToEnd;
          }
        } else if (endOfBodyReached()) {
//          Serial.println("endOfBodyReached");
          // process response - there may be more than one chunk.. but for now, just ignore
          if (!iKeepAlive) {
            iClient.stop();
          }
          iHttpState = eIdle;
          bodyComplete(iBuffer);
        }          
        break;
    }
  }
  
  if (!iClient.connected() && eIdle != iHttpState) {
    iHttpState = eIdle;
    iClient.stop();
    // hackhackhack
    resetSocket();
  }
  
  return HTTP_SUCCESS;
}

