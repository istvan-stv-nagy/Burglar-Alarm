#include <avr/interrupt.h>
#include <EEPROM.h>

#define MAX_BOUNCE_TIME 5

// alarm pins (for the reading the "sensors" and controlling the buzzer )
#define BUTTON1 8
#define BUTTON2 2
#define BUZZ_PIN 13
#define CONTINUOUS_PIN 7
#define ANALOG_PIN 0

//---------LCD---------
#include <LiquidCrystal.h>
#define LCD_RS 12
#define LCD_EN 11
#define LCD_D4 5
#define LCD_D5 4
#define LCD_D6 3
#define LCD_D7 6
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

//---------Remote control---------
#include <IRremote.h>
#define RECV_PIN 10
#define  IR_0         0xff6897
#define  IR_1         0xff30cf
#define  IR_2         0xff18e7
#define  IR_3         0xff7a85
#define  IR_4         0xff10ef
#define  IR_5         0xff38c7
#define  IR_6         0xff5aa5
#define  IR_7         0xff42bd
#define  IR_8         0xff4ab5
#define  IR_9         0xff52ad
#define  IR_DELETE_MINUS     0xffe01f
#define  IR_CHANGE_PLUS      0xffa857
#define  IR_ENTER_EQ  0xff906f
#define  IR_ON_OFF    0xffa25d
#define  IR_SET_MODE  0xff629d
#define  IR_MUTE      0xffe21d
#define  IR_NEXT_PLAY 0xffc23d
#define  IR_AGAIN_REW 0xff22dd
#define  IR_FF        0xff02fd
IRrecv irrecv(RECV_PIN);
decode_results results;

int keyToInt(int key_pressed) {
  switch (key_pressed) {
    case IR_0: return 0;
    case IR_1: return 1;
    case IR_2: return 2;
    case IR_3: return 3;
    case IR_4: return 4;
    case IR_5: return 5;
    case IR_6: return 6;
    case IR_7: return 7;
    case IR_8: return 8;
    case IR_9: return 9;
    default: return -1;
  }
}

//counter, used for counting down from a given value
volatile int counter = 0;
//counts the number of seconds using a timer interrupt
volatile int sec = 0;

// flags
volatile boolean countDown = false;
volatile boolean setAlarm = false;
boolean changeEngPass = false;

// structures 
struct entryStruct {
  byte timeLimit;
  byte password[4];
};

struct zone {
  byte pin = BUTTON1;
  char type = 'D';
  int value = 0;
  entryStruct *param = NULL;
};
struct zone zones[4];

struct timeStruct {
  byte hour = 0;
  byte minutes = 0;
  byte seconds = 0;
} myTime;

struct dateStruct {
  byte year = 17;
  byte month = 11;
  byte day = 27;
} myDate;

struct eventStruct {
  byte zone;
  timeStruct alarm_time;
  dateStruct alarm_date;
};
struct eventStruct* events;

//-----MENUS---------
#define MAX_MENUS 5

#define MENU_MAIN 0
#define MENU_CLOCK 1
#define MENU_DATE 2
#define MENU_ZONES 3
#define MENU_HISTORY 4
#define MENU_ENGINEER 5

#define MENU_SET_TIME 11
#define MENU_SET_DATE 12

#define MAX_ZONES 4
#define ZONE_1 21
#define ZONE_2 22
#define ZONE_3 23
#define ZONE_4 24

#define MENU_ALARM 30
#define MENU_GIVE_PASSWORD 31
#define MENU_REQUIRE_PASSWORD 32
#define MENU_REQUIRE_ENG_PASS 33
#define MENU_SET_PASS 34
#define MENU_SET_LIMIT 35
#define MENU_SET_THRESHOLD 36
#define MENU_SET_DIGITAL 37

#define MENU_SET_ENG_PASS 38
#define MENU_SELECT_TYPE 40

// used in menus navigation
int menu = 0;
int menuSelect = MENU_CLOCK;
int zoneSelect = ZONE_1;
int eventSelect = 0;

// used to hold the engineer current password
byte eng_pass[4] = {0, 0, 0, 0};
// used whenever we are receiving user digits input
byte numbers[6] = {0, 0, 0, 0, 0, 0};
// used in printToLcd function for telling which alarm zone is triggered
byte alarmZone = -1;

// used for dissabling one zone checking when the zone is under setting
int settingZone = -1;

// offsets in EEPROM
#define EVENTS_OFFSET 36
#define ENG_OFFSET 508

// read engineer password from EEPROM and save it to a global array
void readEngFromEEPROM() {
  for (int i = 0; i < 4; i++) {
    eng_pass[i] = EEPROM.read(ENG_OFFSET + i);
  }
}

// save engineer password to EEPROM (when changed)
void saveEngToEEPROM() {
  for (int i = 0; i < 4; i++) {
    EEPROM.write(ENG_OFFSET + i, eng_pass[i]);
  }
}

// read one event from the EEPROM from given address
void readEventFromEEPROM(struct eventStruct* event, int offset) {
  event->zone = EEPROM.read(offset);
  event->alarm_time.hour = EEPROM.read(offset + 1);
  event->alarm_time.minutes = EEPROM.read(offset + 2);
  event->alarm_time.seconds = EEPROM.read(offset + 3);
  event->alarm_date.year = EEPROM.read(offset + 4);
  event->alarm_date.month = EEPROM.read(offset + 5);
  event->alarm_date.day = EEPROM.read(offset + 6);
}

// create alarm event for zone i and save it to EEPROM
void saveEventToEEPROM(int i) {
  struct eventStruct event;
  byte eventNumber;
  int offset;
  eventNumber = EEPROM.read(EVENTS_OFFSET);
  event.zone = i + 1;
  event.alarm_time.hour = myTime.hour; event.alarm_time.minutes = myTime.minutes; event.alarm_time.seconds = myTime.seconds;
  event.alarm_date.year = myDate.year; event.alarm_date.month = myDate.month; event.alarm_date.day = myDate.day;
  offset = EVENTS_OFFSET + 1 + eventNumber * sizeof(struct eventStruct);
  eventNumber++;
  EEPROM.write(EVENTS_OFFSET, eventNumber);

  EEPROM.write(offset, event.zone);
  EEPROM.write(offset + 1, event.alarm_time.hour);
  EEPROM.write(offset + 2, event.alarm_time.minutes);
  EEPROM.write(offset + 3, event.alarm_time.seconds);
  EEPROM.write(offset + 4, event.alarm_date.year);
  EEPROM.write(offset + 5, event.alarm_date.month);
  EEPROM.write(offset + 6, event.alarm_date.day);
}

// read Zones from EEPROM - used in setup() function
int readZonesFromEEPROM() {
  int offset = 0;
  for (int i = 0; i < 4; i++) {
    zones[i].pin = EEPROM.read(offset);
    zones[i].type = EEPROM.read(offset + 1);
    zones[i].value = word(EEPROM.read(offset + 3), EEPROM.read(offset + 2));
    offset += 4;
    if (zones[i].type == 'E') {
      zones[i].param = (struct entryStruct*)malloc(sizeof(struct entryStruct));
      zones[i].param->timeLimit = EEPROM.read(offset);
      offset++;
      for (int j = 0; j < 4; j++) {
        zones[i].param->password[j] = EEPROM.read(offset);
        offset++;
      }
    }
  }
  return offset;
}

// save all Zones to EEPROM, required whenever one zone has been changed
// all require saving in the case a zone changes type from A/D/C to E or vice-versa
void saveZonesToEEPROM() {
  int offset = 0;
  for (int i = 0; i < 4; i++) {
    Serial.println(offset);
    EEPROM.write(offset, zones[i].pin);
    EEPROM.write(offset + 1, zones[i].type);
    EEPROM.write(offset + 2, lowByte(zones[i].value));
    EEPROM.write(offset + 3, highByte(zones[i].value));
    offset += 4;
    if (zones[i].type == 'E') {
      EEPROM.write(offset, zones[i].param->timeLimit );
      offset++;
      for (int j = 0; j < 4; j++) {
        EEPROM.write(offset, zones[i].param->password[j]);
        offset++;
      }
    }
  }
}

void setup() {
  Serial.begin(9600);
  readZonesFromEEPROM();

  //saveEngToEEPROM();
  readEngFromEEPROM();

  Serial.print(eng_pass[0]); Serial.print(eng_pass[1]); Serial.print(eng_pass[2]); Serial.print(eng_pass[3]);

  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  pinMode(BUTTON1, INPUT);
  pinMode(BUTTON2, INPUT);
  pinMode(BUZZ_PIN, OUTPUT);
  pinMode(CONTINUOUS_PIN, INPUT);
  pinMode(ANALOG_PIN, INPUT);

  //clear the interrupts
  cli();
  //set the timer
  TCCR1A = 0;
  TCCR1B = 0;
  OCR1A = 15625;
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS10);
  TCCR1B |= (1 << CS12);
  TIMSK1 |= (1 << OCIE1A);
  sei();

  //enable IR
  irrecv.enableIRIn();
}

// alarm
void playSong() {
  digitalWrite(BUZZ_PIN, HIGH);
}

// correct display of numbers on the LCD
void printNumber(int number) {
  if (number < 10) {
    lcd.print("0");
  }
  lcd.print(number);
}

// history event show 
void printEvent(struct eventStruct event) {
  lcd.setCursor(0, 0);
  lcd.print("zone:");
  lcd.print(event.zone);
  lcd.print(" ");
  printNumber(event.alarm_time.hour);
  lcd.print(":");
  printNumber(event.alarm_time.minutes);
  lcd.print(":");
  printNumber(event.alarm_time.seconds);
  lcd.setCursor(0, 1);
  printNumber(event.alarm_date.year);
  lcd.print("/");
  printNumber(event.alarm_date.month);
  lcd.print("/");
  printNumber(event.alarm_date.day);
}

// main function that manages the LCD print with respect to current menu
void printOnLCD() {
  int index;
  lcd.setCursor(0, 0);
  switch (menu) {
    case MENU_CLOCK:
      lcd.print("CLOCK:");
      lcd.setCursor(0, 1);
      printNumber(myTime.hour);
      lcd.print(":");
      printNumber(myTime.minutes);
      lcd.print(":");
      printNumber(myTime.seconds);
      break;
    case MENU_DATE:
      lcd.print("DATE:");
      lcd.setCursor(0, 1);
      printNumber(myDate.year);
      lcd.print("/");
      printNumber(myDate.month);
      lcd.print("/");
      printNumber(myDate.day);
      break;
    case MENU_SET_TIME:
      lcd.print("SET TIME");
      lcd.setCursor(0, 1);
      for (int j = 0; j < 6; j++) {
        lcd.print(numbers[j]);
        if (j == 1 || j == 3) lcd.print(":");
      }
      break;
    case MENU_SET_DATE:
      lcd.print("SET DATE");
      lcd.setCursor(0, 1);
      for (int j = 0; j < 6; j++) {
        lcd.print(numbers[j]);
        if (j == 1 || j == 3) lcd.print("/");
      }
      break;
    case MENU_ZONES:
      index = zoneSelect - ZONE_1;
      lcd.print("zone"); lcd.print(index + 1); lcd.print(zones[index].type);
      lcd.setCursor(0, 1);
      switch (zones[index].type) {
        case 'E': lcd.print("time lim="); lcd.print(zones[index].param->timeLimit); break;
        case 'D': lcd.print("active="); lcd.print(zones[index].value); break;
        case 'A': lcd.print("threshold="); lcd.print(zones[index].value); break;
        case 'C': lcd.print("continuous watch"); break;
      }
      break;
    case MENU_HISTORY:
      if (EEPROM.read(EVENTS_OFFSET) > 0)
        printEvent(events[eventSelect]);
      else
        lcd.print("empty");
      break;
    case MENU_MAIN:
      lcd.print("MAIN MENU");
      lcd.setCursor(0, 1);
      switch (menuSelect) {
        case MENU_CLOCK: lcd.print("clock"); break;
        case MENU_DATE : lcd.print("date");  break;
        case MENU_ZONES: lcd.print("zones"); break;
        case MENU_HISTORY: lcd.print("history"); break;
        case MENU_ENGINEER: lcd.print("engineer"); break;
      }
      break;
    case MENU_ALARM:
      lcd.print("ALARM!!!");
      lcd.setCursor(0, 1);
      lcd.print("zone ");
      lcd.print(alarmZone + 1);
      break;
    case MENU_GIVE_PASSWORD:
      lcd.print("Enter Pass ");
      printNumber(counter);
      break;
    case MENU_REQUIRE_PASSWORD:
      lcd.print("Enter Pass ");
      break;
    case MENU_REQUIRE_ENG_PASS:
      lcd.print("Engineer pass");
      break;
    case MENU_SET_PASS:
      lcd.print("Give new pass ");
      break;
    case MENU_SET_ENG_PASS:
      lcd.print("New eng pass ");
      break;
    case MENU_SET_LIMIT:
      lcd.print("Give time limit ");
      break;
    case MENU_SET_THRESHOLD:
      lcd.print("Give threshold");
      break;
    case MENU_SET_DIGITAL:
      lcd.print("Set active 0/1");
      break;
    case MENU_SELECT_TYPE:
      lcd.print("Select type");
      lcd.setCursor(0, 1);
      lcd.print("0=D 1=E 2=C 3=A");
      break;
    default: break;
  }
}

// state machine where states are menus; transition are driven by user input 
void performTask() {
  myTime.seconds = sec;
  if (myTime.seconds >= 60) {
    myTime.seconds = 0;
    myTime.minutes++;
    sec = 0;
  }
  if (myTime.minutes >= 60) {
    myTime.minutes = 0;
    myTime.hour++;
  }
  if (myTime.hour >= 24) {
    myTime.hour = 0;
    myTime.minutes = 0;
    myTime.seconds = 0;
  }
  if (menu == MENU_ALARM) {
    playSong();
  }
  if (irrecv.decode(&results)) {
    Serial.println("key pressed");
    int digit;
    static int index = 0;
    switch (menu) {
      case MENU_MAIN:
        if (results.value == IR_NEXT_PLAY) {
          menuSelect++;
          lcd.clear();
          if (menuSelect > MAX_MENUS)
            menuSelect = MENU_CLOCK;
        }
        else if (results.value == IR_ENTER_EQ) {
          if (menuSelect == MENU_HISTORY) {
            eventSelect=0;
            byte eventNumber = EEPROM.read(EVENTS_OFFSET);
            events = (struct eventStruct*) realloc(events, eventNumber * sizeof(struct eventStruct));
            for (int i = 0; i < eventNumber; i++) {
              readEventFromEEPROM(&events[i], EVENTS_OFFSET + 1 + i * sizeof(struct eventStruct));
            }
          }
          if (menuSelect == MENU_ENGINEER) {
            changeEngPass = true;
            menu = MENU_REQUIRE_ENG_PASS;
          } else menu = menuSelect;

          lcd.clear();
        }
        break;
      case MENU_ZONES:
        if (results.value == IR_NEXT_PLAY) {
          zoneSelect++;
          lcd.clear();
          if (zoneSelect >= MAX_ZONES + ZONE_1)
            zoneSelect = ZONE_1;
        }
        else if (results.value == IR_SET_MODE) {
          lcd.clear();
          index = zoneSelect - ZONE_1;
          switch (zones[index].type) {
            case 'E': menu = MENU_REQUIRE_PASSWORD; break;
            case 'A': menu = MENU_SET_THRESHOLD; break;
            case 'D': menu = MENU_SET_DIGITAL; break;
            default: menu = MENU_MAIN;
          }
        }
        else if (results.value == IR_AGAIN_REW) {
          menu = MENU_MAIN;
          lcd.clear();
        }
        else if (results.value == IR_CHANGE_PLUS) {
          index = zoneSelect - ZONE_1;
          settingZone = index;
          menu = MENU_REQUIRE_ENG_PASS;
          lcd.clear();
        }
        break;
      case MENU_SELECT_TYPE:
        switch (results.value) {
          case IR_0:
            zones[index].type = 'D';
            zones[index].param = NULL;

            menu = MENU_SET_DIGITAL;
            lcd.clear();
            break;

          case IR_1:
            zones[index].type = 'E';
            zones[index].param = (struct entryStruct*) malloc(sizeof(entryStruct));
            zones[index].value = 1;
            menu = MENU_SET_PASS;
            lcd.clear();
            break;

          case IR_2:
            zones[index].type = 'C';
            zones[index].param = NULL;
            zones[index].value = 0;
            menu = MENU_ZONES;
            settingZone = -1;
            lcd.clear();
            saveZonesToEEPROM();
            break;

          case IR_3:
            if (zones[index].pin != ANALOG_PIN) {
              lcd.clear();
              lcd.print("HW only zone4");
              delay(1000);
              lcd.clear();
              menu = MENU_ZONES;
            } else {
              zones[index].type = 'A';
              zones[index].param = NULL;
              menu = MENU_SET_THRESHOLD;
              lcd.clear();
            }
            break;
        }
        break;
      case MENU_HISTORY:
        if (results.value == IR_ENTER_EQ) {
          menu = MENU_MAIN;
          lcd.clear();
        }
        else if (results.value == IR_NEXT_PLAY) {
          eventSelect++;
          if (eventSelect >= EEPROM.read(EVENTS_OFFSET))
            eventSelect = 0;
        }
        else if (results.value == IR_DELETE_MINUS) {
          EEPROM.write(EVENTS_OFFSET, 0);
          lcd.clear();
          lcd.print("history deleted");
          delay(1000);
          lcd.clear();
          menu = MENU_MAIN;
          eventSelect=0;
        }
        break;
      case MENU_CLOCK:
        if (results.value == IR_ENTER_EQ) {
          menu = MENU_MAIN;
          menuSelect = MENU_CLOCK;
          lcd.clear();
        } else if (results.value == IR_SET_MODE) {
          menu = MENU_SET_TIME;
          lcd.clear();
        }
        break;
      case MENU_DATE:
        if (results.value == IR_ENTER_EQ) {
          menu = MENU_MAIN;
          menuSelect = MENU_DATE;
          lcd.clear();
        } else if (results.value == IR_SET_MODE) {
          menu = MENU_SET_DATE;
          lcd.clear();
        }
        break;
      case MENU_SET_TIME...MENU_SET_DATE:
        static int i = 0;
        digit = keyToInt(results.value);
        if (results.value == IR_AGAIN_REW) {
          lcd.clear();
          i = 0;
          for (int j = 0; j < 6; j++)
            numbers[j] = 0;
        }
        else if (results.value == IR_ENTER_EQ) {
          if (menu == MENU_SET_TIME) {
            myTime.hour = numbers[0] * 10 + numbers[1];
            myTime.minutes = numbers[2] * 10 + numbers[3];
            sec = numbers[4] * 10 + numbers[5];
            myTime.seconds = sec;
            menu = MENU_CLOCK;
          } else {
            myDate.year = numbers[0] * 10 + numbers[1];
            myDate.month = numbers[2] * 10 + numbers[3];
            myDate.day = numbers[4] * 10 + numbers[5];
            
            if (myDate.month > 12)  myDate.month=12;
            if (myDate.day > 31)  myDate.month=31;
            
            menu = MENU_DATE;
          }
          i = 0;
          for (int j = 0; j < 6; j++)
            numbers[j] = 0;
          lcd.clear();
        }
        else if (digit >= 0 && digit <= 9) {
          numbers[i] = digit;
          i++;
        }
        break;
      case MENU_ALARM:
        if (results.value == IR_ON_OFF) {
          menu = MENU_MAIN;
          digitalWrite(BUZZ_PIN, LOW);
          lcd.clear();
          setAlarm = false;
        }
        break;
      case MENU_GIVE_PASSWORD...MENU_REQUIRE_ENG_PASS:
        //MENU_GIVE_PASSWORD =alarm alert in entry/exit uses alarmZone ; MENU_REQUIRE_PASSWORD =mode on entry zone ; MENU_REQUIRE_ENG_PASS =change eng pass/ configure a zone
        static int j = 0;
        digit = keyToInt(results.value);
        if (results.value == IR_ENTER_EQ) {
          j = 0;
          boolean same = true;
          for (int k = 0; k < 4; k++) {
            Serial.print(menu); Serial.print(" "); Serial.print(numbers[k]); Serial.println(eng_pass[k]);
            if (menu == MENU_REQUIRE_ENG_PASS) {
              if (numbers[k] != eng_pass[k])
                same = false;
            } else if (menu == MENU_GIVE_PASSWORD) {
              if (numbers[k] != zones[alarmZone].param->password[k]) same = false;
            } else if (menu == MENU_REQUIRE_PASSWORD) {
              if (numbers[k] != zones[index].param->password[k]) same = false;
            }
          }
          if (same == false) {
            Serial.print("WRONG PASSWORD!");
            if (menu == MENU_GIVE_PASSWORD) {
              countDown = false;
              setAlarm = true;
              saveEventToEEPROM(alarmZone);
              menu = MENU_ALARM;
              lcd.clear();
            } else {
              lcd.clear();
              if (menu == MENU_REQUIRE_PASSWORD)
                lcd.print("Wrong pass, lads!");
              else
                lcd.print("You NOT engineer");
              delay(1000);
              lcd.clear();
              if (menu == MENU_REQUIRE_ENG_PASS) menu = MENU_MAIN;
              else menu = MENU_ZONES;  // two cases: MENU_REQUIRE_PASS(plus) && MENU_GIVE_PASS(alert on entry/exit zone)
            }
          } else {
            Serial.print("GOOD PASSWORD!");
            if (menu == MENU_GIVE_PASSWORD) {
              lcd.clear();
              lcd.print("Hello Master!");
              delay(1000);
              countDown = false;
              menu = MENU_MAIN;
              lcd.clear();
              setAlarm = false;
              digitalWrite(BUZZ_PIN, LOW);
            }
            else if (menu == MENU_REQUIRE_PASSWORD) {
              menu = MENU_SET_PASS;
              lcd.clear();
            }
            else if (menu == MENU_REQUIRE_ENG_PASS) {
              if (changeEngPass) menu = MENU_SET_ENG_PASS;
              else menu = MENU_SELECT_TYPE;
              lcd.clear();
            }
          }
        }
        if (digit >= 0 && digit <= 9) {
          numbers[j] = digit;
          lcd.setCursor(j, 1);
          lcd.print("*");
          Serial.println(digit);
          j++;
        }
        break;
      case MENU_SET_PASS:
        digit = keyToInt(results.value);
        if (results.value == IR_ENTER_EQ) {
          for (int k = 0; k < 4; k++) {
            zones[index].param->password[k] = numbers[k];
          }
          menu = MENU_SET_LIMIT;
          lcd.clear();
          i = 0;
        } else if (digit >= 0 && digit <= 9) {
          lcd.setCursor(i, 1);
          lcd.print(digit);
          numbers[i] = digit;
          i++;
        }
        break;
      case MENU_SET_ENG_PASS:
        digit = keyToInt(results.value);
        if (results.value == IR_ENTER_EQ) {
          for (int k = 0; k < 4; k++) {
            eng_pass[k] = numbers[k];
          }
          menu = MENU_MAIN;
          changeEngPass = false;
          saveEngToEEPROM();
          lcd.clear();
          i = 0;
        } else if (digit >= 0 && digit <= 9) {
          lcd.setCursor(i, 1);
          lcd.print(digit);
          numbers[i] = digit;
          i++;
        }
        break;
      case MENU_SET_LIMIT:
        digit = keyToInt(results.value);
        if (results.value == IR_ENTER_EQ) {
          zones[index].param->timeLimit = numbers[0] * 10 + numbers[1];
          lcd.clear();
          lcd.print("Changes saved");
          delay(1000);
          menu = MENU_ZONES;
          lcd.clear();
          i = 0;
          settingZone = -1;
          saveZonesToEEPROM();
        } else if (digit >= 0 && digit <= 9) {
          lcd.setCursor(i, 1);
          lcd.print(digit);
          numbers[i] = digit;
          i++;
        }
        break;
      case MENU_SET_THRESHOLD:
        digit = keyToInt(results.value);
        if (results.value == IR_ENTER_EQ) {
          int value = 0;
          for (int k = 0; k < i; k++) {
            value = value * 10 + numbers[k];
          }
          zones[index].value = value;
          menu = MENU_ZONES;
          settingZone = -1;
          lcd.clear();
          i = 0;
          saveZonesToEEPROM();
        } else if (digit >= 0 && digit <= 9) {
          lcd.setCursor(i, 1);
          lcd.print(digit);
          numbers[i] = digit;
          i++;
        }
        break;
      case MENU_SET_DIGITAL:
        digit = keyToInt(results.value);
        if (digit == 0 || digit == 1) {
          zones[index].value = digit;
          menu = MENU_ZONES;
          settingZone = -1;
          lcd.clear();
          i = 0;
          saveZonesToEEPROM();
        }
       break;
      default: break;
    }
    irrecv.resume();
  }
}

// interrupt service routine for counting seconds
ISR(TIMER1_COMPA_vect) {
  sec++;
  if (countDown) {
    counter--;
  }
}

boolean debounce(int pin) {
  if (digitalRead(pin)) {
    delay(MAX_BOUNCE_TIME);
    return digitalRead(pin);
  }
  return false;
}

// function that checks if the alarm activate conditions are met on one of the zones
// activate conditions depend on zones types 
void checkZones() {
  for (int i = 0; i < 4; i++) {
    byte stateNow = 0;
    if (zones[i].pin == ANALOG_PIN) {
      int readPot = analogRead(zones[i].pin);
      if (readPot > 512) stateNow = HIGH;
      else stateNow = LOW;
    } else stateNow = debounce(zones[i].pin);

    if (i != settingZone) {
      if (zones[i].type == 'E') {
        if (!countDown && (stateNow == zones[i].value)) {
          Serial.print("zone E: countDown="); Serial.print(countDown); Serial.print("pin="); Serial.println(debounce(zones[i].pin));
          counter = zones[i].param->timeLimit;
          countDown = true;
          menu = MENU_GIVE_PASSWORD;
          alarmZone = i;
          lcd.clear();
        }
        if (countDown && counter == 0) {
          countDown = false;
          setAlarm = true;
          //-----create event
          saveEventToEEPROM(i);
          menu = MENU_ALARM;
          lcd.clear();
        }
        if (countDown) {
          digitalWrite(BUZZ_PIN, HIGH);
          delay(200);
          digitalWrite(BUZZ_PIN, LOW);
          delay(200);
        }
      }
      else if (zones[i].type == 'C') {
        if (stateNow == zones[i].value) {
          if (!setAlarm) {
            menu = MENU_ALARM;
            setAlarm = true;
            saveEventToEEPROM(i);
            lcd.clear();
            alarmZone = i;
          }
        }
      }
      else if (zones[i].type == 'A') {
        int val = analogRead(zones[i].pin);
        if (val >= zones[i].value) {
          if (!setAlarm) {
            menu = MENU_ALARM;
            setAlarm = true;
            saveEventToEEPROM(i);
            lcd.clear();
            alarmZone = i;
          }
        }
      }
      else if (zones[i].type == 'D') {
        if (stateNow == zones[i].value) {
          Serial.print("sunt D si cred debounce(zones[i].pin) == zones[i].value D=");
          Serial.println(zones[i].value);
          if (!setAlarm) {
            menu = MENU_ALARM;
            setAlarm = true;
            saveEventToEEPROM(i);
            lcd.clear();
            alarmZone = i;
          }
        }
      }
    }
  }
}

// very clean loop
void loop() {
  printOnLCD();
  performTask();
  checkZones();
}
