#include <SPI.h>
#include <Dhcp.h>
#include <Dns.h>
#include <EthernetClient.h>
#include <EthernetServer.h>
#include <EthernetUdp.h>
#include <NBEthernet.h>
#include <util.h>

const char* kUserAgent = "Arduino/2.0";
const char* kContentLengthPrefix = "Content-Length: ";
const char* statusPrefix = "HTTP/*.* ";

// todo: does not deal with chunked transfer encoding

void initHttp() {
  iState = eIdle;
  iStatusCode = 0;
  iContentLength = 0;
  iBodyLengthConsumed = 0;
  iContentLengthPtr = 0;
}

int startRequest(const char* aServerName, const char* aURLPath) {
    if (eIdle != iState)
        return HTTP_ERROR_API;
    
    if (!iClient.initConnection(aServerName, 80))
        return HTTP_ERROR_CONNECTION_FAILED;
    
    iServerName = aServerName;
    iURLPath = aURLPath;
    iState = eRequestStarted;
    return HTTP_SUCCESS;
}

boolean isComplete() {
    return (iState == eComplete);
}

void sendHeaders() {
    iClient.print("GET ");
    iClient.print(iURLPath);
    iClient.println(" HTTP/1.1");
    iClient.print("Host: ");
    iClient.println(iServerName);
    iClient.print("User-Agent: ");
    iClient.println(kUserAgent);
    iClient.println("Connection: close");
    iClient.println();
}

int read() {
    int ret = iClient.read();
    if (ret >= 0) {
//      Serial.print((char)ret);
      if (iState == eReadingBody && iContentLength > 0) {
        // We're outputting the body now and we've seen a Content-Length header
        // So keep track of how many bytes are left
        iBodyLengthConsumed++;
      }
    }
    return ret;
}

int available() {
  return iClient.available();
}

int poll() {
    if (eRequestStarted == iState) {
      int result = iClient.finishedConnecting();
      if (1 == result) {
          sendHeaders();
          iState = eRequestSent;
          statusPtr = statusPrefix;
          iStatusCode = 0;
//          Serial.println("sent headers");
          return HTTP_SUCCESS;
      } else if (0 != result) {
          iState = eComplete;
          return HTTP_ERROR_CONNECTION_FAILED;
      }
      // still waiting
      return HTTP_SUCCESS;
    }
    
    // todo: check timeout

    // get next character
    while (available() && iState != eReadingBody) {
      char c = read();
        
      switch (iState) {
        case eRequestSent:
          if ( (*statusPtr == '*') || (*statusPtr == c) ) {
            // This character matches, just move along
            statusPtr++;
            if (*statusPtr == '\0') {
              // We've reached the end of the prefix
              iState = eReadingStatusCode;
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
            iState = eStatusCodeRead;
          }
          break;
        case eStatusCodeRead:
          if (c == '\n') {
            iContentLengthPtr = kContentLengthPrefix;
            iState = eReadingHeaders;
          }
          break;
        case eReadingHeaders:
          if (*iContentLengthPtr == c) {
            // This character matches, just move along
            iContentLengthPtr++;
            if (*iContentLengthPtr == '\0') {
              // We've reached the end of the prefix
//              Serial.println("matched content length");
              iState = eReadingContentLength;
              // Just in case we get multiple Content-Length headers, this
              // will ensure we just get the value of the last one
              iContentLength = 0;
            }
          } else if (c == '\r') {
            // probably end of headers
            iState = eLineStartingCRFound;
          } else {
            // not correct header, skip
            iState = eSkipToEndOfHeader;
          }
          break;
        case eReadingContentLength:
          if (isdigit(c)) {
            iContentLength = iContentLength*10 + (c - '0');
          } else {
            // We've reached the end of the content length
            // We could sanity check it here or double-check for "\r\n"
            // rather than anything else, but let's be lenient
            iState = eSkipToEndOfHeader;
//            Serial.println("read content length");
          }
          break;
        case eLineStartingCRFound:
          if (c == '\n') {
            iState = eReadingBody;
//            Serial.println("end of headers, now body");
          }
          break;
        case eSkipToEndOfHeader:
          if (c == '\n') {
            iContentLengthPtr = kContentLengthPrefix;              
            iState = eReadingHeaders;
          }
          break;
      }
  }
  return HTTP_SUCCESS;
}

