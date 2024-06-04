#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <RTClib.h>

#define codeVersion "0.5a"

// OLED FeatherWing buttons
#define BUTTON_A 15
#define BUTTON_B 32
#define BUTTON_C 14

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
RTC_PCF8523 rtc;

int dateTimeSelection = 0;  // To track the current field being edited
DateTime newDateTime;
bool isSettingDateTime = false;

enum MenuState {
  HOME_MENU,
  FILE_SELECT_MENU,
  SETTINGS_MENU,
  RS232_SETTINGS_MENU,
  RS232_FLOW_CONTROL_MENU,
  RS232_PARITY_MENU,
  RS232_DUPLEX_MENU,
  RS232_BAUD_RATE_MENU,
  RS232_BIT_COUNT_MENU,
  OLED_TIMEOUT_MENU,
  RECONNECT_SD_MENU,
  DATE_TIME_MENU,
  FILE_TRANSMIT_MENU,
  WIFI_MENU,
  ABOUT_MENU,
  ABOUT_MENU_2,
  FILE_SELECT_ERROR
};

MenuState currentMenu = HOME_MENU;
MenuState previousMenu = HOME_MENU;
MenuState menuStack[10];
int menuStackIndex = 0;

volatile bool buttonCPressed = false;
int currentSelection = 0;
int rs232Cursor = 0;     // Cursor for RS232 Settings Menu
int settingsCursor = 0;  // Cursor for Settings Menu
int fileCursor = 0;
int oledTimeout = 30;
int tempOledTimeout = 30;
const int fileLimit = 255;
String files[fileLimit];
bool isDirectory[fileLimit];
int fileCount = 0;
String selectedFile;
String currentDirectory = "/";
unsigned long lastButtonPressTime = 0;
unsigned long lastButtonCTime = 0;        // For debouncing button C
const unsigned long debounceDelay = 200;  // 200 milliseconds debounce delay
bool oledOn = true;
bool wifiEnable = false;

int baudRates[] = { 300, 600, 750, 1200, 2400, 4800, 9600, 19200, 31250, 38400, 57600, 74880, 115200, 230400, 250000, 460800, 500000, 921600, 1000000, 2000000 };
int baudRateIndex = 6;  // Default 9600
int tempBaudRateIndex = 6;
int parityIndex = 0;  // 0 - None, 1 - Even, 2 - Odd
int tempParityIndex = 0;
int duplexIndex = 1;  // 0 - Half, 1 - Full
int tempDuplexIndex = 1;
int flowControlIndex = 3;  // 0 - None, 1 - XON/XOFF, 2 - ENQ/ACK, 3 - Hardware
int tempFlowControlIndex = 3;
int bitCountIndex = 1;  // 0 - 7-bit, 1 - 8-bit
int tempBitCountIndex = 1;

void IRAM_ATTR handleButtonC() {
  unsigned long currentTime = millis();
  if (currentTime - lastButtonCTime > debounceDelay) {
    buttonCPressed = true;
    lastButtonCTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1)
      ;
  }

  if (!rtc.initialized() || rtc.lostPower()) {
    Serial.println("RTC is not initialized, setting to compile date/time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  newDateTime = rtc.now();

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_C), handleButtonC, FALLING);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.display();
  delay(1000);

  display.clearDisplay();
  display.display();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();

  if (!SD.begin()) {
    Serial.println("Card Mount Failed");
    currentMenu = FILE_SELECT_ERROR;
  } else {
    listFiles(currentDirectory);
  }

  showMenu();
}

void loop() {
  if (oledOn) {
    if (!digitalRead(BUTTON_A) || !digitalRead(BUTTON_B) || buttonCPressed) {
      handleButtonPress();
      lastButtonPressTime = millis();
      buttonCPressed = false;
    }

    if (millis() - lastButtonPressTime > oledTimeout * 1000) {
      oledOn = false;
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      display.clearDisplay();
      display.display();
    }
  } else {
    if (!digitalRead(BUTTON_A) || !digitalRead(BUTTON_B) || buttonCPressed) {
      oledOn = true;
      display.ssd1306_command(SSD1306_DISPLAYON);
      showMenu();
      lastButtonPressTime = millis();
      buttonCPressed = false;
      delay(200);  // Prevent the initial button press from interacting with the menu
    }
  }
  delay(200);
}

void handleButtonPress() {
  if (!digitalRead(BUTTON_A)) {
    handleButtonA();
  } else if (!digitalRead(BUTTON_B)) {
    handleButtonB();
  } else if (buttonCPressed) {
    handleButtonCPress();
  }
}

void showMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);

  switch (currentMenu) {
    case HOME_MENU:
      display.println(currentSelection == 0 ? "> File Select" : "  File Select");
      display.println(currentSelection == 1 ? "> Settings" : "  Settings");
      display.println(currentSelection == 2 ? "> About" : "  About");
      break;
    case FILE_SELECT_MENU:
      showFileSelectMenu();
      break;
    case SETTINGS_MENU:
      showSettingsMenu();
      break;
    case RS232_SETTINGS_MENU:
      showRS232SettingsMenu();
      break;
    case RS232_BAUD_RATE_MENU:
      showBaudRateMenu();
      break;
    case RS232_PARITY_MENU:
      showParityMenu();
      break;
    case RS232_DUPLEX_MENU:
      showDuplexMenu();
      break;
    case RS232_FLOW_CONTROL_MENU:
      showFlowControlMenu();
      break;
    case RS232_BIT_COUNT_MENU:
      showBitCountMenu();
      break;
    case OLED_TIMEOUT_MENU:
      display.print("Set OLED Timeout:\nCurrent: ");
      display.print(oledTimeout);
      display.println("s");
      break;
    case RECONNECT_SD_MENU:
      SD.end();
      for (int i = 0; i < fileLimit; i++) {
        files[i] = "";
      }
      delay(10);
      if (SD.begin()) {
        display.print("SD card connected");
      } else {
        display.print("SD card connection failed or not present");
      }
      SD.end();
      break;
    case DATE_TIME_MENU:
      showDateTimeMenu();  // Call the function to display the date/time menu
      break;
    case WIFI_MENU:
      showWiFiMenu();
      break;
    case FILE_TRANSMIT_MENU:
      display.print("Transmitting: ");
      display.println(selectedFile);
      display.println("Progress: 0%");
      break;
    case ABOUT_MENU:
      display.println("RS232 Drip Feeder");
      display.println("By: Zachary Goode");
      display.print("Version: ");
      display.println(codeVersion);
      break;
    case ABOUT_MENU_2:
      display.println("Source Code:");
      display.println("https://github.com/ZGoode/RS-232-DripFeeder");
      break;
    case FILE_SELECT_ERROR:
      display.println("SD card failed, or not present");
      break;
  }

  display.display();
}

void showBaudRateMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Baud Rate:\n");
  display.print(baudRates[tempBaudRateIndex]);
  display.display();
}

void showParityMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Parity:\n");
  switch (tempParityIndex) {
    case 0:
      display.print("None");
      break;
    case 1:
      display.print("Even");
      break;
    case 2:
      display.print("Odd");
      break;
  }
  display.display();
}

void showDuplexMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Duplex:\n");
  switch (tempDuplexIndex) {
    case 0:
      display.print("Full");
      break;
    case 1:
      display.print("Half");
      break;
  }
  display.display();
}

void showFlowControlMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Flow Control:\n");
  switch (tempFlowControlIndex) {
    case 0:
      display.print("None");
      break;
    case 1:
      display.print("XON/XOFF");
      break;
    case 2:
      display.print("ENQ/ACK");
      break;
    case 3:
      display.print("Hardware");
      break;
  }
  display.display();
}

void showBitCountMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Bit Count:\n");
  switch (tempBitCountIndex) {
    case 0:
      display.print("7-Bit");
      break;
    case 1:
      display.print("8-Bit");
      break;
  }
  display.display();
}

void showFileSelectMenu() {
  int visibleRows = 4;
  int rowHeight = 8;  // Each row is 8 pixels high
  int displayHeight = visibleRows * rowHeight;
  int cursorY = 0;
  display.clearDisplay();

  for (int i = 0; i < visibleRows && (fileCursor + i) < fileCount; i++) {
    if (fileCursor + i == currentSelection) {
      display.fillRect(0, cursorY, display.width() - 5, rowHeight, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(0, cursorY);
    display.println(files[fileCursor + i]);
    cursorY += rowHeight;
  }

  // Draw scrollbar
  display.drawRect(display.width() - 5, 0, 5, displayHeight, SSD1306_WHITE);
  display.drawLine(display.width() - 2, 0, display.width() - 2, displayHeight, SSD1306_WHITE);
  int scrollBarHeight = displayHeight * visibleRows / fileCount;
  int scrollBarPos = (displayHeight - scrollBarHeight) * fileCursor / (fileCount - visibleRows);
  display.fillRect(display.width() - 4, scrollBarPos, 2, scrollBarHeight, SSD1306_WHITE);

  display.display();
}

void showSettingsMenu() {
  display.clearDisplay();
  int visibleRows = 4;  // Number of rows that can be displayed at once
  int rowHeight = 8;    // Height of each row in pixels
  int totalRows = 5;    // Total number of menu items
  int displayHeight = visibleRows * rowHeight;
  int cursorY = 0;

  // Adjust cursor and text display for scrolling
  for (int i = 0; i < visibleRows && (settingsCursor + i) < totalRows; i++) {
    display.setCursor(0, cursorY);
    switch (settingsCursor + i) {
      case 0:
        display.println(settingsCursor + i == currentSelection ? "> RS232 Settings" : "  RS232 Settings");
        break;
      case 1:
        display.println(settingsCursor + i == currentSelection ? "> OLED Timeout" : "  OLED Timeout");
        break;
      case 2:
        display.println(settingsCursor + i == currentSelection ? "> Reconnect SD" : "  Reconnect SD");
        break;
      case 3:
        display.println(settingsCursor + i == currentSelection ? "> Date/Time" : "  Date/Time");
        break;
      case 4:
        display.println(settingsCursor + i == currentSelection ? "> WiFi" : "  WiFi");
        break;
    }
    cursorY += rowHeight;
  }

  // Draw scrollbar
  display.drawRect(display.width() - 5, 0, 5, displayHeight, SSD1306_WHITE);
  display.drawLine(display.width() - 2, 0, display.width() - 2, displayHeight, SSD1306_WHITE);
  int scrollBarHeight = displayHeight * visibleRows / totalRows;
  int scrollBarPos = (displayHeight - scrollBarHeight) * settingsCursor / (totalRows - visibleRows);
  display.fillRect(display.width() - 4, scrollBarPos, 2, scrollBarHeight, SSD1306_WHITE);

  display.display();
}

void showRS232SettingsMenu() {
  display.clearDisplay();
  int visibleRows = 4;  // Number of rows that can be displayed at once
  int rowHeight = 8;    // Each row is 8 pixels high
  int totalRows = 5;    // Total number of menu items
  int displayHeight = visibleRows * rowHeight;
  int cursorY = 0;

  for (int i = 0; i < visibleRows && (rs232Cursor + i) < totalRows; i++) {
    display.setCursor(0, cursorY);
    switch (rs232Cursor + i) {
      case 0:
        display.println(rs232Cursor + i == currentSelection ? "> Baud Rate" : "  Baud Rate");
        break;
      case 1:
        display.println(rs232Cursor + i == currentSelection ? "> Parity" : "  Parity");
        break;
      case 2:
        display.println(rs232Cursor + i == currentSelection ? "> Duplex" : "  Duplex");
        break;
      case 3:
        display.println(rs232Cursor + i == currentSelection ? "> Flow Control" : "  Flow Control");
        break;
      case 4:
        display.println(rs232Cursor + i == currentSelection ? "> Bit Count" : "  Bit Count");
        break;
    }
    cursorY += rowHeight;
  }

  // Draw scrollbar
  display.drawRect(display.width() - 5, 0, 5, displayHeight, SSD1306_WHITE);
  display.drawLine(display.width() - 2, 0, display.width() - 2, displayHeight, SSD1306_WHITE);
  int scrollBarHeight = displayHeight * visibleRows / totalRows;
  int scrollBarPos = (displayHeight - scrollBarHeight) * rs232Cursor / (totalRows - visibleRows);
  display.fillRect(display.width() - 4, scrollBarPos, 2, scrollBarHeight, SSD1306_WHITE);

  display.display();
}

void showWiFiMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(currentSelection == 0 ? "> Status" : "  Status");
  if (wifiEnable) {
    display.println(currentSelection == 1 ? "> Enabled" : "  Enabled");
  } else {
    display.println(currentSelection == 1 ? "> Disabled" : "  Disabled");
  }
  display.println(currentSelection == 2 ? "> Reset" : "  Reset");
  display.display();
}

void showDateTimeMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);

  DateTime now = rtc.now();  // Get the current date/time

  if (isSettingDateTime) {
    display.print("Set Date/Time:\n");

    display.print("Year: ");
    display.print(newDateTime.year());
    if (dateTimeSelection == 0) display.print(" <");
    display.print("\n");

    display.print("Month: ");
    display.print(newDateTime.month());
    if (dateTimeSelection == 1) display.print(" <");
    display.print("\n");

    display.print("Day: ");
    display.print(newDateTime.day());
    if (dateTimeSelection == 2) display.print(" <");
    display.print("\n");

    display.print("Hour: ");
    display.print(newDateTime.hour());
    if (dateTimeSelection == 3) display.print(" <");
    display.print("\n");

    display.print("Minute: ");
    display.print(newDateTime.minute());
    if (dateTimeSelection == 4) display.print(" <");
    display.print("\n");
  } else {
    display.print("Date/Time:\n");
    display.print(now.year(), DEC);
    display.print('/');
    display.print(now.month(), DEC);
    display.print('/');
    display.print(now.day(), DEC);
    display.print(" ");
    display.print(now.hour(), DEC);
    display.print(':');
    display.print(now.minute(), DEC);
    display.print(':');
    display.print(now.second(), DEC);
  }

  display.display();

  // Debug prints to check function call and variable values
  Serial.print("isSettingDateTime: ");
  Serial.println(isSettingDateTime);
  Serial.print("dateTimeSelection: ");
  Serial.println(dateTimeSelection);
  Serial.print("newDateTime: ");
  Serial.print(newDateTime.year());
  Serial.print('/');
  Serial.print(newDateTime.month());
  Serial.print('/');
  Serial.print(newDateTime.day());
  Serial.print(" ");
  Serial.print(newDateTime.hour());
  Serial.print(':');
  Serial.print(newDateTime.minute());
  Serial.println();
  Serial.println("showDateTimeMenu called");
}

void enterDateTimeMenu() {
  isSettingDateTime = false;  // or true, based on your control logic
  dateTimeSelection = 0;      // Start with the year
  newDateTime = rtc.now();    // Initialize with the current date and time
  currentMenu = DATE_TIME_MENU;
  showDateTimeMenu();  // Call to show the initial DateTime menu
}

void listFiles(String directory) {
  fileCount = 0;
  File root = SD.open(directory);

  if (!root) {
    Serial.println("Failed to open directory");
    currentMenu = FILE_SELECT_ERROR;
    return;
  }

  File file = root.openNextFile();
  while (file && fileCount < fileLimit) {
    String fileName = String(file.name());
    if (fileName != "System Volume Information") {
      if (file.isDirectory()) {
        files[fileCount] = "[DIR] " + fileName;
      } else {
        files[fileCount] = fileName;
      }
      isDirectory[fileCount] = file.isDirectory();
      fileCount++;
    }
    file = root.openNextFile();
  }
  file.close();

  if (fileCount == 0) {
    files[fileCount] = "No Files";
    isDirectory[fileCount] = false;
    fileCount++;
  }
}

void handleButtonA() {
  if (currentMenu == OLED_TIMEOUT_MENU) {
    oledTimeout += 5;
    if (oledTimeout > 120) {
      oledTimeout = 10;
    }
  } else if (currentMenu == RS232_BAUD_RATE_MENU) {
    tempBaudRateIndex = (tempBaudRateIndex + 1) % (sizeof(baudRates) / sizeof(baudRates[0]));
    showBaudRateMenu();
  } else if (currentMenu == RS232_PARITY_MENU) {
    tempParityIndex = (tempParityIndex + 1) % 3;  // There are 3 parity options: None, Even, Odd
    showParityMenu();
  } else if (currentMenu == RS232_DUPLEX_MENU) {
    tempDuplexIndex = (tempDuplexIndex + 1) % 2;  // There are 2 duplex options: Half, Full
    showDuplexMenu();
  } else if (currentMenu == RS232_FLOW_CONTROL_MENU) {
    tempFlowControlIndex = (tempFlowControlIndex + 1) % 4;  // There are 4 flow control options: None, XON/XOFF, ENQ/ACK, Hardware
    showFlowControlMenu();
  } else if (currentMenu == RS232_BIT_COUNT_MENU) {
    tempBitCountIndex = (tempBitCountIndex + 1) % 2;  // There are 2 bit count options: 7-bit, 8-bit
    showBitCountMenu();
  } else if (currentMenu == SETTINGS_MENU) {
    if (currentSelection < getCurrentMenuSize() - 1) {
      currentSelection++;
      if (currentSelection >= settingsCursor + 4) {
        settingsCursor++;
      }
    } else {
      currentSelection = 0;  // Loop around to the top
      settingsCursor = 0;
    }
  } else if (currentMenu == RS232_SETTINGS_MENU) {
    if (currentSelection < getCurrentMenuSize() - 1) {
      currentSelection++;
      if (currentSelection >= rs232Cursor + 4) {
        rs232Cursor++;
      }
    } else {
      currentSelection = 0;  // Loop around to the top
      rs232Cursor = 0;
    }
  } else if (currentMenu == FILE_SELECT_MENU) {
    if (currentSelection < fileCount - 1) {
      currentSelection++;
      if (currentSelection >= fileCursor + 4) {
        fileCursor++;
      }
    } else {
      currentSelection = 0;  // Loop around to the top
      fileCursor = 0;
    }
  } else if (currentMenu == DATE_TIME_MENU && isSettingDateTime) {
    switch (dateTimeSelection) {
      case 0:
        newDateTime = DateTime(newDateTime.year() + 1, newDateTime.month(), newDateTime.day(),
                               newDateTime.hour(), newDateTime.minute());
        break;
      case 1:
        newDateTime = DateTime(newDateTime.year(), newDateTime.month() % 12 + 1, newDateTime.day(),
                               newDateTime.hour(), newDateTime.minute());
        break;
      case 2:
        newDateTime = DateTime(newDateTime.year(), newDateTime.month(), newDateTime.day() % 31 + 1,
                               newDateTime.hour(), newDateTime.minute());
        break;
      case 3:
        newDateTime = DateTime(newDateTime.year(), newDateTime.month(), newDateTime.day(),
                               (newDateTime.hour() + 1) % 24, newDateTime.minute());
        break;
      case 4:
        newDateTime = DateTime(newDateTime.year(), newDateTime.month(), newDateTime.day(),
                               newDateTime.hour(), (newDateTime.minute() + 1) % 60);
        break;
    }
  } else {
    if (currentSelection < getCurrentMenuSize() - 1) {
      currentSelection++;
    } else {
      currentSelection = 0;  // Loop around to the top
    }
  }
  showMenu();
}

int getCurrentMenuSize() {
  switch (currentMenu) {
    case HOME_MENU:
      return 3;  // File Select, Settings, About
    case FILE_SELECT_MENU:
      return fileCount;
    case SETTINGS_MENU:
      return 5;  // RS232 Settings, OLED Timeout, Reconnect SD, Date/Time, WiFi
    case RS232_SETTINGS_MENU:
      return 5;  // Baud Rate, Parity, Duplex, Flow Control, Bit Count
    case RS232_BAUD_RATE_MENU:
      return sizeof(baudRates) / sizeof(baudRates[0]);
    case RS232_PARITY_MENU:
      return 3;  // None, Even, Odd
    case RS232_DUPLEX_MENU:
      return 2;  // Half, Full
    case RS232_FLOW_CONTROL_MENU:
      return 4;  // None, XON/XOFF, ENQ/ACK, Hardware
    case RS232_BIT_COUNT_MENU:
      return 2;  // 7-bit, 8-bit
    case OLED_TIMEOUT_MENU:
      return 1;  // Only one option to toggle timeout
    case RECONNECT_SD_MENU:
      return 0;  // Only one option to reconnect SD
    case DATE_TIME_MENU:
      return 1;  // Placeholder for Date/Time menu size
    case WIFI_MENU:
      return 3;  // Placeholder for WiFi menu size
    case FILE_TRANSMIT_MENU:
      return 1;  // Only one option for transmitting file
    case ABOUT_MENU:
      return 0;  // Only one screen for About
    case FILE_SELECT_ERROR:
      return 0;  // Error message screen
    default:
      return 0;  // Default case, should not happen
  }
}

void handleButtonB() {
  if (currentMenu == OLED_TIMEOUT_MENU) {
    currentMenu = SETTINGS_MENU;  // Save the new value and return to settings menu
  } else if (currentMenu == RS232_BAUD_RATE_MENU) {
    baudRateIndex = tempBaudRateIndex;
    currentMenu = RS232_SETTINGS_MENU;
  } else if (currentMenu == RS232_PARITY_MENU) {
    parityIndex = tempParityIndex;
    currentMenu = RS232_SETTINGS_MENU;
  } else if (currentMenu == RS232_DUPLEX_MENU) {
    duplexIndex = tempDuplexIndex;
    currentMenu = RS232_SETTINGS_MENU;
  } else if (currentMenu == RS232_FLOW_CONTROL_MENU) {
    flowControlIndex = tempFlowControlIndex;
    currentMenu = RS232_SETTINGS_MENU;
  } else if (currentMenu == RS232_BIT_COUNT_MENU) {
    bitCountIndex = tempBitCountIndex;
    currentMenu = RS232_SETTINGS_MENU;
  } else if (currentMenu == WIFI_MENU && currentSelection == 1) {
    wifiEnable = !wifiEnable;
    showMenu();
    return;
  } else if (currentMenu == DATE_TIME_MENU && isSettingDateTime) {

  } else {
    switch (currentMenu) {
      case HOME_MENU:
        switch (currentSelection) {
          case 0:
            currentMenu = FILE_SELECT_MENU;
            break;
          case 1:
            currentMenu = SETTINGS_MENU;
            break;
          case 2:
            currentMenu = ABOUT_MENU;
            break;
        }
        break;
      case SETTINGS_MENU:
        switch (currentSelection) {
          case 0:
            currentMenu = RS232_SETTINGS_MENU;
            break;
          case 1:
            currentMenu = OLED_TIMEOUT_MENU;
            tempOledTimeout = oledTimeout;
            break;
          case 2:
            currentMenu = RECONNECT_SD_MENU;
            break;
          case 3:
            currentMenu = DATE_TIME_MENU;
            break;
          case 4:
            currentMenu = WIFI_MENU;
            break;
        }
        break;
      case FILE_SELECT_MENU:
        if (isDirectory[currentSelection]) {
          currentDirectory += "/" + files[currentSelection];
          currentSelection = 0;
          fileCursor = 0;
          listFiles(currentDirectory);
        } else {
          selectedFile = files[currentSelection];
          currentMenu = FILE_TRANSMIT_MENU;
        }
        break;
      case RS232_SETTINGS_MENU:
        switch (currentSelection) {
          case 0:
            currentMenu = RS232_BAUD_RATE_MENU;
            break;
          case 1:
            currentMenu = RS232_PARITY_MENU;
            break;
          case 2:
            currentMenu = RS232_DUPLEX_MENU;
            break;
          case 3:
            currentMenu = RS232_FLOW_CONTROL_MENU;
            break;
          case 4:
            currentMenu = RS232_BIT_COUNT_MENU;
            break;
        }
        break;
      case RS232_BAUD_RATE_MENU:
        baudRateIndex = currentSelection;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_PARITY_MENU:
        parityIndex = currentSelection;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_DUPLEX_MENU:
        duplexIndex = currentSelection;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_FLOW_CONTROL_MENU:
        flowControlIndex = currentSelection;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_BIT_COUNT_MENU:
        bitCountIndex = currentSelection;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case OLED_TIMEOUT_MENU:
        oledTimeout += 5;
        if (oledTimeout > 120) {
          oledTimeout = 10;
        }
        currentMenu = SETTINGS_MENU;
        break;
      case RECONNECT_SD_MENU:
        currentMenu = SETTINGS_MENU;
        break;
      case DATE_TIME_MENU:
        // Placeholder for date/time menu actions
        isSettingDateTime = !isSettingDateTime;
        if (!isSettingDateTime) {
          rtc.adjust(newDateTime);
        }
        break;
      case WIFI_MENU:
        // Placeholder for WiFi menu actions
        currentMenu = SETTINGS_MENU;
        break;
      case FILE_TRANSMIT_MENU:
        currentMenu = FILE_SELECT_MENU;
        break;
      case ABOUT_MENU:
        currentMenu = ABOUT_MENU_2;
        break;
      case ABOUT_MENU_2:
        currentMenu = ABOUT_MENU;
        break;
    }
  }
  currentSelection = 0;
  showMenu();
}

void handleButtonCPress() {
  if (buttonCPressed) {
    switch (currentMenu) {
      case HOME_MENU:
        // No action needed
        break;
      case FILE_SELECT_MENU:
        currentMenu = HOME_MENU;
        currentSelection = 0;
        fileCursor = 0;
        break;
      case SETTINGS_MENU:
        currentMenu = HOME_MENU;
        currentSelection = 0;
        settingsCursor = 0;
        break;
      case RS232_SETTINGS_MENU:
        currentMenu = SETTINGS_MENU;
        currentSelection = 0;
        rs232Cursor = 0;
        break;
      case RS232_BAUD_RATE_MENU:
        tempBaudRateIndex = baudRateIndex;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_PARITY_MENU:
        tempParityIndex = parityIndex;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_DUPLEX_MENU:
        tempDuplexIndex = duplexIndex;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_FLOW_CONTROL_MENU:
        tempFlowControlIndex = flowControlIndex;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case RS232_BIT_COUNT_MENU:
        tempBitCountIndex = bitCountIndex;
        currentMenu = RS232_SETTINGS_MENU;
        break;
      case OLED_TIMEOUT_MENU:
        currentMenu = SETTINGS_MENU;
        oledTimeout = tempOledTimeout;
        break;
      case RECONNECT_SD_MENU:
      case DATE_TIME_MENU:
        if (isSettingDateTime) {
          isSettingDateTime = false;
          rtc.adjust(newDateTime);
        } else {
          currentMenu = SETTINGS_MENU;
        }
        currentSelection = 0;
        break;
      case WIFI_MENU:
        currentMenu = SETTINGS_MENU;
        currentSelection = 0;
        break;
      case FILE_TRANSMIT_MENU:
        currentMenu = FILE_SELECT_MENU;
        currentSelection = 0;
        fileCursor = 0;
        break;
      case ABOUT_MENU:
        currentMenu = HOME_MENU;
        currentSelection = 0;
        break;
      case ABOUT_MENU_2:
        currentMenu = HOME_MENU;
        currentSelection = 0;
        break;
    }
    showMenu();
  }
}

void pushMenuState(MenuState state) {
  if (menuStackIndex < 10) {
    menuStack[menuStackIndex++] = state;
  }
}

MenuState popMenuState() {
  if (menuStackIndex > 0) {
    return menuStack[--menuStackIndex];
  }
  return HOME_MENU;
}
