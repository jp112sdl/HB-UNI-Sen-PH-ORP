//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2021-03-03 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------
// ci-test=yes board=328p aes=no

#define SENSOR_ONLY
#define HIDE_IGNORE_MSG

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>

#include <Register.h>
#include <MultiChannelDevice.h>
#include <sensors/Ds18b20.h>

#include <LiquidCrystal_I2C.h>
#define LCD_ADDRESS        0x27
#define LCD_ROWS           2
#define LCD_COLUMNS        16

#define CONFIG_BUTTON_PIN  8
#define LED_PIN            4
#define DS18B20_PIN        5

#define PH_SIGNAL_PIN             A0
#define ORP_SIGNAL_PIN            A1
#define SENSOR_SWITCH_PIN         6
#define OFF LOW
#define ON  HIGH
#define REF_VOLTAGE               3300
#define CALIBRATION_MODE_TIMEOUT  600   //seconds

#define PEERS_PER_CHANNEL  6

using namespace as;

const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0x17, 0x01},          // Device ID
  "JPPHORP001",                // Device Serial
  {0xF3, 0x17},                // Device Model
  0x10,                        // Firmware Version
  0x53,                        // Device Type
  {0x01, 0x00}                 // Info Bytes
};

typedef AskSin<StatusLed<LED_PIN>, NoBattery, Radio<AvrSPI<10, 11, 12, 13>, 2>> Hal;
Hal hal;

DEFREGISTER(UReg0, MASTERID_REGS, 0x1f, 0x20, 0x21, DREG_BACKONTIME)
class UList0 : public RegList0<UReg0> {
  public:
    UList0 (uint16_t addr) : RegList0<UReg0>(addr) {}

    bool Sendeintervall (uint8_t value) const {
      return this->writeRegister(0x21, value & 0xff);
    }
    uint8_t Sendeintervall () const {
      return this->readRegister(0x21, 0);
    }

    bool Messintervall (uint16_t value) const {
      return this->writeRegister(0x1f, (value >> 8) & 0xff) && this->writeRegister(0x20, value & 0xff);
    }
    uint16_t Messintervall () const {
      return (this->readRegister(0x1f, 0) << 8) + this->readRegister(0x20, 0);
    }

    void defaults () {
      clear();
      lowBatLimit(22);
      backOnTime(60);
      Sendeintervall(18);
      Messintervall(10);
    }
};

DEFREGISTER(UReg1, 0x01, 0x02, 0x03, 0x04, 0x05)
class UList1 : public RegList1<UReg1> {
  public:
    UList1 (uint16_t addr) : RegList1<UReg1>(addr) {}

    bool TemperatureOffsetIndex (uint8_t value) const { return this->writeRegister(0x05, value & 0xff); }
    uint8_t TemperatureOffsetIndex () const { return this->readRegister(0x05, 0); }

    bool OrpOffset (int32_t value) const {
      return
          this->writeRegister(0x01, (value >> 24) & 0xff) &&
          this->writeRegister(0x02, (value >> 16) & 0xff) &&
          this->writeRegister(0x03, (value >> 8) & 0xff) &&
          this->writeRegister(0x04, (value) & 0xff)
          ;
    }

    int32_t OrpOffset () const {
      return
          ((int32_t)(this->readRegister(0x01, 0)) << 24) +
          ((int32_t)(this->readRegister(0x02, 0)) << 16) +
          ((int32_t)(this->readRegister(0x03, 0)) << 8) +
          ((int32_t)(this->readRegister(0x04, 0)))
          ;
    }

    void defaults () {
      clear();
      TemperatureOffsetIndex(7);
      OrpOffset(0);
    }
};

class LcdType {
public:
  class BacklightAlarm : public Alarm {
    LcdType& lcdDev;
  public:
    BacklightAlarm (LcdType& l) :  Alarm(0), lcdDev(l) {}
    virtual ~BacklightAlarm () {}
    void restartTimer(uint8_t sec) {
      sysclock.cancel(*this);
      set(seconds2ticks(sec));
      lcdDev.lcd.backlight();
      sysclock.add(*this);
    }

    virtual void trigger (__attribute__((unused)) AlarmClock& clock) {
      lcdDev.lcd.noBacklight();
    }
  }backlightalarm;
private:
  uint8_t backlightOnTime;
  byte degree[8] = { 0b00111, 0b00101, 0b00111, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 };

  String tempToStr(int16_t t) {
    String s_temp = "--.-";
    s_temp = (String)((float)t / 10.0);
    s_temp = s_temp.substring(0, s_temp.length() - 1);
    //if (t < 1000 && t >= 0) s_temp = " " + s_temp;
    return s_temp;
  }

  String phToStr(uint16_t p) {
    String s_ph = " --.-";
    s_ph = (String)((float)p / 100.0);
    if (p < 1000) s_ph = " " + s_ph;
    return s_ph ;
  }

  String orpToStr(int16_t o) {
    String s_orp = "----";
    s_orp = (String)o;
    if (o >= 0) {
      if (o < 10)        s_orp = "   " + s_orp;
      else if (o < 100)  s_orp = "  "  + s_orp;
      else if (o < 1000)   s_orp = " " + s_orp;
    } else {
      if (o < -9) s_orp = " " + s_orp;
      else if (o < 0) s_orp = "  " + s_orp;
    }
    return s_orp ;
  }

public:
  LiquidCrystal_I2C lcd;
  LcdType () :  backlightalarm(*this), backlightOnTime(10), lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS){}
  virtual ~LcdType () {}

  void showMeasureValues(int16_t temperature, uint16_t ph, int16_t orp) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(" T  |  PH  | ORP");
   //         "16.0|  9.0 | 750"
    lcd.setCursor(0,1);
    lcd.print(tempToStr(temperature));
    lcd.setCursor(4,1);lcd.print("|");
    lcd.print(phToStr(ph));
    lcd.setCursor(11,1);lcd.print("|");
    lcd.print(orpToStr(orp));
  }

  void showCalibrationMenu(uint8_t step, uint16_t n, uint16_t a, int16_t t) {
    lcd.clear();
    switch (step) {
    case 0:
      lcd.setCursor(0,0);lcd.print(F("CALIBRATION MODE"));
      lcd.setCursor(2,1);lcd.print(F("Press button"));
      break;
    case 1:
      lcd.setCursor(0,0);lcd.print(F("Put in 7.0 sol."));
      lcd.setCursor(2,1);lcd.print(F("Press button"));
      break;
    case 2:
      lcd.setCursor(0,0);lcd.print(F("Reading 7.0 DONE"));
      lcd.setCursor(2,1);lcd.print(F("Press button"));
      break;
    case 3:
      lcd.setCursor(0,0);lcd.print(F("Put in 4.0 sol."));
      lcd.setCursor(2,1);lcd.print(F("Press button"));
      break;
    case 4:
      lcd.setCursor(0,0);lcd.print(F("Reading 4.0 DONE"));
      lcd.setCursor(2,1);lcd.print(F("Press button"));
      break;
    case 5:
      lcd.setCursor(0,0);lcd.print(F("Saving."));lcd.print(tempToStr(t));lcd.setCursor(11,0);lcd.write(byte(0));
      lcd.setCursor(0,1);lcd.print("7:");lcd.print(n);lcd.print(" 4:");lcd.print(a);
      _delay_ms(2000);
      break;
    case 6:
      lcd.setCursor(3,0);lcd.print(F("CAL FAILED"));
      lcd.setCursor(1,1);lcd.print(F("STARTING AGAIN"));
      _delay_ms(2000);
      break;
    }

  }
  void initLCD(uint8_t *serial) {
    Wire.begin();
    Wire.beginTransmission(LCD_ADDRESS);
    if (Wire.endTransmission() == 0) {
      lcd.init();
      lcd.createChar(0, degree);
      lcd.backlight();
      lcd.setCursor(0, 0);
      lcd.print(ASKSIN_PLUS_PLUS_IDENTIFIER);
      lcd.setCursor(3, 1);
      lcd.setContrast(200);
      lcd.print((char*)serial);

      if (backlightOnTime > 0) backlightalarm.restartTimer(backlightOnTime);

    } else {
      DPRINT("LCD Display not found at 0x");DHEXLN((uint8_t)LCD_ADDRESS);
    }
  }

  void setBackLightOnTime(uint8_t t) {
    backlightOnTime = t;
    if (backlightOnTime == 0)
      lcd.backlight();
    else
      lcd.noBacklight();
  }
};
LcdType lcd;

class MeasureEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, int16_t temp, uint16_t ph, int16_t orp) {
      Message::init(0x0f, msgcnt, 0x53, BIDI | WKMEUP, (temp >> 8) & 0x7f, temp & 0xff);
      pload[0] = (ph >> 8) & 0xff;
      pload[1] =  ph & 0xff;
      pload[2] = (orp >> 8) & 0xff;
      pload[3] =  orp & 0xff;
    }
};

class MeasureChannel : public Channel<Hal, UList1, EmptyList, List4, PEERS_PER_CHANNEL, UList0>, public Alarm {
private:
    MeasureEventMsg   msg;
    UserStorage       us;
    OneWire           dsWire;
    Ds18b20           ds18b20[1];
    bool              ds18b20_present;
    bool              phcalibrationMode;
    bool              first;
    int16_t           currentTemperature;
    int16_t           calib_Temperature;
    uint8_t           phcalibrationStep;
    uint16_t          ph;
    int16_t           orp;
    uint16_t          calib_neutralVoltage;
    uint16_t          calib_acidVoltage;
    uint16_t          measureCount;
    uint32_t          ph_cumulated;
    int32_t           orp_cumulated;
    uint32_t          temperature_cumulated;
  public:
    MeasureChannel () : Channel(), Alarm(seconds2ticks(3)), us(0), dsWire(DS18B20_PIN), ds18b20_present(false), phcalibrationMode(false), first(true), currentTemperature(0), calib_Temperature(0), phcalibrationStep(0), ph(0), orp(0), calib_neutralVoltage(0), calib_acidVoltage(0), measureCount(0), ph_cumulated(0), orp_cumulated(0), temperature_cumulated(0) {}
    virtual ~MeasureChannel () {}

    int16_t readTemperature() {
      if (ds18b20_present == false) return 250;

      Ds18b20::measure(ds18b20, 1);
      DPRINT(F("Temperature       : "));DDECLN(ds18b20[0].temperature());
      return (ds18b20[0].temperature()) + (-35+5*this->getList1().TemperatureOffsetIndex());
    }

    uint32_t readVoltage(uint8_t pin) {
      analogRead(pin);

      //Mittelwert ??ber 5 Messungen
      uint32_t analogValue = 0;
      for (uint8_t i=0; i <5; i++) {
        _delay_ms(5);
        analogValue += analogRead(pin);
      }
      analogValue = analogValue / 5;
      DPRINT(F("analogValue ("));DDEC(pin);DPRINT(F(")  : "));DDECLN(analogValue);
      //

      uint32_t voltage = ((uint32_t)analogValue * REF_VOLTAGE * 10UL) / 1024;
      DPRINT(F("measured Voltage  : "));DDECLN(voltage);

      return voltage;
    }

    void restorePHCalibrationValues() {
      calib_neutralVoltage = ((uint16_t)(us.getByte(1)) << 8) + ((uint16_t)(us.getByte(2)));
      calib_acidVoltage    = ((uint16_t)(us.getByte(3)) << 8) + ((uint16_t)(us.getByte(4)));
      calib_Temperature    = ((uint16_t)(us.getByte(5)) << 8) + ((uint16_t)(us.getByte(6)));

      DPRINTLN(F("Restored Calibration Values:"));
      DPRINT(F("-CAL neutralVoltage: "));DDECLN(calib_neutralVoltage);
      DPRINT(F("-CAL acidVoltage   : "));DDECLN(calib_acidVoltage);
      DPRINT(F("-CAL temperature   : "));DDECLN(calib_Temperature);
    }

    void disablePHCalibrationMode() {
      DPRINTLN(F("Exiting Calibration Mode"));
      phcalibrationMode = false;
      sysclock.cancel(*this);
      phcalibrationStep = 0;
      this->changed(true);
      set(millis2ticks(1000));
      sysclock.add(*this);
    }

    void enablePHCalibrationMode() {
      DPRINTLN(F("Entering Calibration Mode"));
      phcalibrationMode = true;
      sysclock.cancel(*this);
      this->changed(true);
      set(seconds2ticks(CALIBRATION_MODE_TIMEOUT));
      sysclock.add(*this);
      nextCalibrationStep();
    }

    void savePHCalibrationValues() {
      DPRINTLN(F("Saving Calibration Values:"));
      DPRINT(F("-CAL neutralVoltage: "));DDECLN(calib_neutralVoltage);
      DPRINT(F("-CAL acidVoltage   : "));DDECLN(calib_acidVoltage);
      DPRINT(F("-CAL temperature   : "));DDECLN(calib_Temperature);

      us.setByte(1, (calib_neutralVoltage >> 8) & 0xff);
      us.setByte(2, (calib_neutralVoltage)      & 0xff);

      us.setByte(3, (calib_acidVoltage >> 8) & 0xff);
      us.setByte(4, (calib_acidVoltage)      & 0xff);

      us.setByte(5, (calib_Temperature >> 8) & 0xff);
      us.setByte(6, (calib_Temperature)      & 0xff);
    }

    void togglePHCalibrationMode() {
      phcalibrationMode = !phcalibrationMode;
      if (phcalibrationMode == true) enablePHCalibrationMode(); else disablePHCalibrationMode();
    }

    bool getCalibrationMode() {
      return phcalibrationMode;
    }

    void nextCalibrationStep() {
      if (phcalibrationMode == true) {
        uint32_t voltage = 0;
        DPRINT(F("CALIB STEP "));DDECLN(phcalibrationStep);
        switch (phcalibrationStep) {
        case 0:
          break;
        case 1:
          break;
        case 2:
          voltage = readVoltage(PH_SIGNAL_PIN);
          //voltage = 14900;
          if (voltage > 13220 && voltage < 16780) {
            calib_neutralVoltage = voltage;
          } else phcalibrationStep = 6;
          break;
        case 3:
          break;
        case 4:
          voltage = readVoltage(PH_SIGNAL_PIN);
          //voltage = 19900;
          if (voltage > 18540 && voltage < 22100) {
            calib_acidVoltage = voltage;
          } else phcalibrationStep = 6;
          break;
        case 5:
          calib_Temperature = readTemperature();
          savePHCalibrationValues();
          disablePHCalibrationMode();
          break;
        }
        lcd.showCalibrationMenu(phcalibrationStep, calib_neutralVoltage, calib_acidVoltage, calib_Temperature);
        if (phcalibrationStep < 6) phcalibrationStep++; else phcalibrationStep = 1;
      } else {
        phcalibrationStep = 0;
      }
    }

    uint16_t readPH() {
      if (first) {
        restorePHCalibrationValues();
        first = false;
      }
      //Erfassen der PH-Sensor Spannung
      uint32_t measuredVoltage = readVoltage(PH_SIGNAL_PIN);

      //PH-Berechnung:
      //1.) slope
      float slope = (7.0-4.0)/((((float)calib_neutralVoltage/10.0)-1500.0)/3.0 - (((float)calib_acidVoltage / 10.0)-1500.0)/3.0);
      DPRINT(F("         SLOPE    : "));DDECLN(slope);

      //1a.) slope temperature compensation
      float slope_corrected = slope * ( ( ((float)currentTemperature / 10.0) +273.15) / ( ((float)calib_Temperature / 10.0)  + 273.15) );
      DPRINT(F("         SLOPECORR: "));DDECLN(slope_corrected);

      //2.) intercept
      float intercept =  7.0 - slope_corrected*(((float)calib_neutralVoltage/10.0)-1500.0)/3.0;
      DPRINT(F("         INTERCEPT: "));DDECLN(intercept);
      //3.) PH
      uint16_t _ph = ( slope_corrected*( ((float)measuredVoltage/10.0) - 1500.0 ) / 3.0 + intercept ) * 100.0; //PH Wert muss mit 100 multipliziert werden, da nur "ganze Bytes" ??bertragen werden k??nnen (PH 7.2 ^= 72)
      DPRINT(F("         PH       : "));DDECLN(_ph);
      return _ph;
    }

    int16_t readORP() {
      //Erfassen der ORP-Sensor Spannung
      int32_t measuredVoltage = readVoltage(ORP_SIGNAL_PIN);
      int16_t orpValue=((30L * REF_VOLTAGE) -(75*(measuredVoltage/10L)))/75;

      orpValue += this->getList1().OrpOffset();

      DPRINT(F("        ORP       : "));DDECLN(orpValue);
      return orpValue;
    }

    void run() {
      measureCount++;

      DPRINT(F("Messung #"));DDECLN(measureCount);

      set(seconds2ticks(max(5,device().getList0().Messintervall())));

      //Erfassen der aktuellen Temperatur (DS18B20)
      currentTemperature = readTemperature();

      ph = readPH();

      digitalWrite(SENSOR_SWITCH_PIN, ON);
      _delay_ms(100);
      orp = readORP();
      digitalWrite(SENSOR_SWITCH_PIN, OFF);

      //Anzeige der Daten auf dem LCD Display
      lcd.showMeasureValues(currentTemperature, ph, orp);

      ph_cumulated          += ph;
      orp_cumulated         += orp;
      temperature_cumulated += currentTemperature;

      if (measureCount >= device().getList0().Sendeintervall()) {
        msg.init(device().nextcount(), (ds18b20_present == true) ? temperature_cumulated / measureCount  : -400, ph_cumulated / measureCount, orp_cumulated / measureCount);
        device().broadcastEvent(msg);
        measureCount = 0;
        ph_cumulated = 0;
        orp_cumulated = 0;
        temperature_cumulated = 0;
      }
      sysclock.add(*this);
    }

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      if (phcalibrationMode == false) {
        run();
      } else {
        disablePHCalibrationMode();
      }
    }

    void setup(Device<Hal, UList0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);
      pinMode(PH_SIGNAL_PIN, INPUT);
      pinMode(ORP_SIGNAL_PIN, INPUT);
      pinMode(SENSOR_SWITCH_PIN, OUTPUT);
      digitalWrite(SENSOR_SWITCH_PIN, OFF);
      ds18b20_present = (Ds18b20::init(dsWire, ds18b20, 1) == 1);
      DPRINT(F("DS18B20: "));DPRINTLN( ds18b20_present == true ? "OK":"FAIL");
      sysclock.add(*this);
    }

    void setUserStorage(const UserStorage& storage) {
      us = storage;
    }

    void configChanged() {
      DPRINT(F("*Temperature Offset   : "));DDECLN(this->getList1().TemperatureOffsetIndex());
      DPRINT(F("*Orp         Offset   : "));DDECLN(this->getList1().OrpOffset());
    }

    uint8_t status () const { return 0; }
    uint8_t flags ()  const { return phcalibrationMode ? 0x01 << 1 : 0x00; }
};

class UType : public MultiChannelDevice<Hal, MeasureChannel, 1, UList0> {

public:
  typedef MultiChannelDevice<Hal, MeasureChannel, 1, UList0> TSDevice;
  UType(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
  virtual ~UType () {}

  virtual void configChanged () {
    TSDevice::configChanged();
    DPRINT(F("*Messintervall        : ")); DDECLN(this->getList0().Messintervall());
    DPRINT(F("*Sendeintervall       : ")); DDECLN(this->getList0().Sendeintervall());

    uint8_t bOn = this->getList0().backOnTime();
    DPRINT(F("*LCD Backlight Ontime : ")); DDECLN(bOn);
    lcd.setBackLightOnTime(bOn);
  }
};

UType sdev(devinfo, 0x20);

class CalibButton : public StateButton<HIGH,LOW,INPUT_PULLUP> {
  UType& device;
public:
  typedef StateButton<HIGH,LOW,INPUT_PULLUP> ButtonType;
  CalibButton (UType& dev,uint8_t longpresstime=3) : device(dev) { this->setLongPressTime(seconds2ticks(longpresstime)); }
  virtual ~CalibButton () {}
  virtual void state (uint8_t s) {
    uint8_t old = ButtonType::state();
    ButtonType::state(s);
    if( s == ButtonType::released ) {
      if (device.channel(1).getCalibrationMode() == true) {
        device.channel(1).nextCalibrationStep();
      } else {
        device.startPairing();
      }
    }
    else if ( s == ButtonType::pressed) {
      lcd.backlightalarm.restartTimer( sdev.getList0().backOnTime() );
    }
    else if( s == ButtonType::longreleased ) {
      device.channel(1).togglePHCalibrationMode();
    }
    else if( s == ButtonType::longpressed ) {
      if( old == ButtonType::longpressed ) {
        if( device.getList0().localResetDisable() == false ) {
          device.reset();
        }
      }
      else {
        device.led().set(LedStates::key_long);
      }
    }
  }
};
CalibButton calibBtn(sdev);

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  buttonISR(calibBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
  sdev.channel(1).setUserStorage(sdev.getUserStorage());
  uint8_t serial[11];sdev.getDeviceSerial(serial);serial[10]=0;
  lcd.initLCD(serial);
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    hal.activity.savePower<Idle<false, true>>(hal);
  }
}
