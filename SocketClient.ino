

int startRequest(const char* aServerName, const uint16_t aPort, const char* aURLPath, const char* aMethod, const char* aData);
boolean isReady();
int pollHttp();

const char* kHost = "salty-ridge-8671.herokuapp.com";
const uint16_t kPort = 80;
//const char* kHost = "192.168.1.26";
//const uint16_t kPort = 5000;
const char* kBasePath = "/socket.io/1/";

typedef enum {
    eStarting,
    eHandshakeSent,
    eTransportSent,
    eEndpointSent,
    eMessage,
    ePolling
} tSocketState;

tSocketState iSocketState = eStarting;

static char endpoint[128];

void bodyComplete(String &aData) {
  switch (iSocketState) {
    case eHandshakeSent:
    {
//      Serial.println("eHandshakeSent");
      iSocketState = eTransportSent;
      String s = kBasePath;
      s += "xhr-polling/";
      s += aData.substring(0,aData.indexOf(':'));
      s.toCharArray(endpoint,128);
//      Serial.println(endpoint);
      startRequest(kHost,kPort,endpoint,kMethodGET,0);
      break;
    }
    case eTransportSent:
      Serial.println("eTransportSent");
      // 1:: - indicates connected successfully to the transport
      if (aData.charAt(0) == '1') {
        iSocketState = eEndpointSent;
        // now connect to endpoint
        startRequest(kHost,kPort,endpoint,kMethodPOST,"1::/arduino");
      } else {
        // errrrrrror
        Serial.println("ERROR");
        resetSocket();
      }
      break;
    case eEndpointSent:
      // 1:: - indicates connected successfully to the transport
      Serial.println("eEndpointSent");
      if (aData.charAt(0) == '1') {
        // success
        iSocketState = eMessage;
        // send first poll
        startRequest(kHost,kPort,endpoint,kMethodGET,0);
      } else {
        Serial.println("ERROR2");
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
      switch (aData.charAt(0)) {
        case '5':
        {
          // just assume we have one parameter enclosed in []
          Serial.println(aData);
          int start = aData.indexOf('[');
          int end = aData.indexOf(']');
          if (-1 != start && -1 != end) {
            String time = aData.substring(start+1,end);
            addTime(time.toInt());
          }
          break;
        }
        case '1':
        {
          // success - all good
          break;
        }
        case '8':
        {
          // noop, to prevent connection drop
          break;
        }
        default:
        {
          Serial.println("RECEIVED UNKNOWN MESSAGE TYPE");
          Serial.println(aData);
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
    iSocketState = eStarting;
    // backoff for a bit
    backoffCounter = 30;
}  

static boolean doPoll = 0;
int pollSocket() {
  if (pollHttp() != HTTP_SUCCESS) {
    resetSocket();
  }
    
  if (isReady()) {  
    switch (iSocketState) {
      case eStarting:
        if (backoffCounter) {
          backoffCounter--;
        } else {
          Serial.println("eStarting");
          iSocketState = eHandshakeSent;
          startRequest(kHost,kPort,kBasePath,kMethodGET,0);
        }
        break;
      case ePolling:
        if (doPoll) {
          // poll
          Serial.println("ePolling");
          startRequest(kHost,kPort,endpoint,kMethodGET,0);
        } else {
          // send update
          Serial.println("Sending update");
          // 5::/arduino:{"name":"update","args":[60]}
          char s[48];
          sprintf(s,"5::/arduino:{\"name\":\"update\",\"args\":[%d]}",timeRemaining);
          startRequest(kHost,kPort,endpoint,kMethodPOST,s);
          iSocketState = eMessage;
        }
        doPoll = !doPoll;
        break;
      default:
        break;
    }
  }
}
