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

//#define TRACE_RESPONSE true

void bodyComplete(uint16_t statusCode, const char *aData);

const char* kUserAgent = "Arduino/2.0";
const char* kContentLengthPrefix = "Content-Length: ";
const char* kTransferEncodingPrefix = "Transfer-Encoding: ";
const char* kConnectionPrefix = "Connection: ";
const char* statusPrefix = "HTTP/*.* ";
const char* kMethodGET = "GET";
const char* kMethodPOST = "POST";

const int MAX_RESPONSE = 48;
const int MAX_POST = 48;

typedef enum {
    eNothing,
    eDhcpSent,
    eIdle,
    eMaintain,
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
uint16_t iPort;
const char* statusPtr;
char* ptr;
char iServerName[64];
char iURLPath[32];
char iBuffer[MAX_RESPONSE+1];
const char* iMethod;
char iData[MAX_POST+1];
boolean iChunked;
uint16_t iChunkLength;
boolean iKeepAlive;
uint8_t timeout = 0;

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

  strcpy(iURLPath,aURLPath);
  iMethod = aMethod;
  if (aData) {
    strcpy(iData,aData);
  } else {
    iData[0] = 0;
  }
  statusPtr = statusPrefix;
  iStatusCode = 0;
  iBodyLengthConsumed = 0;
  iContentLength = -1;
  timeout = 30;
  
  if (iClient.connected() && iKeepAlive) {
    // should also check port
    if (strcmp(iServerName,aServerName) == 0 && aPort == iPort) {
      // keep-alive
      sendHeaders();
      iHttpState = eRequestSent;
      return HTTP_SUCCESS;
    } else {
      iClient.stop();
    }
  }

  strcpy(iServerName,aServerName);
  iPort = aPort;
  
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
  iClient << iMethod << ' ' << iURLPath << " HTTP/1.1" << endl;
  iClient << "Host: " << iServerName;
  if (80 != iPort) {
    iClient << ':' << iPort;
  }  
  iClient << endl;
  iClient << "User-Agent: " << kUserAgent << endl;
  if (iMethod == kMethodPOST) {
    iClient << "Content-Type: text/plain" << endl << kContentLengthPrefix << strlen(iData) << endl;
  }
  iClient << kConnectionPrefix << "keep-alive" << endl << endl;
  if (iMethod == kMethodPOST) {
    iClient << iData;
  }
}

int read() {
    int ret = iClient.read();
    if (ret >= 0) {
#ifdef TRACE_RESPONSE      
      Serial << (char)ret;
#endif
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
        Serial << "DHCP failed" << endl;
        iHttpState = eNothing;
        return HTTP_ERROR_CONNECTION_FAILED;
      }
      // still going
      return HTTP_SUCCESS;
    case eRequestStarted:
      Serial << "eRequestStarted" << endl;
      result = iClient.finishedConnecting();
      if (1 == result) {
          sendHeaders();
          iHttpState = eRequestSent;
          return HTTP_SUCCESS;
      } else if (0 != result) {
          Serial << "Connection failed" << endl;
          iHttpState = eIdle;
          return HTTP_ERROR_CONNECTION_FAILED;
      }
      // still waiting
      return HTTP_SUCCESS;
    case eIdle:
      if (Ethernet.maintainNeeded() != DHCP_CHECK_NONE) {
        iHttpState = eMaintain;
      }
      break;
    case eMaintain:
      if (Ethernet.maintainFinished()) {
        iHttpState = eIdle;
      }
      break;    
    default:
      break;
  }
        
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
          Serial << "failed to match http" << endl;
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
//          Serial << "HTTP_STATUS " << iStatusCode << endl;
          iHttpState = eReadingHeader;
          ptr = iBuffer;
          iChunked = false;
          iKeepAlive = false;
        }
        break;
      case eReadingHeader:
        if (c == '\r') {
          // end of header, match
          if (ptr == iBuffer) {
            // probably end of headers
            iHttpState = eLineStartingCRFound;
          } else {
//            Serial << "CHECKING: " << iBuffer << endl;
            if (strncmp(iBuffer,kContentLengthPrefix,16) == 0) {
              iContentLength = atoi(iBuffer + 16);
//              Serial << "content size " << iContentLength << endl;
            } else if (strncmp(iBuffer,kTransferEncodingPrefix,19) == 0) {
              iChunked = (toupper(iBuffer[19]) == 'C');
              iChunkLength = 0;
//              Serial << "chunked " << iChunked << endl;
            } else if (strncmp(iBuffer,kConnectionPrefix,12) == 0) {
              iKeepAlive = (toupper(iBuffer[12]) == 'K');
//              Serial << "keep alive " << iKeepAlive << endl;
            }
            iHttpState = eSkipToEndOfHeader;
          }            
        } else {
          if ((ptr-iBuffer) < MAX_RESPONSE) {
            *ptr++ = c;
            *ptr = 0;
          }
        }
        break;
      case eLineStartingCRFound:
        if (c == '\n') {
          iHttpState = (iChunked ? eReadChunk : eReadingBody);
          ptr = iBuffer;
        }
        break;
      case eSkipToEndOfHeader:
        if (c == '\n') {
          ptr = iBuffer;
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
//          Serial << "starting chunk size=" << iChunkLength << endl;
        }
        break;
      case eChunkStartingCRFound:
        if (c == '\n') {
          if (0 == iChunkLength) {
            // this was the last chunk
            // todo: there can optionally be more headers followed by a blank line..
            // ignore for now as we just absorb blank line at the start
//            Serial << "end of last chunk" << endl;
            iHttpState = eIdle;
            bodyComplete(iStatusCode, iBuffer);
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
          *ptr++ = c;
          *ptr = 0;
        }
        if (iChunked) {
          iChunkLength--;
          if (!iChunkLength) {
            iHttpState = eChunkSkipToEnd;
          }
        } else if (endOfBodyReached()) {
//          Serial << "endOfBodyReached" << endl;
          // process response - there may be more than one chunk.. but for now, just ignore
          if (!iKeepAlive) {
            iClient.stop();
          }
          iHttpState = eIdle;
          bodyComplete(iStatusCode, iBuffer);
        }          
        break;
    }
  }
  
  if (eIdle != iHttpState) {
    if (timeout) timeout--;
    if (!iClient.connected() || !timeout) {
      iHttpState = eIdle;
      iKeepAlive = false;
      iClient.stop();
      // hackhackhack
      resetSocket();
    }
  }
  
  return HTTP_SUCCESS;
}

