//spart RAM ein
char StringBuffer[250];
#define P(str) strncpy_P(StringBuffer, PSTR(str), sizeof(StringBuffer))

//#include <SoftwareSerial.h>
#include <Ethernet.h>        //für w5100 im arduino "built in" enthalten
#include <utility/w5100.h>
#include <PubSubClient.h>    //MQTT Bibliothek von Nick O'Leary
#include <SPI.h>             //für w5100 im arduino "built in" enthalten
#include <avr/wdt.h>         //Watchdog
#include <TimeLib.h>         //Uhrzeit & Datum Library von Paul Stoffregen
#include <EEPROM.h>

// ---- Konstanten ----
byte mac[] = {0x62, 0x0F, 0xD9, 0x3D, 0x60, 0xAF};
const IPAddress ip(192, 168, 0, 122);
const IPAddress server(192, 168, 0, 5);             // MQTT Server IP Adresse lokal
const float maxCellVoltage = 4.05;


// ---- Variablen ----
char mqttBuffer[50];            //Buffer für Umwandlung von variablen in char
char DatumZeit[30];
byte Stunde=0, Minute=0, Sekunde=0, Tag=0, Monat=0;
int  Jahr=0;

String inString = "";      // string to hold input
int incomingByte, BalanceCode, Length, highbyte, lowbyte;
byte Mosfet_control, mosfetnow;
uint8_t BYTE1, BYTE2, BYTE3, BYTE4, BYTE5, BYTE6, BYTE7, BYTE8, BYTE9, BYTE10;
uint8_t inInts[40], data[9];   // an array to hold incoming data, not seen any longer than 34 bytes, or 9
uint16_t a16bitvar;
float CellMin = 5, CellMax = 0, Cellsum = 0;
float Cell01, Cell02, Cell03, Cell04, Cell05, Cell06, Cell07, Cell08, Cell09, Cell10, Cell11, Cell12, Cell13, Cell14;
float PowerInBat;
float kWhIn = 0, kWhOut = 0, Ah = 0, kWhInDay = 0, kWhOutDay = 0, kWhL1delivered = 0, kWhL2delivered = 0, kWhL3delivered = 0;
byte  flagCutOff = false;
// Soyosource grit tie stuff
const int   maxSoyoOutputL1 = 900;
const int   maxSoyoOutputL2 = 900;
const int   maxSoyoOutputL3 = 900;
float       lowVoltageCutoff = 50.8;
int         L1demandCalc, L2demandCalc, L3demandCalc;
// -- Serial data --
byte byte0 = 36;
byte byte1 = 86;
byte byte2 = 0;
byte byte3 = 33;
byte byte4 = 0; //(2 byte watts as short integer xaxb)
byte byte5 = 0; //(2 byte watts as short integer xaxb)
byte byte6 = 128;
byte byte7 = 8; // checksum
byte serialpacket[8];
int L1SMLPower = 0, L2SMLPower = 0, L3SMLPower = 0; //amount of electricity being imported from grid on L2
int L1demand = 0, L2demand = 0, L3demand = 0;   //current power inverter should deliver (default to zero)
byte L1Pin = 22, L2Pin = 23, L3Pin = 24;

// ---- Timer ----
unsigned long  vorMillisSensoren = 0;      // Polling Timer BMS
const long     intervalSensoren  = 1000;
unsigned long vorMillisReconnect = 100000; // nur alle 100s einen reconnect versuchen
const long    intervalReconnect  = 100000;
unsigned long  timeLastL1Message = 0;
unsigned long  timeLastL2Message = 0;
unsigned long  timeLastL3Message = 0;

EthernetClient ethClient;
PubSubClient client(ethClient);


void setup()
{
  Serial.begin(115200);    // debug serial
  Serial.println("Setup Anfang");
  Serial1.begin(9600);     // BMS serial
  Serial2.begin(4800);     // RS485 serial
  Serial.println("BMS auslesen");
  pinMode(L1Pin, OUTPUT);
  pinMode(L2Pin, OUTPUT);
  pinMode(L3Pin, OUTPUT);


  client.setServer(server, 1883); // Adresse des MQTT-Brokers
  client.setCallback(callback);   // Handler für eingehende Nachrichten
  client.setBufferSize(512);      // increase MQTT buffer for Home Asssistant auto discover
  client.setSocketTimeout(2);     // decrease timeout (default 15s is way too mutch for WDT)
  client.setKeepAlive(2);
  
  // Ethernet-Verbindung aufbauen
  Ethernet.begin(mac, ip);
  W5100.setRetransmissionTime(0x07D0);
  W5100.setRetransmissionCount(2);
  // Watchdog aktivieren, nicht unter 250ms, folgende timeout verwenden:
  // WDTO_1S, WDTO_2S, WDTO_4S, WDTO_8S
  wdt_enable(WDTO_8S);

  // read and set kWh counter from eeprom
  EEPROM.get(0,  kWhIn);
  EEPROM.get(4,  kWhOut);
  EEPROM.get(8,  kWhL1delivered);
  EEPROM.get(12, kWhL2delivered);
  //EEPROM.get(16, kWhL3delivered);
  EEPROM.get(20, lowVoltageCutoff);

  // set soyosource array
  serialpacket[0]=byte0;
  serialpacket[1]=byte1;
  serialpacket[2]=byte2;
  serialpacket[3]=byte3;
  serialpacket[4]=byte4;
  serialpacket[5]=byte5;
  serialpacket[6]=byte6;
  serialpacket[7]=byte7;
  
   //Powerwall kWh int
   //EEPROM.put (0, 2180.99);
   //Powerwall kWh out
   //EEPROM.put (4, 706.502);
   //L2 kWh delivered
   //EEPROM.put (12, 784.0);
   //EEPROM.put (8, 200.0);
}

void loop()
{
  // Watchdog reset
  wdt_reset();

  // MQTT Verbindung aufbauen, solange probieren bis es klappt:
  if (!client.connected())  reconnect();

  // MQTT loop
  client.loop();

  // reset kWh day counter
  if (hour() == 23 && minute() == 59 && second() > 55 && second() < 59 )
  {
     kWhInDay = 0, kWhOutDay = 0;
  }

  // write kWh counter to eeprom
  if (hour() == 8 && minute() == 0 && second() == 1)
  {
    EEPROM.put (0,  kWhIn);
    EEPROM.put (4,  kWhOut);
    EEPROM.put (8,  kWhL1delivered);
    EEPROM.put (12, kWhL2delivered);
    //EEPROM.put (16, kWhL3delivered);
  }
  // Aufgaben ein Mal pro Sekunde durchführen
  if(millis()-vorMillisSensoren > intervalSensoren)
  {
    vorMillisSensoren = millis();

    //CELLS VOLTAGE 04
    call_get_cells_v();      // requests cells voltage
    get_bms_feedback();     // returns with up to date, inString= chars, inInts[]= numbers, chksum in last 2 bytes
    //                       Length (length of data string)
    //  got cell voltages, bytes 0 and 1, its 16 bit, high and low
    //  go through and print them
    // Length = Length - 2;
//    Serial.println ("");
    // print headings
    // for (int i = 2; i < (Length + 1); i = i + 2) {
    //   Serial.print (F(" Cell "));
    //   Serial.print (i / 2);
    //   Serial.print(F("  "));
    // }

    // Serial.print (F(" CellMax ")); // CellMax heading
    // Serial.print(F("  "));

    // Serial.print (F(" CellMin ")); // CellMin heading
    // Serial.print(F("  "));

    // Serial.print (F(" Diff ")); // diference heading
    // Serial.print(F("  "));

    // Serial.print (F("  Avg ")); // Average heading
    // Serial.print(F("  "));

    // // and the values
    // Serial.println ("");
    for (int i = 0; i < Length; i = i + 2) {
      highbyte = (inInts[i]);
      lowbyte = (inInts[i + 1]);
      uint16_t Cellnow = two_ints_into16(highbyte, lowbyte);
      float Cellnowf = Cellnow / 1000.0f; // convert to float
      Cellsum = Cellsum + Cellnowf;
      if (Cellnowf > CellMax) {   // get high and low
        CellMax = Cellnowf;
      }
      if (Cellnowf < CellMin) {
        CellMin = Cellnowf;
      }
      // Serial.print(F(" "));
      // Serial.print(Cellnowf, 3); // 3 decimal places
      
      // Serial.print(i);
      // Serial.print(F("   "));
      // todo switch optimieren, topic mit i variable erstellen
      switch (i)
      {
        case 0:
          client.publish("/Powerwall/Cell1", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell01 = Cellnowf;
          break;
        case 2:
          client.publish("/Powerwall/Cell2", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell02 = Cellnowf;
          break;
        case 4:
          client.publish("/Powerwall/Cell3", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell03 = Cellnowf;
          break;
        case 6:
          client.publish("/Powerwall/Cell4", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell04 = Cellnowf;
          break;
        case 8:
          client.publish("/Powerwall/Cell5", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell05 = Cellnowf;
          break;
        case 10:
          client.publish("/Powerwall/Cell6", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell06 = Cellnowf;
          break;
        case 12:
          client.publish("/Powerwall/Cell7", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell07 = Cellnowf;
          break;
        case 14:
          client.publish("/Powerwall/Cell8", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell08 = Cellnowf;
          break;
        case 16:
          client.publish("/Powerwall/Cell9", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell09 = Cellnowf;
          break;
        case 18:
          client.publish("/Powerwall/Cell10", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell10 = Cellnowf;
          break;
        case 20:
          client.publish("/Powerwall/Cell11", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell11 = Cellnowf;
          break;
        case 22:
          client.publish("/Powerwall/Cell12", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell12 = Cellnowf;
          break;
        case 24:
          client.publish("/Powerwall/Cell13", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell13 = Cellnowf;
          break;
        case 26:
          client.publish("/Powerwall/Cell14", dtostrf(Cellnowf, 1, 2, mqttBuffer), true);
          Cell14 = Cellnowf;
          break;
      }
  }

  // Serial.print(F(" "));
  // Serial.print(CellMax, 3); // 3 decimal places
  // Serial.print(F("   "));
  client.publish("/Powerwall/CellMax", dtostrf(CellMax, 1, 2, mqttBuffer), true);

  // Serial.print(F("   "));
  // Serial.print(CellMin, 3); // 3 decimal places
  // Serial.print(F("   "));
  client.publish("/Powerwall/CellMin", dtostrf(CellMin, 1, 2, mqttBuffer), true);


  float Celldiff = CellMax - CellMin; // difference between highest and lowest
  // Serial.print(F("   "));
  // Serial.print(Celldiff, 3); // 3 decimal places
  // Serial.print(F("   "));
  client.publish("/Powerwall/CellDiff", dtostrf(Celldiff, 1, 2, mqttBuffer), true);


  Cellsum = Cellsum / (Length / 2); // Average of Cells
  // Serial.print(F(" "));
  // Serial.print(Cellsum, 3); // 3 decimal places
  // Serial.print(F("   "));
  client.publish("/Powerwall/CellAverage", dtostrf(Cellsum, 1, 2, mqttBuffer), true);


  //USING BASIC INFO 03 get
  //  CELL BALANCE... info
  call_Basic_info();      // requests basic info.
  get_bms_feedback();   // get that data, used to get BALANCE STATE byte 17 less 4, decimal=byte 13
  BalanceCode = inInts[13]; //  the 13th byte
  BalanceCode = Bit_Reverse( BalanceCode ) ; // reverse the bits, so they are in same order as cells

  // PACK VOLTAGE,, bytes 0 and 1, its 16 bit, high and low
  highbyte = (inInts[0]); // bytes 0 and 1
  lowbyte = (inInts[1]);
  uint16_t PackVoltage = two_ints_into16(highbyte, lowbyte);
  float PackVoltagef = PackVoltage / 100.0f; // convert to float and leave at 2 dec places
  // Serial.print(F("Pack Voltage = "));
  // Serial.print(PackVoltagef);
  client.publish("/Powerwall/Voltage", dtostrf(PackVoltagef, 1, 2, mqttBuffer), true);
  if (PackVoltagef < 46) Ah = 0;


  // CURRENT
  highbyte = (inInts[2]); // bytes 2 and 3
  lowbyte = (inInts[3]);
  int PackCurrent = two_ints_into16(highbyte, lowbyte);
 // uint16_t PackCurrent = two_ints_into16(highbyte, lowbyte);
  float PackCurrentf = PackCurrent / 100.0f; // convert to float and leave at 2 dec places
  // Serial.print(F("   Current = "));
  // Serial.print(PackCurrentf);
  client.publish("/Powerwall/Current", dtostrf(PackCurrentf, 1, 2, mqttBuffer), true);
  PowerInBat = PackCurrentf * PackVoltagef;
  client.publish("/Powerwall/Power", dtostrf(PowerInBat, 1, 2, mqttBuffer), true);
  


  //REMAINING CAPACITY
  highbyte = (inInts[4]);
  lowbyte = (inInts[5]);
  uint16_t RemainCapacity = two_ints_into16(highbyte, lowbyte);
  float RemainCapacityf = RemainCapacity / 100.0f; // convert to float and leave at 2 dec places
  // Serial.print(F("   Remaining Capacity = "));
  // Serial.print(RemainCapacityf);
  // Serial.print(F("Ah"));
  client.publish("/Powerwall/RemainCapacity", dtostrf(RemainCapacityf, 1, 2, mqttBuffer), true);

  //RSOC
  int RSOC = (inInts[19]);
  // Serial.print(F("   RSOC = "));
  // Serial.print(RSOC);
  // Serial.print(F("%"));
  client.publish("/Powerwall/RSOC", dtostrf(RSOC, 1, 0, mqttBuffer), true);


  //Temp probe 1
  highbyte = (inInts[23]);
  lowbyte = (inInts[24]);
  float Temp_probe_1 = two_ints_into16(highbyte, lowbyte);
  float Temp_probe_1f = (Temp_probe_1 - 2731) / 10.00f; // convert to float and leave at 2 dec places
  // Serial.println("");
  // Serial.print(F("Temp probe 1 = "));
  // Serial.print(Temp_probe_1f);
  // Serial.print(" ");
  client.publish("/Powerwall/Temp1", dtostrf(Temp_probe_1f, 1, 2, mqttBuffer), true);


  //Temp probe 2
  highbyte = (inInts[25]);
  lowbyte = (inInts[26]);
  float Temp_probe_2 = two_ints_into16(highbyte, lowbyte);
  float Temp_probe_2f = (Temp_probe_2 - 2731) / 10.00f; // convert to float and leave at 2 dec places
  // Serial.print(F("   Temp probe 2 = "));
  // Serial.print(Temp_probe_2f);
  // Serial.println("");
  client.publish("/Powerwall/Temp2", dtostrf(Temp_probe_2f, 1, 2, mqttBuffer), true);


  // Show the state of MOSFET control
  // Serial.println("");
  // Serial.print(F("Mosfet Charge = "));
  Mosfet_control = (inInts[20]);
  Mosfet_control = Mosfet_control & 1; //& (bitwise and) just want bit 0
  // Serial.print(Mosfet_control);
  client.publish("/Powerwall/Charge", dtostrf(Mosfet_control, 1, 0, mqttBuffer), true);

  // Serial.print(F("  Mosfet DisCharge = "));
  mosfetnow = mosfetnow >> 1; //>> (bitshift right) use variabe mosfetnow, move bit 1 to bit 0
  Mosfet_control = mosfetnow & 1; //& (bitwise and) just want bit 0 again
  // Serial.println(Mosfet_control);
  client.publish("/Powerwall/DisCharge", dtostrf(Mosfet_control, 1, 0, mqttBuffer), true);

  //calculate kWh
  if (PackCurrentf > 0)
  {
    kWhInDay = kWhInDay + (PackVoltagef * PackCurrentf / 3600 / 1000);
    kWhIn = kWhIn + (PackVoltagef * PackCurrentf / 3600 / 1000);
    Ah = Ah + (PackCurrentf / 3600);
  }
  if (PackCurrentf < 0)
  {
    kWhOutDay = kWhOutDay + (PackVoltagef * PackCurrentf * -1 / 3600 / 1000);
    kWhOut = kWhOut + (PackVoltagef * PackCurrentf * -1 / 3600 / 1000);
    Ah = Ah + (PackCurrentf / 3600);
  }
  
  client.publish("/Powerwall/kWhInDay", dtostrf(kWhInDay, 1, 3, mqttBuffer), true);
  client.publish("/Powerwall/kWhOutDay", dtostrf(kWhOutDay, 1, 3, mqttBuffer), true);
  client.publish("/Powerwall/kWhIn", dtostrf(kWhIn, 1, 3, mqttBuffer), true);
  client.publish("/Powerwall/kWhOut", dtostrf(kWhOut, 1, 3, mqttBuffer), true);
  client.publish("/Powerwall/Ah", dtostrf(Ah, 1, 3, mqttBuffer), true);
  
  if (millis() - timeLastL1Message > 10000)
  {
    // timeout mqtt set L2demand to 0W
    L1demand = 0;
    client.publish("/Powerwall/error", "timeout L1 subscribe, L1 delivery stopped", false);
  }

  if (millis() - timeLastL2Message > 10000)
  {
    // timeout mqtt set L2demand to 0W
    L2demand = 0;
    client.publish("/Powerwall/error", "timeout L2 subscribe, L2 delivery stopped", false);
  }

  if (millis() - timeLastL3Message > 10000)
  {
    // timeout mqtt set L2demand to 0W
    L3demand = 0;
    client.publish("/Powerwall/error", "timeout L3 subscribe, L3 delivery stopped", false);
  }

  if (PackVoltagef < lowVoltageCutoff && flagCutOff == false)
  {
    client.publish("/Powerwall/error", "Powerwall almost empty, delivery stopped", false);
    flagCutOff = true;
  }
  else if (PackVoltagef > lowVoltageCutoff + 0.85 && flagCutOff == true)
  {
    flagCutOff = false;
  }

  if (flagCutOff == true)
  {
    // Powerwall almost empty set L1/L2/l3 demand to 0W
    L1demand = 0;
    L2demand = 0;
    L3demand = 0;
  }

  // put to much incomming solar energy to grid, when batt is full
  if (Cell01 > maxCellVoltage || Cell02 > maxCellVoltage || Cell03 > maxCellVoltage || Cell04 > maxCellVoltage || Cell05 > maxCellVoltage || Cell06 > maxCellVoltage || Cell07 > maxCellVoltage || Cell08 > maxCellVoltage || Cell09 > maxCellVoltage || Cell10 > maxCellVoltage || Cell11 > maxCellVoltage || Cell12 > maxCellVoltage || Cell13 > maxCellVoltage || Cell14 > maxCellVoltage)
  {
    //search for lowest demand
    if(L1demand <= L2demand && L1demand <= L3demand)
    {
      L1demandCalc += 30;
      if(L1demandCalc > maxSoyoOutputL1) L1demandCalc = maxSoyoOutputL1;
    }
    if(L2demand <= L1demand && L2demand <= L3demand)
    {
      L2demandCalc += 30;
      if(L2demandCalc > maxSoyoOutputL2) L2demandCalc = maxSoyoOutputL2;
    }
    if(L3demand <= L1demand && L3demand <= L2demand)
    {
      L3demandCalc += 30;
      if(L3demandCalc > maxSoyoOutputL3) L3demandCalc = maxSoyoOutputL3;
    }
  }
  else
  {
    L1demandCalc -= 5;
    if(L1demandCalc < 0) L1demandCalc = 0;
    L2demandCalc -= 5;
    if(L2demandCalc < 0) L2demandCalc = 0;
    L3demandCalc -= 5;
    if(L3demandCalc < 0) L3demandCalc = 0;
  }

  L1demand += L1demandCalc;
  L2demand += L2demandCalc;
  L3demand += L3demandCalc;
  if (L1demand >= maxSoyoOutputL1) L1demand = maxSoyoOutputL1;
  else if (L1demand <= 0) L1demand = 0;
  client.publish("/Powerwall/L1Delivery", dtostrf(L1demand, 1, 0, mqttBuffer), true);

  if (L2demand >= maxSoyoOutputL2) L2demand = maxSoyoOutputL2;
  else if (L2demand <= 0) L2demand = 0;
  client.publish("/Powerwall/L2Delivery", dtostrf(L2demand, 1, 0, mqttBuffer), true);

  if (L3demand >= maxSoyoOutputL3) L3demand = maxSoyoOutputL3;
  else if (L3demand <= 0) L3demand = 0;
  client.publish("/Powerwall/L3Delivery", dtostrf(L3demand, 1, 0, mqttBuffer), true);

  if (L1demand > 0)
  {
    kWhL1delivered += ((float)L1demand / 3600 / 1000);
  }
  client.publish("/Powerwall/kWhL1delivered", dtostrf(kWhL1delivered, 1, 3, mqttBuffer), true);

  if (L2demand > 0)
  {
    kWhL2delivered += ((float)L2demand / 3600 / 1000);
  }
  client.publish("/Powerwall/kWhL2delivered", dtostrf(kWhL2delivered, 1, 3, mqttBuffer), true);

  if (L3demand > 0)
  {
    kWhL3delivered += ((float)L3demand / 3600 / 1000);
  }
  client.publish("/Powerwall/kWhL3delivered", dtostrf(kWhL3delivered, 1, 3, mqttBuffer), true);
  
  // -- Compute serial packet and send it to inverter (just the 3 bytes that change) --
  
  byte4 = int(L1demand/256); // (2 byte watts as short integer xaxb)
  if (byte4 < 0 or byte4 > 256){
      byte4 = 0;}
  byte5 = int(L1demand)-(byte4 * 256); // (2 byte watts as short integer xaxb)
  if (byte5 < 0 or byte5 > 256) {
      byte5 = 0;}
  byte7 = (264 - byte4 - byte5); //checksum calculation
  if (byte7 > 256){
      byte7 = 8;}

  serialpacket[4]=byte4;
  serialpacket[5]=byte5;
  serialpacket[7]=byte7;

  digitalWrite(L1Pin, HIGH);
  delay(1);
  Serial2.write(serialpacket,8);
  Serial2.flush();
  digitalWrite(L1Pin, LOW);
  
  // -- Compute serial packet and send it to inverter (just the 3 bytes that change) --
  byte4 = int(L2demand/256); // (2 byte watts as short integer xaxb)
  if (byte4 < 0 or byte4 > 256){
      byte4 = 0;}
  byte5 = int(L2demand)-(byte4 * 256); // (2 byte watts as short integer xaxb)
  if (byte5 < 0 or byte5 > 256) {
      byte5 = 0;}
  byte7 = (264 - byte4 - byte5); //checksum calculation
  if (byte7 > 256){
      byte7 = 8;}

  serialpacket[4]=byte4;
  serialpacket[5]=byte5;
  serialpacket[7]=byte7;

  digitalWrite(L2Pin, HIGH);
  delay(1);
  Serial2.write(serialpacket,8);
  Serial2.flush();
  digitalWrite(L2Pin, LOW);
  
  // -- Compute serial packet and send it to inverter (just the 3 bytes that change) --
  byte4 = int(L3demand/256); // (2 byte watts as short integer xaxb)
  if (byte4 < 0 or byte4 > 256){
      byte4 = 0;}
  byte5 = int(L3demand)-(byte4 * 256); // (2 byte watts as short integer xaxb)
  if (byte5 < 0 or byte5 > 256) {
      byte5 = 0;}
  byte7 = (264 - byte4 - byte5); //checksum calculation
  if (byte7 > 256){
      byte7 = 8;}

  serialpacket[4]=byte4;
  serialpacket[5]=byte5;
  serialpacket[7]=byte7;

  digitalWrite(L3Pin, HIGH);
  delay(1);
  Serial2.write(serialpacket,8);
  Serial2.flush();
  digitalWrite(L3Pin, LOW);

  } // 1sec loop
}

//------------------------------------------------------------------------------------------
uint16_t two_ints_into16(int highbyte, int lowbyte) // turns two bytes into a single long integer
{
  a16bitvar = (highbyte);
  a16bitvar <<= 8; //Left shift 8 bits,
  a16bitvar = (a16bitvar | lowbyte); //OR operation, merge the two
  return a16bitvar;
}
// ----------------------------------------------------------------------------------------------------
void call_Basic_info()
// total voltage, current, Residual capacity, Balanced state, MOSFET control status
{
  flush(); // flush first

  //  DD  A5 03 00  FF  FD  77
  // 221 165  3  0 255 253 119
  uint8_t data[7] = {221, 165, 3, 0, 255, 253, 119};
  Serial1.write(data, 7);
}
//--------------------------------------------------------------------------
void call_get_cells_v()
{
  flush(); // flush first

  // DD  A5  4 0 FF  FC  77
  // 221 165 4 0 255 252 119
  uint8_t data[7] = {221, 165, 4, 0, 255, 252, 119};
  Serial1.write(data, 7);
}
//--------------------------------------------------------------------------
void call_Hardware_info()
{
  flush(); // flush first

  //  DD  A5 05 00  FF  FB  77
  // 221 165  5  0 255 251 119
  uint8_t data[7] = {221, 165, 5, 0, 255, 251, 119};
  // uint8_t data[7] = {DD, A5, 05, 00, FF, FB, 77};
  Serial1.write(data, 7);
}

//------------------------------------------------------------------------------
void flush()
{ // FLUSH
  delay(100); // give it a mo to settle, seems to miss occasionally without this
  while (Serial1.available() > 0)
  { Serial1.read();
  }
  delay(50); // give it a mo to settle, seems to miss occasionally without this
}
//--------------------------------------------------------------------------
void get_bms_feedback()  // returns with up to date, inString= chars, inInts= numbers, chksum in last 2 bytes
//                          Length
//                          Data only, exclude first 3 bytes
{
  inString = ""; // clear instring for new incoming
  delay(100); // give it a mo to settle, seems to miss occasionally without this
  if (Serial1.available() > 0) {
    {
      for (int i = 0; i < 4; i++)               // just get first 4 bytes
      {
        incomingByte = Serial1.read();
        if (i == 3)
        { // could look at 3rd byte, it's the ok signal
          Length = (incomingByte); // The fourth byte holds the length of data, excluding last 3 bytes checksum etc
          // Serial.print(" inc ");
          //Serial.print(incomingByte);
        }
        if (Length == 0) {
          Length = 1; // in some responses, length=0, dont want that, so, make Length=1
        }
      }
      //  Length = Length + 2; // want to get the checksum too, for writing back, saves calculating it later
      for (int i = 0; i < Length + 2; i++) { // get the checksum in last two bytes, just in case need later
        incomingByte = Serial1.read(); // get the rest of the data, how long it might be.
        inString += (char)incomingByte; // convert the incoming byte to a char and add it to the string
        inInts[i] = incomingByte;       // save incoming byte to array as int
      }
    }
  }
}

//-----------------------------------------------------------------------------------------------------
byte Bit_Reverse( byte x )
// http://www.nrtm.org/index.php/2013/07/25/reverse-bits-in-a-byte/
{
  //          01010101  |         10101010
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  //          00110011  |         11001100
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  //          00001111  |         11110000
  x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);
  return x;
}

//MQTT callback bei empfangener Nachricht

void callback(char* topic, byte* payload, unsigned int length)
{
  // Zähler
  int i = 0;
  // Hilfsvariablen für die Convertierung der Nachricht in ein String
  char message_buff[100];
 
  // Kopieren der Nachricht und erstellen eines Bytes mit abschließender \0
  for(i=0; i<length; i++) {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  // wenn topic /SmartMeter/L2 empfangen wurde
  if (String(topic)=="/SmartMeter/L1")
  {
    timeLastL1Message = millis();
    L1SMLPower = atoi(message_buff);
    if (L1SMLPower < 0) L1SMLPower = L1SMLPower / 2;
    L1demand = L1demand + L1SMLPower + 5; //add grid import to current L2demand and add few watts
  }
  if (String(topic)=="/SmartMeter/L2")
  {
    timeLastL2Message = millis();
    L2SMLPower = atoi(message_buff);
    if (L2SMLPower < 0) L2SMLPower = L2SMLPower / 2;
    L2demand = L2demand + L2SMLPower + 5; //add grid import to current L2demand and add few watts
  }
  if (String(topic)=="/SmartMeter/L3")
  {
    timeLastL3Message = millis();
    L3SMLPower = atoi(message_buff);
    if (L3SMLPower < 0) L3SMLPower = L3SMLPower / 2;
    L3demand = L3demand + L3SMLPower + 5; //add grid import to current L2demand and add few watts
  }

  if (String(topic)=="/Powerwall/setCutOffVoltage")
  {
    float inputFloat = atof(message_buff);
    if (inputFloat >= 42.0 && inputFloat <= 55.0)
    {
      lowVoltageCutoff = inputFloat;
      EEPROM.put(20, inputFloat);
    }
    
  }


  // wenn topic /System/Zeit empfangen dann String zerlegen und Variablen füllen 
  if (String(topic)=="/System/Zeit")
  {
    Stunde=String(message_buff).substring(0,2).toInt();
    Minute=String(message_buff).substring(2,4).toInt();
    Sekunde=String(message_buff).substring(4,6).toInt();
    setTime(Stunde, Minute, Sekunde, Tag, Monat, Jahr);
  }
  // wenn topic /System/Datum empfangen dann String zerlegen und Variablen füllen 
  if (String(topic)=="/System/Datum")
  {
    Tag=String(message_buff).substring(0,2).toInt();
    Monat=String(message_buff).substring(2,4).toInt();
    Jahr=String(message_buff).substring(4,8).toInt();
  }
}

void reconnect()
{
  if(millis() - vorMillisReconnect > intervalReconnect)
  {
    vorMillisReconnect = millis();
    Serial.println("versuche mqtt reconnect!");
    
    //Verbindungsversuch:
    if (client.connect("Powerwall","Stan","rotweiss"))
    {
      Serial.println("mqtt verbunden!");
      // Abonierte Topics:
      client.subscribe(P("/System/Zeit"));
      client.subscribe(P("/System/Datum"));
      client.subscribe(P("/SmartMeter/L1"));
      client.subscribe(P("/SmartMeter/L2"));
      client.subscribe(P("/SmartMeter/L3"));
      client.subscribe(P("/Powerwall/setCutOffVoltage"));
      
      //HomeAssistant autodiscover configs
      client.publish("homeassistant/sensor/Powerwall/Power/config", P("{\"name\":\"Powerwall Power\",\"obj_idd\":\"PowerwallPower\",\"uniq_id\":\"powerwall_power\",\"unit_of_meas\":\"W\",\"stat_t\":\"/Powerwall/Power\",\"dev_cla\":\"power\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Voltage/config", P("{\"name\":\"Powerwall Voltage\",\"obj_idd\":\"PowerwallVoltage\",\"uniq_id\":\"powerwall_voltage\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Voltage\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Current/config", P("{\"name\":\"Powerwall Current\",\"obj_idd\":\"PowerwallCurrent\",\"uniq_id\":\"powerwall_current\",\"unit_of_meas\":\"A\",\"stat_t\":\"/Powerwall/Current\",\"dev_cla\":\"current\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/RemainCapacity/config", P("{\"name\":\"Powerwall Remain Capacity\",\"obj_idd\":\"PowerwallRemainCapacity\",\"uniq_id\":\"powerwall_remain_capacity\",\"unit_of_meas\":\"Ah\",\"stat_t\":\"/Powerwall/RemainCapacity\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/RSOC/config", P("{\"name\":\"Powerwall RSOC\",\"obj_idd\":\"PowerwallRSOC\",\"uniq_id\":\"powerwall_RSOC\",\"unit_of_meas\":\"%\",\"stat_t\":\"/Powerwall/RSOC\",\"dev_cla\":\"battery\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Temp1/config", P("{\"name\":\"Powerwall Temp1\",\"obj_idd\":\"PowerwallTemp1\",\"uniq_id\":\"powerwall_temp1\",\"unit_of_meas\":\"°C\",\"stat_t\":\"/Powerwall/Temp1\",\"dev_cla\":\"temperature\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Temp2/config", P("{\"name\":\"Powerwall Temp2\",\"obj_idd\":\"PowerwallTemp2\",\"uniq_id\":\"powerwall_temp2\",\"unit_of_meas\":\"°C\",\"stat_t\":\"/Powerwall/Temp2\",\"dev_cla\":\"temperature\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell1/config", P("{\"name\":\"Powerwall Cell1\",\"obj_idd\":\"PowerwallCell1\",\"uniq_id\":\"powerwall_cell1\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell1\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell2/config", P("{\"name\":\"Powerwall Cell2\",\"obj_idd\":\"PowerwallCell2\",\"uniq_id\":\"powerwall_cell2\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell2\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell3/config", P("{\"name\":\"Powerwall Cell3\",\"obj_idd\":\"PowerwallCell3\",\"uniq_id\":\"powerwall_cell3\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell3\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell4/config", P("{\"name\":\"Powerwall Cell4\",\"obj_idd\":\"PowerwallCell4\",\"uniq_id\":\"powerwall_cell4\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell4\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell5/config", P("{\"name\":\"Powerwall Cell5\",\"obj_idd\":\"PowerwallCell5\",\"uniq_id\":\"powerwall_cell5\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell5\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell6/config", P("{\"name\":\"Powerwall Cell6\",\"obj_idd\":\"PowerwallCell6\",\"uniq_id\":\"powerwall_cell6\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell6\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell7/config", P("{\"name\":\"Powerwall Cell7\",\"obj_idd\":\"PowerwallCell7\",\"uniq_id\":\"powerwall_cell7\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell7\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell8/config", P("{\"name\":\"Powerwall Cell8\",\"obj_idd\":\"PowerwallCell8\",\"uniq_id\":\"powerwall_cell8\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell8\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell9/config", P("{\"name\":\"Powerwall Cell9\",\"obj_idd\":\"PowerwallCell9\",\"uniq_id\":\"powerwall_cell9\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell9\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell10/config", P("{\"name\":\"Powerwall Cell10\",\"obj_idd\":\"PowerwallCell10\",\"uniq_id\":\"powerwall_cell10\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell10\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell11/config", P("{\"name\":\"Powerwall Cell11\",\"obj_idd\":\"PowerwallCell11\",\"uniq_id\":\"powerwall_cell11\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell11\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell12/config", P("{\"name\":\"Powerwall Cell12\",\"obj_idd\":\"PowerwallCell12\",\"uniq_id\":\"powerwall_cell12\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell12\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell13/config", P("{\"name\":\"Powerwall Cell13\",\"obj_idd\":\"PowerwallCell13\",\"uniq_id\":\"powerwall_cell13\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell13\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Cell14/config", P("{\"name\":\"Powerwall Cell14\",\"obj_idd\":\"PowerwallCell14\",\"uniq_id\":\"powerwall_cell14\",\"unit_of_meas\":\"V\",\"stat_t\":\"/Powerwall/Cell14\",\"dev_cla\":\"voltage\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/kWhInDay/config", P("{\"name\":\"Powerwall kWh in day\",\"obj_idd\":\"PowerwallkWhInDay\",\"uniq_id\":\"powerwall_kWhinday\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/Powerwall/kWhInDay\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/kWhOutDay/config", P("{\"name\":\"Powerwall kWh out day\",\"obj_idd\":\"PowerwallkWhOutDay\",\"uniq_id\":\"powerwall_kWhoutday\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/Powerwall/kWhOutDay\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/kWhIn/config", P("{\"name\":\"Powerwall kWh in\",\"obj_idd\":\"PowerwallkWhIn\",\"uniq_id\":\"powerwall_kWhin\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/Powerwall/kWhIn\",\"stat_cla\":\"total\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/kWhOut/config", P("{\"name\":\"Powerwall kWh out\",\"obj_idd\":\"PowerwallkWhOut\",\"uniq_id\":\"powerwall_kWhout\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/Powerwall/kWhOut\",\"stat_cla\":\"total\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/Ah/config", P("{\"name\":\"Powerwall Ah\",\"obj_idd\":\"PowerwallAh\",\"uniq_id\":\"powerwall_ah\",\"unit_of_meas\":\"Ah\",\"stat_t\":\"/Powerwall/Ah\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/L1Delivery/config", P("{\"name\":\"Powerwall L1 delivery\",\"obj_idd\":\"PowerwallL1Delivery\",\"uniq_id\":\"powerwall_l1_delivery\",\"unit_of_meas\":\"W\",\"stat_t\":\"/Powerwall/L1Delivery\",\"dev_cla\":\"power\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/kWhL1delivered/config", P("{\"name\":\"Soyo L1 delivered\",\"obj_idd\":\"SoyoL1delivered\",\"uniq_id\":\"soyo_l1_delivered\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/Powerwall/kWhL1delivered\",\"stat_cla\":\"total\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/L2Delivery/config", P("{\"name\":\"Powerwall L2 delivery\",\"obj_idd\":\"PowerwallL2Delivery\",\"uniq_id\":\"powerwall_l2_delivery\",\"unit_of_meas\":\"W\",\"stat_t\":\"/Powerwall/L2Delivery\",\"dev_cla\":\"power\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/kWhL2delivered/config", P("{\"name\":\"Soyo L2 delivered\",\"obj_idd\":\"SoyoL2delivered\",\"uniq_id\":\"soyo_l2_delivered\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/Powerwall/kWhL2delivered\",\"stat_cla\":\"total\",\"dev_cla\":\"energy\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/L3Delivery/config", P("{\"name\":\"Powerwall L3 delivery\",\"obj_idd\":\"PowerwallL3Delivery\",\"uniq_id\":\"powerwall_l3_delivery\",\"unit_of_meas\":\"W\",\"stat_t\":\"/Powerwall/L3Delivery\",\"dev_cla\":\"power\"}"), true);
      client.publish("homeassistant/sensor/Powerwall/kWhL3delivered/config", P("{\"name\":\"Soyo L3 delivered\",\"obj_idd\":\"SoyoL3delivered\",\"uniq_id\":\"soyo_l3_delivered\",\"unit_of_meas\":\"kWh\",\"stat_t\":\"/Powerwall/kWhL3delivered\",\"stat_cla\":\"total\",\"dev_cla\":\"energy\"}"), true);
    }
  }
}
