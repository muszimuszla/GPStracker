#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <ArduinoJWT.h>
#include <sha256.h>

#define DEBUG
// This fields needs to be specified
#define SECRET "..."
#define ID "..."
char URL[98] = "...";

#define LED_RED 11
#define LED_GREEN 10
#define LED_BLUE 9
#define PWR_KEY 17
#define DTR 8
#define TX 1
#define RX 0
#define DEBUG_TX 13
#define DEBUG_RX 12

#ifdef __arm__
extern "C" char* sbrk(int incr);
#else 
extern char *__brkval;
#endif 

int freeMemory() {
	char top;
#ifdef __arm__
	return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
	return &top - __brkval;
#else 
	return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif
}

struct gpsInfo_s {
	String gpsRunStatus;
	String fixStatus;
	String timeDate;
	String latitude;
	String longitude;
	String altitude;
	String speedOverGround;
	String courseOverGround;
};

void turnOnSim808();
void turnOnGNSS();
void turnOnAntenne();
void turnOffAntenne();
void getLocation();
void sendGPSInfo();
void turnOnInternetConnection();
void lightLED(uint8_t R, uint8_t G, uint8_t B, uint16_t time = 0);
void sendCommand(String req, String res = "OK", bool addinfo = false, char* ptr = NULL);
void createToken();

char JWToken[180]; 
gpsInfo_s gpsInfo;
const size_t capacity = JSON_OBJECT_SIZE(7) + 110;

uint32_t lastToken = 0;
uint32_t currenttime = 0;
uint32_t checkToken = 0;

#ifdef DEBUG
SoftwareSerial debugSerial(DEBUG_RX, DEBUG_TX);
#endif


// The setup() function runs once each time the micro-controller starts
void setup()
{
#ifdef DEBUG
	debugSerial.begin(9600);
#endif 

	turnOnSim808();
	delay(10000);
	while (Serial.available()) Serial.read();
	turnOnGNSS();
	turnOnAntenne();
	turnOnInternetConnection();

	//LED RED
	lightLED(100, 0, 0, 2000);		
	//LED GREEN				
	lightLED(0, 100, 0, 2000);		
	//LED BLUE				
	lightLED(0, 0, 100, 2000);						
}

// Add the main program code into the continuous loop() function
void loop()
{
	getLocation();

	if (gpsInfo.fixStatus == "1") {
		currenttime = millis();
		if (currenttime - lastToken > 600000) createToken();
		if (lastToken == 0) createToken();
#ifdef DEBUG
		debugSerial.println(F("SENDING DATA WILL BE: "));
		debugSerial.print(F("dataToSend: "));
		debugSerial.println(dataToSend);
		debugSerial.print(F("dataToSendLength: "));
		debugSerial.println(dataToSendLength);
		debugSerial.print(F("JWToken: "));
		debugSerial.println(JWToken);
#endif
		sendGPSInfo();
		lightLED(0, 100, 0, 1000);
		delay(30000);
	}
	delay(5000);
}

// Get location information
void getLocation() {
	while (Serial.available()) Serial.read();
	Serial.println(F("AT+CGNSINF"));   
	Serial.readStringUntil(':');

	gpsInfo.gpsRunStatus = Serial.readStringUntil(',');
	gpsInfo.fixStatus = Serial.readStringUntil(',');
	gpsInfo.timeDate = Serial.readStringUntil(',');
	gpsInfo.latitude = Serial.readStringUntil(',');
	gpsInfo.longitude = Serial.readStringUntil(',');
	gpsInfo.altitude = Serial.readStringUntil(',');
	gpsInfo.speedOverGround = Serial.readStringUntil(',');
	gpsInfo.courseOverGround = Serial.readStringUntil(',');

	if (Serial.find("OK")) {
		lightLED(0, 50, 50, 1000);
#ifdef DEBUG
		debugSerial.println(F("GPS info OK"));
#endif 
	}
	else {
		lightLED(100, 0, 0, 1000);
#ifdef DEBUG
		debugSerial.println(F("GPS info not OK"));
#endif 
	}

}
// Send GPS info to server
void sendGPSInfo() {	
	StaticJsonDocument<capacity> jsonDoc;
	jsonDoc["Bearer"] = (const char*)JWToken;
	jsonDoc["tim"] = gpsInfo.timeDate;
	jsonDoc["lat"] = gpsInfo.latitude;
	jsonDoc["lng"] = gpsInfo.longitude;
	jsonDoc["alt"] = gpsInfo.altitude;
	jsonDoc["spd"] = gpsInfo.speedOverGround;
	jsonDoc["cog"] = gpsInfo.courseOverGround;

#ifdef DEBUG
	debugSerial.print(F("dataToSend JSON: "));
	serializeJson(jsonDoc,debugSerial);
#endif

	//terminate HTTP Service
	sendCommand(F("AT+HTTPTERM"));			
	//initialize HTTP Service
	sendCommand(F("AT+HTTPINIT"));				             
	//set HTTP bearer profile indentifier
	sendCommand(F("AT+HTTPPARA=\"CID\",1"));				
	//set HTTP client URL
	sendCommand(F("AT+HTTPPARA=\"URL\",\""), "OK", true, URL);					
	//set type of information to json   
	sendCommand(F("AT+HTTPPARA=\"CONTENT\",\"application/json\""));	
	//set length and max time 							        
	sendCommand("AT+HTTPDATA=" + String(measureJson(jsonDoc)) + ",20000", "DOWNLOAD");					        
	serializeJson(jsonDoc, Serial);
	//send json  
	sendCommand("", "OK");																	     
	//set HTTP method action : POST  
	sendCommand(F("AT+HTTPACTION=1"), "ACTION");	
	//read the HTTP Server Response  											
	sendCommand(F("AT+HTTPREAD"));																 

}
// Enable Sim808 module
// by pulling down PWR_KEY for 1 sec
void turnOnSim808() {
	digitalWrite(DTR, HIGH);
	digitalWrite(PWR_KEY, LOW);
	pinMode(PWR_KEY, OUTPUT);
	delay(1200);
	pinMode(PWR_KEY, INPUT);
	Serial.begin(9600);
	pinMode(LED_RED, OUTPUT);
	pinMode(LED_GREEN, OUTPUT);
	pinMode(LED_BLUE, OUTPUT);
	digitalWrite(LED_RED, HIGH);
	digitalWrite(LED_GREEN, HIGH);
	digitalWrite(LED_BLUE, HIGH);
}
// Enable GNSS
void turnOnGNSS() {
	sendCommand(F("AT+CGNSPWR=1"));                       
}
// Enable active antenna
void turnOnAntenne() {
	sendCommand(F("AT+CGPIO=0,43,1,1"));           
}
// Disable active antenna
void turnOffAntenne() {
	sendCommand(F("AT+CGPIO=0,43,1,0"));          
}
// Enable Internet connection
void turnOnInternetConnection() {
	 //set connection type : GPRS
	sendCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\""));          
	//set access point
	sendCommand(F("AT+SAPBR=3,1,\"APN\",\"internet\""));        
	//open bearer
	sendCommand(F("AT+SAPBR=1,1"));                                       
	//query bearer
	sendCommand(F("AT+SAPBR=2,1"));                              
	//initialize HTTP Service
	sendCommand(F("AT+HTTPINIT"));         
}
//Light on LED
void lightLED(uint8_t R, uint8_t G, uint8_t B, uint16_t time) {
	uint8_t R_v = map(R, 0, 100, 255, 0);
	uint8_t G_v = map(G, 0, 100, 255, 0);
	uint8_t B_v = map(B, 0, 100, 255, 0);
	analogWrite(LED_RED, R_v);
	analogWrite(LED_GREEN, G_v);
	analogWrite(LED_BLUE, B_v);
	if (time != 0) {
		delay(time);
		digitalWrite(LED_RED, HIGH);
		digitalWrite(LED_GREEN, HIGH);
		digitalWrite(LED_BLUE, HIGH);
	}
}
// Send command to Sim808
void sendCommand(String req, String res, bool addinfo, char* ptr) {
	uint8_t errorCounter = 0;
	String strRes;

	//clean the Serial
	while (Serial.available()) Serial.read();

	//send command
	if (req != "") {
		for (unsigned int i = 0; i < req.length(); i++) {
			Serial.write(req[i]);
		}
	}
	if (addinfo == true) {
		for (unsigned int i = 0; (ptr[i]) != '\0'; i++) {
			Serial.write(ptr[i]); 
		}
		Serial.write("\"");
	}
	Serial.write("\r\n");

#ifdef DEBUG
	debugSerial.print(F("CMD to SIM: "));
	debugSerial.println(req);
#endif

	//handling response
	while (true) {
		delay(1000);
		//get response
		strRes = Serial.readString();
		//clean the Serial
		for (unsigned int i = 0; i < strRes.length(); i++) {
			Serial.read();
		}
		//if proper response is present 
		if (strRes.indexOf(res) > -1) {
#ifdef DEBUG
			debugSerial.print(F("CMD from SIM: "));
			debugSerial.println(strRes);
#endif
			lightLED(0, 0, 100, 1000);
			break;
		}
		//if response contains error
		else if (strRes.indexOf("ERROR") > -1)
		{
			errorCounter++;
#ifdef DEBUG
			debugSerial.print(F("CMD from SIM: "));
			debugSerial.println(strRes);
			debugSerial.print(F("errorCounter: "));
			debugSerial.println(errorCounter);
#endif
			//send command again
			for (unsigned int i = 0; i < req.length(); i++) {
				Serial.write(req[i]);
			}
			Serial.write("\r\n");
			lightLED(100, 0, 0, 1000);
		}
		else if (strRes.length() >= 63) {
#ifdef DEBUG
			debugSerial.print(F("CMD from SIM: "));
			debugSerial.println(strRes);
			debugSerial.println(F("*Message to long, assuming it's correct*"));
#endif
			break;
		}
		else {
#ifdef DEBUG
			debugSerial.print(F("CMD from SIM: "));
			debugSerial.println(strRes);
#endif
			errorCounter++;
			if (errorCounter > 10) {
				errorCounter = 0;
				lightLED(100, 0, 0, 1000);
				//send command again
				for (unsigned int i = 0; i < req.length(); i++) {
					Serial.write(req[i]);
				}
				if (addinfo == true) {
					for (unsigned int i = 0; (char)(ptr[i]) != '\0'; i++) {
						Serial.write(ptr[i]); 
					}
					Serial.write("\"");
				}
				Serial.write("\r\n");
			}
		}
	}
}
// Create and encode token
void createToken() {
	char serializedJSON[72]; 
	const size_t capacity = JSON_OBJECT_SIZE(2) + 60;
	StaticJsonDocument<capacity> jsonDoc;

	jsonDoc["_id"] = ID;
	jsonDoc["tim"] = gpsInfo.timeDate;

	serializeJson(jsonDoc, serializedJSON);
	ArduinoJWT jwt(SECRET);
	jwt.encodeJWT((char*)serializedJSON, (char*)JWToken);
	lastToken = millis();

#ifdef DEBUG
	debugSerial.print(F("Serialized JSON: "));
	debugSerial.println(serializedJSON);
	debugSerial.print(F("Generated JWToken: "));
	debugSerial.println(JWToken);
#endif
}