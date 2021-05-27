#include <SoftwareSerial.h>
SoftwareSerial MySerial(2, 3);  // RX, TX 

String inString = "";      // string to hold input
int incomingByte, BalanceCode, Length, highbyte, lowbyte;
byte Mosfet_control, mosfetnow;
uint8_t BYTE1, BYTE2, BYTE3, BYTE4, BYTE5, BYTE6, BYTE7, BYTE8, BYTE9, BYTE10;
uint8_t inInts[40], data[9];   // an array to hold incoming data, not seen any longer than 34 bytes, or 9
uint16_t a16bitvar;
float CellMin = 5, CellMax = 0, Cellsum = 0;


void setup()
{

  Serial.begin(9600);    // will be sending all data to serial, for later analysis
  MySerial.begin(9600);  // set the data rate for the MySerial port
}

void loop()
{
  //CCCCCCCCCCCCCCCCCCCCCCC  CELLS VOLTAGE  CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC

  //CELLS VOLTAGE 04
  call_get_cells_v();      // requests cells voltage
  get_bms_feedback();     // returns with up to date, inString= chars, inInts[]= numbers, chksum in last 2 bytes
  //                       Length (length of data string)
  //  got cell voltages, bytes 0 and 1, its 16 bit, high and low
  //  go through and print them
  // Length = Length - 2;
  Serial.println ("");
  // print headings
  for (int i = 2; i < (Length + 1); i = i + 2) {
    Serial.print (F(" Cell "));
    Serial.print (i / 2);
    Serial.print(F("  "));
  }

  Serial.print (F(" CellMax ")); // CellMax heading
  Serial.print(F("  "));

  Serial.print (F(" CellMin ")); // CellMin heading
  Serial.print(F("  "));

  Serial.print (F(" Diff ")); // diference heading
  Serial.print(F("  "));

  Serial.print (F("  Avg ")); // Average heading
  Serial.print(F("  "));

  // and the values
  Serial.println ("");
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
    Serial.print(F(" "));
    Serial.print(Cellnowf, 3); // 3 decimal places
    Serial.print(F("   "));
  }

  Serial.print(F(" "));
  Serial.print(CellMax, 3); // 3 decimal places
  Serial.print(F("   "));

  Serial.print(F("   "));
  Serial.print(CellMin, 3); // 3 decimal places
  Serial.print(F("   "));

  float Celldiff = CellMax - CellMin; // difference between highest and lowest
  Serial.print(F("   "));
  Serial.print(Celldiff, 3); // 3 decimal places
  Serial.print(F("   "));

  Cellsum = Cellsum / (Length / 2); // Average of Cells
  Serial.print(F(" "));
  Serial.print(Cellsum, 3); // 3 decimal places
  Serial.print(F("   "));


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
  Serial.print(F("Pack Voltage = "));
  Serial.print(PackVoltagef);

  // CURRENT
  highbyte = (inInts[2]); // bytes 2 and 3
  lowbyte = (inInts[3]);
  int PackCurrent = two_ints_into16(highbyte, lowbyte);
 // uint16_t PackCurrent = two_ints_into16(highbyte, lowbyte);
  float PackCurrentf = PackCurrent / 100.0f; // convert to float and leave at 2 dec places
  Serial.print(F("   Current = "));
  Serial.print(PackCurrentf);

  //REMAINING CAPACITY
  highbyte = (inInts[4]);
  lowbyte = (inInts[5]);
  uint16_t RemainCapacity = two_ints_into16(highbyte, lowbyte);
  float RemainCapacityf = RemainCapacity / 100.0f; // convert to float and leave at 2 dec places
  Serial.print(F("   Remaining Capacity = "));
  Serial.print(RemainCapacityf);
  Serial.print(F("Ah"));

  //RSOC
  int RSOC = (inInts[19]);
  Serial.print(F("   RSOC = "));
  Serial.print(RSOC);
  Serial.print(F("%"));

  //Temp probe 1
  highbyte = (inInts[23]);
  lowbyte = (inInts[24]);
  float Temp_probe_1 = two_ints_into16(highbyte, lowbyte);
  float Temp_probe_1f = (Temp_probe_1 - 2731) / 10.00f; // convert to float and leave at 2 dec places
  Serial.println("");
  Serial.print(F("Temp probe 1 = "));
  Serial.print(Temp_probe_1f);
  Serial.print(" ");

  //Temp probe 2
  highbyte = (inInts[25]);
  lowbyte = (inInts[26]);
  float Temp_probe_2 = two_ints_into16(highbyte, lowbyte);
  float Temp_probe_2f = (Temp_probe_2 - 2731) / 10.00f; // convert to float and leave at 2 dec places
  Serial.print(F("   Temp probe 2 = "));
  Serial.print(Temp_probe_2f);
  Serial.println("");

  


  // Show the state of MOSFET control
  Serial.println("");
  Serial.print(F("Mosfet Charge = "));
  Mosfet_control = (inInts[20]);
  Mosfet_control = Mosfet_control & 1; //& (bitwise and) just want bit 0
  Serial.print(Mosfet_control);
  Serial.print(F("  Mosfet DisCharge = "));
  mosfetnow = mosfetnow >> 1; //>> (bitshift right) use variabe mosfetnow, move bit 1 to bit 0
  Mosfet_control = mosfetnow & 1; //& (bitwise and) just want bit 0 again
  Serial.println(Mosfet_control);
  delay(1000);
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
  MySerial.write(data, 7);
}
//--------------------------------------------------------------------------
void call_get_cells_v()
{
  flush(); // flush first

  // DD  A5  4 0 FF  FC  77
  // 221 165 4 0 255 252 119
  uint8_t data[7] = {221, 165, 4, 0, 255, 252, 119};
  MySerial.write(data, 7);
}
//--------------------------------------------------------------------------
void call_Hardware_info()
{
  flush(); // flush first

  //  DD  A5 05 00  FF  FB  77
  // 221 165  5  0 255 251 119
  uint8_t data[7] = {221, 165, 5, 0, 255, 251, 119};
  // uint8_t data[7] = {DD, A5, 05, 00, FF, FB, 77};
  MySerial.write(data, 7);
}

//------------------------------------------------------------------------------
void flush()
{ // FLUSH
  delay(100); // give it a mo to settle, seems to miss occasionally without this
  while (MySerial.available() > 0)
  { MySerial.read();
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
  if (MySerial.available() > 0) {
    {
      for (int i = 0; i < 4; i++)               // just get first 4 bytes
      {
        incomingByte = MySerial.read();
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
        incomingByte = MySerial.read(); // get the rest of the data, how long it might be.
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
