

int startRequest(const char* aServerName, const uint16_t aPort, const char* aURLPath, const char* aMethod, const char* aData);
boolean isReady();
int pollHttp();

const char* kHost = "salty-ridge-8671.herokuapp.com";
const uint16_t kPort = 80;
//const char* kHost = "192.168.1.28";
//const uint16_t kPort = 5000;
const char* kBasePath = "/socket.io/1/";
boolean iEndpointConnected = false;

typedef enum {
    eStarting,
    eHandshakeSent,
    eTransportSent,
    eEndpointSent,
    eMessage,
    ePolling
} tSocketState;

tSocketState iSocketState = eStarting;

static char sessionid[21];
static char endpoint[128];

boolean isEndpointConnected() { return iEndpointConnected; }

void bodyComplete(uint16_t statusCode, const char *aData) {
  switch (iSocketState) {
    case eHandshakeSent:
    {
      Serial << "eHandshakeSent" << endl;
      // handshake response, should be 200 ok
// SESSIONID:HEARTBEAT-TIMEOUT:CONNECTION-TIMEOUT:TRANSPORT[,TRANSPORT]*
// 4d4f185e96a7b:15:10:websocket,xhr-polling
      iSocketState = eTransportSent;

      // extract sessionid, ignore timeouts and transports for now - we're just going to poll
      char *p = sessionid;
      while (*aData && *aData != ':') {
        *p++ = *aData++;
      }

      // build uri to open new connection
      PString pe(endpoint, sizeof(endpoint));
      pe << kBasePath << "xhr-polling/" << sessionid ; // << "?t=" << millis();
      startRequest(kHost,kPort,endpoint,kMethodGET,0);
      break;
    }
    case eTransportSent:
      Serial << "eTransportSent" << endl;
      // 1:: - indicates connected successfully to the transport
      if (*aData == '1') {
        iSocketState = eEndpointSent;
        // now connect to endpoint
//        PString pe(endpoint, sizeof(endpoint));
//        pe << kBasePath << "xhr-polling/" << sessionid << "?t=" << millis();
        startRequest(kHost,kPort,endpoint,kMethodPOST,"1::/arduino");
      } else {
        // errrrrrror
        Serial << "ERROR" << endl;
        resetSocket();
      }
      break;
    case eEndpointSent:
      // 1:: - indicates connected successfully to the transport
      Serial << "eEndpointSent" << endl;
      if (*aData == '1') {
        // success
        iSocketState = ePolling;
      } else {
        Serial << "ERROR2" << endl;
        resetSocket();
      }       
      break;
    case eMessage:
    {
//      Serial.println("eMessage");
      // check for an event
      // 1 - success
      // 5::/arduino:{"name":"add","args":[600]} - event
      // 8 - noop
//      Serial.println(aData);
      switch (*aData) {
        case '5':
        {
          // just assume we have one parameter enclosed in []
          Serial << aData << endl;
          char * p = strchr(aData, '[');
          if (p) {
            int time = atoi(p+1);
            addTime(time);
          }
          break;
        }
        case '1':
        {
          // success - all good
          iEndpointConnected = true;
          break;
        }
        case '8':
        {
          // noop, to prevent connection drop
          break;
        }
        default:
        {
          Serial << "RECEIVED UNKNOWN MESSAGE TYPE" << endl << aData << endl;
          break;
        }
      }
      iSocketState = ePolling;
      break;
    }
  }
}

static uint8_t backoffCounter = 0;
void resetSocket() {
    iEndpointConnected = false;
    iSocketState = eStarting;
    // backoff for a bit
    backoffCounter = 30;
}  

static boolean doPoll = 0;
static uint16_t lastTime = -1;
int pollSocket() {
  if (pollHttp() != HTTP_SUCCESS) {
    resetSocket();
    iEndpointConnected = false;
  }
    
  if (isReady()) {  
    switch (iSocketState) {
      case eStarting:
        if (backoffCounter) {
          backoffCounter--;
        } else {
          Serial << "eStarting" << endl;
          iSocketState = eHandshakeSent;
          startRequest(kHost,kPort,kBasePath,kMethodGET,0);
        }
        break;
      case ePolling:
        if (doPoll) {
          // poll
//          Serial << "ePolling - poll" << endl;
//          PString pe(endpoint, sizeof(endpoint));
//          pe << kBasePath << "xhr-polling/" << sessionid << "?t=" << millis();
          startRequest(kHost,kPort,endpoint,kMethodGET,0);
          iSocketState = eMessage;
        } else {
          if (timeRemaining != lastTime) {
//            Serial << "ePolling - sending time" << endl;
            // send update
    //          Serial.println("Sending update");
            // 5::/arduino:{"name":"update","args":[60]}
            char s[48];
            PString ps(s,sizeof(s));
            ps << "5::/arduino:{\"name\":\"update\",\"args\":[" << timeRemaining << "]}";

//            PString pe(endpoint, sizeof(endpoint));
//            pe << kBasePath << "xhr-polling/" << sessionid << "?t=" << millis();
            startRequest(kHost,kPort,endpoint,kMethodPOST,s);

            iSocketState = eMessage;
            lastTime = timeRemaining;
          }
        }
        doPoll = !doPoll;
        break;
      default:
        break;
    }
  }
}
