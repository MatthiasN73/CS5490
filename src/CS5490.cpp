/**

	@author Tiago Britto Lobao
	tiago.blobao@gmail.com

	Purpose: Control an integrated circuit
	Cirrus Logic - CS5490

	Used to measure electrical quantities

	MIT License

	Modified by Maurizio Malaspina maurizio.malaspina@gmail.com 
*/

#include "CS5490.h"

/******* Init CS5490 *******/

//For Arduino & ESP8622
#if !(defined ARDUINO_NodeMCU_32S ) && !defined(__AVR_ATmega1280__) && !defined(__AVR_ATmega2560__) && !defined(ARDUINO_Node32s) && !defined(ESP32)
	CS5490::CS5490(float mclk, int rx, int tx){
		this->selectedPage = -1;
		this->MCLK = mclk;
		this->cSerial = new SoftwareSerial(rx,tx);
		this->resetPin = -1;
	}

	CS5490::CS5490(float mclk, int rx, int tx, int reset){
		this->selectedPage = -1;
		this->MCLK = mclk;
		this->cSerial = new SoftwareSerial(rx,tx);
		this->resetPin = reset;
		pinMode(this->resetPin, INPUT); // In the hypothesis there's an RC circuit connected to the CS5490 reset pin
	}
//For ESP32 AND MEGA
#else
	CS5490::CS5490(float mclk){
		this->selectedPage = -1;
		this->MCLK = mclk;
		this->cSerial = &Serial2;
		this->resetPin = -1;
	}

		CS5490::CS5490(float mclk, int reset){
		this->selectedPage = -1;
		this->MCLK = mclk;
		this->cSerial = &Serial2;
		this->resetPin = reset;
		pinMode(this->resetPin, INPUT); // In the hypothesis there's an RC circuit connected to the CS5490 reset pin
	}
#endif

void CS5490::begin(int baudRate){
	cSerial->begin(baudRate);
	this->_readOperationResult = true;
	delay(10); //Avoid Bugs on Arduino UNO
}

/**************************************************************/
/*                 METHODS FOR BASIC OPERATIONS               */
/**************************************************************/


/******* Write a register by the serial communication *******/
/* data bytes pass by data variable from this class */

void CS5490::write(int page, int address, uint32_t value){

	uint8_t pos = 0;
	uint8_t buffer[5] = {};
	
	//Select page 
	if(this->selectedPage != page){
		buffer[pos++] = (CS_pageByte | (uint8_t)page);
		if(_useSerialChecksum == true) {
			buffer[pos] = calcChecksum(buffer, pos);
			pos++;
		}
		cSerial->write(buffer, pos);
		this->selectedPage = page;
	}

	// Write-Command + address
	pos = 0;	
	buffer[pos++] = (CS_writeByte | (uint8_t)address);

	//Send information
	for(int i=0; i<3 ; i++){
		buffer[pos++] = value & 0x000000FF;
		value >>= 8;
	}

	if(_useSerialChecksum) {
		buffer[pos] = calcChecksum(buffer, pos);
		pos++;
	}
	cSerial->write(buffer, pos);
}

/******* Read a register by the serial communication *******/
/* data bytes pass by data variable from this class */

void CS5490::read(int page, int address){
	unsigned long startMillis;

	this->clearSerialBuffer();
	memset(data, 0, 3);

	uint8_t pos = 0;
	uint8_t buffer[4];

	//Select page
	if(this->selectedPage != page){
		buffer[pos++] = (CS_pageByte | (uint8_t)page);
		if(_useSerialChecksum) {
			buffer[pos] = calcChecksum(buffer, pos);
			pos++;
		}
		cSerial->write(buffer, pos);
		this->selectedPage = page;
	}

	//Select address
	pos = 0;
	buffer[pos++] = (CS_readByte | (uint8_t)address);
	if(_useSerialChecksum) {
		buffer[pos] = calcChecksum(buffer, pos);
		pos++;
	}
	cSerial->write(buffer, pos);


	uint8_t toRead = _useSerialChecksum == true ? 4 : 3;

	startMillis = millis();
	//Wait for 3 or 4 bytes to arrive
	while((cSerial->available() < toRead) && ((millis()-startMillis) < 200));
	if(cSerial->available() >= toRead)
	{
		for(int i=0; i<toRead; i++) {
			buffer[i] = cSerial->read();
		}
		if(_useSerialChecksum) {
			uint8_t csum = calcChecksum(buffer, 3);
			if(csum == buffer[3]) {
				memcpy(data, buffer, 3);
				this->_readOperationCsError = false;
			}
			else {
				this->_readOperationResult = false;
				this->_readOperationCsError = true;
			}
		}
		else
		{
				memcpy(data, buffer, 3);
		}
	}
	else {
		this->_readOperationResult = false;
	}

	this->clearSerialBuffer();
}

// Used to check if read operations succeeded or not. Return:
// true, when all the reading operations occurred from the initial observing instant succeeded
// false, when there was an issue with the communication occurred therefore the read registers values could be corrupted.
//
// Note that the "initial observing instant" is the most recent occurred between the following events:
//  - the last call to "begin()" method (in general before using the object)
//  - the last call to "resolve()" method
bool CS5490::areLastReadingOperationsSucceeded(void)
{
	return this->_readOperationResult;
}

uint32_t CS5490::getRegChk(void)
{
	return this->readReg(16,1);
}

/******* Give an instruction by the serial communication *******/

void CS5490::instruct(int value){
	uint8_t buffer[2] = {};
	uint8_t pos = 0;
	buffer[pos++] = (CS_instructionByte | (uint8_t)value);
	if(_useSerialChecksum) {
		buffer[pos] = calcChecksum(buffer, pos);
		pos++;
	}

	cSerial->write(buffer, pos);
}

/******* Clears cSerial Buffer *******/
void CS5490::clearSerialBuffer(){
	while (cSerial->available()) cSerial->read();
}

// Calculates the checksum for buffer
uint8_t CS5490::calcChecksum(const uint8_t* buffer, uint8_t len){
	uint8_t csum = 0xFF;
	for(uint8_t idx = 0; idx < len; idx++){
		csum = ((csum - buffer[idx]) & 0xFF);
	}
	return(csum);
}

/*
  Function: toDouble
  Transforms a 24 bit number to a double number for easy processing data

  @param
  data[] => Array with size 3. Each uint8_t is an 8 byte number received from CS5490
  LSBpow => Exponent specified from datasheet of the less significant bit
  MSBoption => Information of most significant bit case. It can be only three values:
    MSBnull (1)  The MSB is a Don't Care bit
    MSBsigned (2) the MSB is a negative value, requiring a 2 complement conversion
    MSBunsigned (3) The MSB is a positive value, the default case.
	@return value from last data received from CS55490

	https://repl.it/@tiagolobao/toDoubletoBinary-CS5490
*/
double CS5490::toDouble(int LSBpow, int MSBoption){

	double output = 0.0;
	bool MSB;

	uint32_t buffer = this->concatData();

  switch(MSBoption){

    case MSBnull:
			buffer &= 0x7FFFFF; //Clear MSB
      output = (double)buffer;
      output /= pow(2,LSBpow);
    break;

    case MSBsigned:
      MSB = data[2] & 0x80;
  		if(MSB){  //- (2 complement conversion)
  			buffer = ~buffer;
  			buffer = buffer & 0x00FFFFFF; //Clearing the first 8 bits
  			output = (double)buffer + 1.0;
  			output /= -pow(2,LSBpow);
  		} else {  //+
  		  output = (double)buffer;
  			output /= (pow(2,LSBpow)-1.0);
  		}
    break;

    default:
    case MSBunsigned:
      output = (double)buffer;
  		output /= pow(2,LSBpow);
    break;

  }

	return output;
}

/*
  Function: toBinary
  Transforms a double number to a 24 bit number for writing registers

  @param
  LSBpow => Expoent specified from datasheet of the less significant bit
  MSBoption => Information of most significant bit case. It can be only three values:
    MSBnull (1)  The MSB is a Don't Care bit
    MSBsigned (2) the MSB is a negative value, requiring a 2 complement conversion
    MSBunsigned (3) The MSB is a positive value, the default case.
  input => (double) value to be sent to CS5490
 @return binary value equivalent do (double) input

	https://repl.it/@tiagolobao/toDoubletoBinary-CS5490
*/
uint32_t CS5490::toBinary(int LSBpow, int MSBoption, double input){

	uint32_t output;

  switch(MSBoption){
    case MSBnull:
      input *= pow(2,LSBpow);
      output = (uint32_t)input;
      output &= 0x7FFFFF; //Clear Don't care bits
    break;
    case MSBsigned:
      if(input <= 0){ //- (2 complement conversion)
        input *= -pow(2,LSBpow);
        output = (uint32_t)input;
        output = ~output;
        output = (output+1) & 0xFFFFFF; //Clearing the first 8 bits
      } else {       //+
        input *= (pow(2,LSBpow)-1.0);
        output = (uint32_t)input;
      }
    break;
    default:
    case MSBunsigned:
  		input *= pow(2,LSBpow);
      output = (uint32_t)input;
    break;
  }

  return output;
}

/******* Concatenation of the incomming data from CS5490 *******/
uint32_t CS5490::concatData(){
	uint32_t output = 0;
	output |= (data[2] << 16);
	output |= (data[1] << 8);
	output |= data[0];
	return output;
}


/**************************************************************/
/*              PUBLIC METHODS - Instructions                 */
/**************************************************************/
void CS5490::hardwareReset(void)
{
	unsigned long startTime;

	if(this->resetPin != -1) // Check if the reset pin is properly managed (see constructor overload)
	{
		// Manage LOW --> HIGH reset pin state
		startTime = millis();
		digitalWrite(this->resetPin, LOW);
		pinMode(this->resetPin, OUTPUT);
		while(millis() < startTime + 100);
		digitalWrite(this->resetPin, HIGH);
		startTime = millis();
		while(millis() < startTime + 100);
	}

	// after reset it's deactivated
	_useSerialChecksum = false;

  	// SW reset (in case the reset line fails...)
  	this->reset();

	// Errata: CS5490 Rev B2 Silicon (Mar'13) - Erratum 2 "On chip reference reset state".
	// The following sequence do not affect the critical registers CRC value (default: 0xDD8BD6)
	this->write(0,28,0x000016);
	this->write(0,30,0x0C0008);
	this->write(0,28,0x000000);

}

// Errata: CS5490 Rev B2 Silicon (Mar'13) - Erratum 2 "On chip reference reset state".
bool CS5490::checkInternalVoltageReference(void)
{
	return (this->readReg(0, 30) == 0x0C0008 ? true : false);
}

bool CS5490::isChecksumError(void) 
{
	return(_useSerialChecksum == true ? _readOperationCsError : false);
}

//
void CS5490::checksum_enable(bool value) 
{
	uint32_t regValue = this->readReg(0x00, 0x07);
	if(value == true) {
		regValue &= ~0x020000;
	}
	else {
		regValue |= 0x020000;
	}
	
	this->write(0x0,0x07,regValue);

	_useSerialChecksum = value;
}

// Perform a chip recovery operation. Note that default internal registers values will be restored.
// Used, in general, when the method "areLastReadingOperationsSucceeded()" return false.
void CS5490::resolve(void)
{
  this->hardwareReset();
  this->_readOperationResult = true;
}

void CS5490::reset(){
	this->instruct(1);
}

void CS5490::standby(){
	this->instruct(2);
}

void CS5490::wakeUp(){
	this->instruct(3);
}

void CS5490::singConv(){
	this->instruct(20);
}

void CS5490::contConv(){
	this->instruct(21);
}

void CS5490::haltConv(){
	this->instruct(24);
}

void CS5490::calibrate(uint8_t type, uint8_t channel){
	int settleTime = 2000; //Wait 2 seconds before and after
	delay(settleTime);
	uint8_t calibrationByte = 0b00100000;
	calibrationByte |= (type|channel);
	this->instruct(calibrationByte);
	delay(settleTime);
}

void CS5490::sendCalibrationCommand(uint8_t type, uint8_t channel){
	// The full sequence, according to "AN366REV2" is properly implemented in the "CS5490_AC_Current_Gain_Tuning_demo.ino" example
	uint8_t calibrationByte = 0b00100000;
	calibrationByte |= (type|channel);
	this->instruct(calibrationByte);
}

/**************************************************************/
/*       PUBLIC METHODS - Configuration                       */
/**************************************************************/

/* SET */
void CS5490::setBaudRate(long value){

	//Calculate the correct binary value
	uint32_t hexBR = ceil(value*0.5242880/MCLK);
	if (hexBR > 65535) hexBR = 65535;
	hexBR |= 0x020000;

	this->write(0x80,0x07,hexBR);          // 0x80 instead of 0x00 in order to force a page selection command on page 0
	delay(100); //To avoid bugs from ESP32

	//Reset Serial communication from controller
	cSerial->end();
	cSerial->begin(value);
	delay(50); //Avoid bugs from Arduino MEGA
	return;
}

void CS5490::setDOpinFunction(DO_Function_t DO_fnct, bool openDrain)
{
	uint32_t reg;

	reg = this->readReg(0,1); // Config 1 register
	reg &= (~0x0000000F);

	if(openDrain)
		reg |= 0x00010000;

	if(DO_fnct == DO_EPG)
	{
		reg |= 0x00100000; // Enable EPG block
	}

	if(DO_fnct == DO_EPG || DO_fnct == DO_P_SIGN || DO_fnct == DO_P_SUM_SIGN || DO_fnct == DO_Q_SIGN || DO_fnct == DO_Q_SUM_SIGN || DO_fnct == DO_V_ZERO_CROSSING || DO_fnct == DO_I_ZERO_CROSSING || DO_fnct == DO_HI_Z_DEFAULT)
	{
		reg |= (uint32_t)DO_fnct;
		this->write(0,1,reg);
	}
}

/* GET */

long CS5490::getBaudRate(){
	this->read(0,7);
	//uint32_t buffer = this->concatData();
	//buffer -= 0x020000;
	uint32_t buffer = (this->concatData() & 0x0000FFFF);
	return ( (buffer/0.5242880)*MCLK );
}

/**************************************************************/
/*       PUBLIC METHODS - Calibration                         */
/**************************************************************/

/* GAIN */

double CS5490::getGainSys(){
	this->read(16,60);
	return this->toDouble(22,MSBsigned);
}

double CS5490::getGainV(){
	this->read(16,35);
	return this->toDouble(22,MSBunsigned);
}

double CS5490::getGainI(){
	this->read(16,33);
	return this->toDouble(22,MSBunsigned);
}

double CS5490::getGainT(){
	this->read(16,54);
	return this->toDouble(16,MSBunsigned);
}

void CS5490::setGainSys(double value){
	uint32_t binValue = this->toBinary(22,MSBsigned,value);
  this->write(16,60,binValue);
}

void CS5490::setGainV(double value){
	uint32_t binValue = this->toBinary(22,MSBunsigned,value);
  this->write(16,35,binValue);
}

void CS5490::setGainI(double value){
	uint32_t binValue = this->toBinary(22,MSBunsigned,value);
  this->write(16,33,binValue);
}

void CS5490::setGainT(double value){
	uint32_t binValue = this->toBinary(16,MSBunsigned,value);
  this->write(16,54,binValue);
}

void CS5490::setAfeGainI(AfeGainI_t AfeCurrentGain)
{
	uint32_t reg;

	if(AfeCurrentGain > I_GAIN_50x)
		return;

	reg = this->readReg(0,0); // Config 0 register
	reg &= (~0x00000030);

	if(AfeCurrentGain == I_GAIN_50x)
		reg |= 0x00000020;

	this->write(0,0,reg);
}

/* OFFSET */

double CS5490::getDcOffsetV(){
  this->read(16,34);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getDcOffsetI(){
  this->read(16,32);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getAcOffsetI(){
  this->read(16,37);
	return this->toDouble(24, MSBunsigned);
}

double CS5490::getOffsetT(){
  this->read(16,55);
	return this->toDouble(16, MSBsigned);
}

void CS5490::setDcOffsetV(double value){
	uint32_t binValue = this->toBinary(23,MSBsigned,value);
  this->write(16,34,binValue);
}

void CS5490::setDcOffsetI(double value){
	uint32_t binValue = this->toBinary(23,MSBsigned,value);
  this->write(16,32,binValue);
}

void CS5490::setAcOffsetI(double value){
	uint32_t binValue = this->toBinary(24,MSBunsigned,value);
  this->write(16,37,binValue);
}

void CS5490::setOffsetT(double value){
	uint32_t binValue = this->toBinary(16,MSBsigned,value);
  this->write(16,55,binValue);
}

/**************************************************************/
/*              PUBLIC METHODS - Measurements                 */
/**************************************************************/

double CS5490::getPeakV(){
	//Page 0, Address 36
	this->read(0,36);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getPeakI(){
	//Page 0, Address 37
	this->read(0,37);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getInstI(){
	//Page 16, Address 2
	this->read(16,2);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getInstV(){
	//Page 16, Address 3
	this->read(16,3);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getInstP(){
	//Page 16, Address 4
	this->read(16,4);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getRmsI(){
	//Page 16, Address 6
	this->read(16,6);
	return this->toDouble(24, MSBunsigned);
}

double CS5490::getRmsV(){
	//Page 16, Address 7
	this->read(16,7);
	return this->toDouble(24, MSBunsigned);
}

double CS5490::getAvgP(){
	//Page 16, Address 5
	this->read(16,5);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getAvgQ(){
	//Page 16, Address 14
	this->read(16,14);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getAvgS(){
	//Page 16, Address 20
	this->read(16,20);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getInstQ(){
	//Page 16, Address 15
	this->read(16,15);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getPF(){
	//Page 16, Address 21
	this->read(16,21);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getTotalP(){
	//Page 16, Address 29
	this->read(16,29);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getTotalS(){
	//Page 16, Address 30
	this->read(16,30);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getTotalQ(){
	//Page 16, Address 31
	this->read(16,31);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getFreq(){
	//Page 16, Address 49
	this->read(16,49);
	return this->toDouble(23, MSBsigned);
}

double CS5490::getTime(){
	//Page 16, Address 61
	this->read(16,61);
	return this->toDouble(0, MSBunsigned);
}

double CS5490::getTemp(){
	//Page 16, Address 27
	this->read(16,27);
	return this->toDouble(16, MSBsigned);
}

/* 
Chip-Id: 0 = CS5484, 1 = CS5480, 3 = CS5490 
*/
int CS5490::getChipId() {
    uint32_t chipInfo = this->readReg(0, 35);
	return((int)((chipInfo & 0xC00000) / 4194303.0));
}

/*
Chip-Revision: 0 = A0, 1 = A1, 2 = B0, 3 = B1, 4 = B2
*/
int CS5490::getChipRev() {
    uint32_t chipInfo = this->readReg(0, 35);
    return((int)((chipInfo & 0xF0) / 15.0));
}

/**************************************************************/
/*              PUBLIC METHODS - Read Register                */
/**************************************************************/

uint32_t CS5490::readReg(int page, int address){
	this->read(page, address);
	return this->concatData();
}
