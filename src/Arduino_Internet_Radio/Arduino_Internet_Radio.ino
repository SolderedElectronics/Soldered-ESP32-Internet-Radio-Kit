#include "Arduino.h"
#include "Audio.h"
#include "WiFi.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS_ImageReader.h>
#include "SPIFFS.h"

#include "include/welcomeScreen.h"
#include "include/speaker0.h"
#include "include/speaker1.h"
#include "include/speaker2.h"
#include "include/speaker3.h"
#include "include/wifi_icon2.h"
#include "include/wifi_icon1.h"
#include "include/wifi_icon0.h"

#include "include/Inter5pt7b.h"
#include "include/Inter8pt7b.h"
#include "include/Inter10pt7b.h"

#include <ESP32Time.h>

// Digital I/O used
#define SPI_MOSI          23
#define SPI_SCK           18
#define I2S_DOUT          25
#define I2S_BCLK          26
#define I2S_LRC           27
#define TFT_CS            15
#define TFT_DC            2
#define TFT_RST           14
#define TFT_PWM           4
#define ROTARY_PIN_A      33
#define ROTARY_PIN_B      32
#define ROTARY_PIN_BUTTON 13

AsyncWebServer server(80);
IPAddress localIP;
Audio *audio = new Audio;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
SPIFFS_ImageReader reader;
SPIFFS_Image Image;
ESP32Time rtc;

//String ssid = "Soldered";
//String password = "dasduino";

const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "links";
const char* PARAM_INPUT_4 = "names";
const char* PARAM_INPUT_5 = "station_to_delete";

String ssid;
String pass;
String links;
String names;
String station_to_delete;

const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* linksPath = "/links.txt";
const char* namesPath = "/names.txt";

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)

time_t last_menu_activity = 0, last_encode = 0, last_button_press = 0, last_button_release = 0, last_refresh = 0;
uint16_t timeout_menu = 15000, debounce_rotary_encoder = 25, debounce_button = 300, button_hold_timeout = 300;
int8_t volume = 0, menu_index = 0, backlight_pwm = 5, time_offset = 1, prev_time_offset = 1;
uint8_t stations_number = 0, flags_reg = 0b00000100, text_color = 0, bckgrnd_color = 1, current_station_index = 0;
bool flag_update_stations = 0, flag_reset = 0, flag_long_pressed = 0;
uint8_t main_menu = 0; // 0 - none, 1- text color, 2- background color, 3 - backlight, 4 - TIMEZONe, 5- display ip
uint8_t menu = 0; //0 - none, 1 - station change, 2 - main menu

char station_name[22] = "", currently_playing[44] = "";

uint16_t colors[] = { 0x0000, 0xFFFF, 0xF800, 0x2DD5, 0x001F, 0x07FF, 0x5970, 0xBD4E, 0xFC00 };

char color_names[][8] PROGMEM = { "Black", "White", "Red", "Green", "Blue", "Cyan", "Purple", "Yellow", "Orange" };

char *stream_links[48];

char *current_station[24];

struct tm timeinfo;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;
time_t epoch;

void setup()
{
    ledcSetup(0, 32000, 8);
    ledcAttachPin(TFT_PWM, 0);
    ledcWrite(0, 250 - backlight_pwm * 25);
    Serial.begin(115200);
    initSPIFFS();
    ssid = readFile(SPIFFS, ssidPath);
    pass = readFile(SPIFFS, passPath);
    Serial.println(ssid);
    Serial.println(pass);
    SPI.begin(SPI_SCK, -1, SPI_MOSI, -1);
    tft.initR(INITR_BLACKTAB);//initialize a ST7735S chip, black tab
    turnOnScreen();

    if (initWiFi()) {
        // Route for root / web page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
            request->send(SPIFFS, "/index.html", "text/html");
        });

        server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest * request) {
            request->send(SPIFFS, "/wifimanager.html", "text/html");
        });

        server.on("/stations", HTTP_GET, [](AsyncWebServerRequest * request) {
            request->send(SPIFFS, "/stations.html", "text/html");
        });

        server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest * request) {
            request->send(SPIFFS, "/favicon.ico", "image/x-icon");
        });

        server.on("/inter.ttf", HTTP_GET, [](AsyncWebServerRequest * request) {
            request->send(SPIFFS, "/Inter.ttf", "file/ttf");
        });

        server.serveStatic("/", SPIFFS, "/");

        //Radio stations fetch
        server.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
            int params = request->params();
            for (int i = 0; i < params; i++) {
                AsyncWebParameter* p = request->getParam(i);
                if (p->isPost()) {
                    if (p->name() == PARAM_INPUT_1) {
                        ssid = p->value().c_str();
                        Serial.print("SSID set to: ");
                        Serial.println(ssid);
                        // Write file to save value
                        writeFile(SPIFFS, ssidPath, ssid.c_str());
                        flag_reset = 1;
                    }
                    // HTTP POST pass value
                    else if (p->name() == PARAM_INPUT_2) {
                        pass = p->value().c_str();
                        Serial.print("Password set to: ");
                        Serial.println(pass);
                        // Write file to save value
                        writeFile(SPIFFS, passPath, pass.c_str());
                        flag_reset = 1;
                    }
                    // HTTP POST ssid value
                    else if (p->name() == PARAM_INPUT_3) {
                        links = p->value().c_str();
                        // Write file to save value
                        File fileToWrite = SPIFFS.open(linksPath, FILE_APPEND);
                        fileToWrite.println(links);
                        fileToWrite.close();
                        flags_reg |= (1 << 2);
                        flag_update_stations = 1;
                    }
                    // HTTP POST pass value
                    else if (p->name() == PARAM_INPUT_4) {
                        names = p->value().c_str();
                        // Write file to save value
                        File fileToWrite = SPIFFS.open(namesPath, FILE_APPEND);
                        fileToWrite.println(names);
                        fileToWrite.close();
                        flags_reg |= (1 << 2);
                        flag_update_stations = 1;
                        request->send(SPIFFS, "/index.html", "text/html");
                    }
                    else if (p->name() == PARAM_INPUT_5) {
                        deleteStation(atoi(p->value().c_str()));
                        getStations();
                        if (atoi(p->value().c_str()) < current_station_index)
                        {
                            current_station_index--;
                            if (menu == 1)
                            {
                                menu_index--;
                            }
                        }
                        flags_reg |= (1 << 2);
                        if (atoi(p->value().c_str()) == current_station_index + 1)
                        {
                            setStation();
                        }
                        request->send(SPIFFS, "/stations.html", "text/html");
                    }
                    //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
                }
            }
            if (flag_update_stations)
            {
                flag_update_stations = 0;
                getStations();
                deleteStation(255); //Remove invalid stations, if 255 is replaced with any other number
                //function will remove station with that index
            }
            else if (flag_reset)
            {
                request->send(200, "text/plain", "Done. ESP will restart, connect to your router and check IP address of ESP32 ");
                ESP.restart();
            }
            delay(3000);
        });

        server.begin();
    }
    else {
        // Connect to Wi-Fi network with SSID and password
        Serial.println("Setting AP (Access Point)");
        // NULL sets an open Access Point
        WiFi.softAP("INTERNET-RADIO", NULL);

        IPAddress IP = WiFi.softAPIP();
        Serial.print("AP IP address: ");
        Serial.println(IP);

        // Web Server Root URL
        server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
            request->send(SPIFFS, "/wifimanager.html", "text/html");
        });

        server.serveStatic("/", SPIFFS, "/");

        server.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
            int params = request->params();
            for (int i = 0; i < params; i++) {
                AsyncWebParameter* p = request->getParam(i);
                if (p->isPost()) {
                    // HTTP POST ssid value
                    if (p->name() == PARAM_INPUT_1) {
                        ssid = p->value().c_str();
                        Serial.print("SSID set to: ");
                        Serial.println(ssid);
                        // Write file to save value
                        writeFile(SPIFFS, ssidPath, ssid.c_str());
                    }
                    // HTTP POST pass value
                    if (p->name() == PARAM_INPUT_2) {
                        pass = p->value().c_str();
                        Serial.print("Password set to: ");
                        Serial.println(pass);
                        // Write file to save value
                        writeFile(SPIFFS, passPath, pass.c_str());
                    }
                    //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
                }
            }
            tft.fillScreen(0xFFFF);
            tft.setTextColor(0x0000);
            tft.setFont(&Inter5pt7b);
            tft.setCursor(10, 17);
            tft.print("SSID and password are ");
            tft.setCursor(10, 27);
            tft.print("configured. Restarting..");
            delay(3000);
            ESP.restart();
        });
        server.begin();
        tft.fillScreen(0xFFFF);
        tft.setTextColor(0x0000);
        tft.setFont(&Inter5pt7b);
        tft.setCursor(10, 17);
        tft.print("Cannot connect to WiFi.");
        tft.setCursor(10, 27);
        tft.print("Please connect to WiFi ");
        tft.setCursor(10, 37);
        tft.print("network \"INTERNET ");
        tft.setCursor(10, 47);
        tft.print("RADIO\" with your ");
        tft.setCursor(10, 57);
        tft.print("phone or PC and open");
        tft.setCursor(10, 67);
        tft.print("192.168.4.1 in your");
        tft.setCursor(10, 77);
        tft.print("web browser and enter");
        tft.setCursor(10, 87);
        tft.print("your network SSID and ");
        tft.setCursor(10, 97);
        tft.print("password and Submit.");
        while (1);
    }
    audio->setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio->setVolume(volume); // 0...21
    pinMode(ROTARY_PIN_BUTTON, INPUT_PULLUP);
    pinMode(ROTARY_PIN_A, INPUT_PULLUP);
    pinMode(ROTARY_PIN_B, INPUT_PULLUP);
    delay(5);
    attachInterrupt(ROTARY_PIN_A, volumeSet, CHANGE);
    attachInterrupt(ROTARY_PIN_BUTTON, buttonPress, CHANGE);

    getTimeNTP();

    getStations();
    if (stations_number)
    {
        setStation();
    }
}

void getTimeNTP()
{
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    getLocalTime(&timeinfo);
    time(&epoch);
    Serial.println(time_offset);
    Serial.println(epoch);
    Serial.println(time_offset * 3600ULL + epoch);
    Serial.println(epoch + time_offset * 3600ULL);
    rtc.setTime(epoch + time_offset * 3600ULL);
}

void loop()
{
    audio->loop();
    if ((last_menu_activity + timeout_menu < millis()) && menu)
    {
        //flag_opened_menu = 0;
        menu = 0;
        main_menu = 0;
        //rotary_change = 1;
        flags_reg |= (1 << 2);
    }

    if (flags_reg & 1)
    {
        if (menu == 2 && main_menu == 0) //Button press and menu = 11
        {
            switch (menu_index)
            {
                case 0:
                    main_menu = 1;
                    break;
                case 1:
                    main_menu = 2;
                    break;
                case 2:
                    main_menu = 3;
                    break;
                case 3:
                    main_menu = 4;
                    break;
                case 4:
                    main_menu = 5;
                    break;
                case 5:
                    menu = 0;
                    menu_index = 0;
                    break;
            }
        }
        else if (menu == 2 && main_menu == 1)
        {
            if (menu_index != bckgrnd_color)
            {
                text_color = menu_index;
            }
            else
            {
                bckgrnd_color = text_color;
                text_color = menu_index;
            }
            main_menu = 0;
        }
        else if (menu == 2 && main_menu == 2)
        {
            if (menu_index != text_color)
            {
                bckgrnd_color = menu_index;
            }
            else
            {
                text_color = bckgrnd_color;
                bckgrnd_color = menu_index;
            }
            main_menu = 0;
        }
        else if (menu == 2 && main_menu == 3)
        {
            main_menu = 0;
        }
        else if (menu == 2 && main_menu == 4)
        {
            main_menu = 0;
        }
        else if (menu == 2 && main_menu == 5)
        {
            main_menu = 0;
        }
        else if (menu == 1) //Button_press and menu = 01
        {
            menu = 0;
            //flag_button_pressed = 0;
            flags_reg &= ~(1);
            //rotary_change = 1;
            current_station_index = menu_index;
            if (stations_number)
            {
                setStation();
            }
        }

        else if (menu == 0) //Button_press and menu = 00
        {
            menu_index = 0;
            //flag_opened_menu = 1;
            menu = 1;
            main_menu = 0;
        }
        menu_index = 0;
        flags_reg |= (1 << 2);
        flags_reg &= 0b11111110;
    }

    if ((last_button_press + button_hold_timeout < millis()) && (digitalRead(ROTARY_PIN_BUTTON) == LOW) && (last_button_release + debounce_button < millis()) && menu != 2)
    {
        menu = 2;
        main_menu = 0;
        flags_reg |= (1 << 2);
    }

    if (flags_reg & (1 << 2))
    {
        if (menu == 1)
        {
            drawMenu();
            last_menu_activity = millis();
        }

        else if (menu == 2)
        {
            if (!main_menu)
                drawMainMenu();
            else
                drawMenuOption();
            last_menu_activity = millis();
        }
        else if (menu == 0)
        {
            audio->setVolume(volume); // 0...21
            drawInfoScreen();
        }
        //rotary_change = 0;
        flags_reg &= 0b11111011;
        last_refresh = millis();
    }

    if (millis() - last_refresh > 30000)
    {
        flags_reg |= (1 << 2);
    }

    //flag_prev_opened_menu = flag_opened_menu;
    //flags_reg = (flags_reg & 0b11100111) | ((flags_reg & 0b01100000) >> 2);
}

// Initialize WiFi
bool initWiFi() {
    Serial.println("initwifi");
    if (ssid == "") {
        Serial.println("Undefined SSID address.");
        return false;
    }

    WiFi.mode(WIFI_STA);

    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.println("Connecting to WiFi...");

    unsigned long currentMillis = millis();
    previousMillis = currentMillis;

    while (WiFi.status() != WL_CONNECTED) {
        currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            Serial.println("Failed to connect.");
            return false;
        }
    }

    Serial.println(WiFi.localIP());
    return true;
    Serial.println("initwifi");
}

void getStations()
{
    stations_number = 0;
    File linksFile, namesFile;
    linksFile = SPIFFS.open(linksPath, FILE_READ);
    namesFile = SPIFFS.open(namesPath, FILE_READ);
    while (linksFile.available() && namesFile.available())
    {
        char *t_links = (char*)ps_malloc(48);
        char *t_names = (char*)ps_malloc(24);
        linksFile.readBytesUntil('\n', t_links, 48 * sizeof(char));
        stream_links[stations_number] = t_links;
        namesFile.readBytesUntil('\n', t_names, 24 * sizeof(char));
        current_station[stations_number] = t_names;
        int i = 0;
        while (i < strlen(stream_links[stations_number]))
        {
            if (stream_links[stations_number][i] == '\r')
            {
                stream_links[stations_number][i] = '\0';
                break;
            }
            i++;
        }
        i = 0;
        while (i < strlen(current_station[stations_number]))
        {
            if (current_station[stations_number][i] == '\r')
            {
                current_station[stations_number][i] = '\0';
                break;
            }
            i++;
        }
        if (strlen(t_links) > 10 && strlen(t_names) > 4)
        {
            stations_number++;
        }

    }
    linksFile.close();
    namesFile.close();

    if (stations_number == 0)
    {
        char *t_links = (char*)ps_malloc(48);
        char *t_names = (char*)ps_malloc(24);
        strcpy(t_links, "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_one");
        strcpy(t_names, "BBC Radio 1");
        stream_links[stations_number] = t_links;
        current_station[stations_number] = t_names;
        stations_number++;
    }

    File stations_empty, stations;
    stations_empty = SPIFFS.open("/stations_empty.html", FILE_READ);
    SPIFFS.remove("/stations.html");
    stations = SPIFFS.open("/stations.html", FILE_APPEND);
    char buff[100];
    for (int i = 0; i < 17; i++)
    {
        stations_empty.readBytesUntil('\n', buff, 100 * sizeof(char));
        int j = 0;
        while (i < strlen(buff))
        {
            if (buff[j] == '\r')
            {
                buff[j + 1] = '\n';
                buff[j + 2] = '\0';
                break;
            }
            j++;
        }
        stations.print(buff);

    }
    for (int i = 0; i < stations_number; i++)
    {
        memset(buff, 0, 100 * sizeof(char));
        strcpy(buff, "\t\t\t<input type=\"radio\" id=\"");
        itoa(i, buff + strlen(buff), 10);
        strcpy(buff + (strlen(buff)), "\" name=\"station_to_delete\" value=\"");
        itoa(i, buff + strlen(buff), 10);
        strcpy(buff + (strlen(buff)), "\">\r\n");
        stations.print(buff);
        memset(buff, 0, 100 * sizeof(char));
        strcpy(buff, "\t\t\t<label for=\"");
        itoa(i, buff + strlen(buff), 10);
        strcpy(buff + (strlen(buff)), "\">");
        strcpy(buff + (strlen(buff)), current_station[i]);
        strcpy(buff + (strlen(buff)), "</label><br>\r\n");
        stations.print(buff);
    }
    stations.print("\t\t\t<input type =\"submit\" value =\"Delete station\">\r\n");
    while (stations_empty.available())
    {
        stations_empty.readBytesUntil('\n', buff, 100 * sizeof(char));
        int j = 0;
        while (j < strlen(buff))
        {
            if (buff[j] == '\r')
            {
                buff[j + 1] = '\n';
                buff[j + 2] = '\0';
                break;
            }
            j++;
        }
        stations.print(buff);
    }
    stations_empty.close();
    stations.close();
}

void deleteStation(uint8_t index)
{
    File file1, temp_file1, file2, temp_file2;
    file1 = SPIFFS.open(linksPath, FILE_READ);
    temp_file1 = SPIFFS.open("/temp_file1.txt", FILE_WRITE);
    file2 = SPIFFS.open(namesPath, FILE_READ);
    temp_file2 = SPIFFS.open("/temp_file2.txt", FILE_WRITE);
    char buff1[48], buff2[24];
    uint8_t counter = 0;
    while (file1.available() && file2.available())
    {
        memset(buff1, 0 , 48 * sizeof(char));
        memset(buff2, 0 , 24 * sizeof(char));
        file1.readBytesUntil('\n', buff1, 48 * sizeof(char));
        file2.readBytesUntil('\n', buff2, 24 * sizeof(char));
        if (counter != index && strlen(buff1) > 10 && strlen(buff2) > 4)
        {
            int j = 0;
            while (j < strlen(buff1))
            {
                if (buff1[j] == '\r')
                {
                    buff1[j + 1] = '\n';
                    buff1[j + 2] = '\0';
                    break;
                }
                j++;
            }
            j = 0;
            while (j < strlen(buff2))
            {
                if (buff2[j] == '\r')
                {
                    buff2[j + 1] = '\n';
                    buff2[j + 2] = '\0';
                    break;
                }
                j++;
            }
            temp_file1.print(buff1);
            temp_file2.print(buff2);
        }
        else
        {
            Serial.println("Skipped");
        }
        counter++;
    }
    SPIFFS.remove(linksPath);
    SPIFFS.remove(namesPath);
    SPIFFS.rename("/temp_file1.txt", linksPath);
    SPIFFS.rename("/temp_file2.txt", namesPath);
}

void turnOnScreen()
{
    tft.setRotation(3);
    tft.fillScreen(ST7735_WHITE);
    tft.drawBitmap(0, 0, welcomeScreen, 160, 128, ST7735_MAGENTA);
}

void drawInfoScreen()
{
    tft.setRotation(3);
    tft.setFont(&Inter10pt7b);
    tft.fillScreen(colors[bckgrnd_color]);
    tft.setCursor(100, 25);
    tft.setTextColor(colors[text_color]);
    rtc.getHour(true) < 10 ? tft.print("0") : 0;
    tft.print(rtc.getHour(true));
    tft.print(":");
    rtc.getMinute() < 10 ? tft.print("0") : 0;
    tft.print(rtc.getMinute());
    tft.setCursor(5, 70);
    tft.setFont(&Inter8pt7b);
    tft.println(station_name);
    tft.setCursor(5, 110);
    tft.setFont(&Inter5pt7b);
    tft.setCursor(15, 108);
    if (strlen(currently_playing))
        for (int i = 0; i < 22; i++)
        {
            if (*(currently_playing + i) == '\0') break;
            tft.print(*(currently_playing + i));
            if (i == 21)
            {
                tft.setCursor(15, 120);
                tft.println(currently_playing + i + 1);
            }
        }
    else
    {
        tft.print("No track info.");
    }
    //tft.drawRect(10, 105, 107, 15, bckgrnd_color == 4 ? colors[8] : colors[4]);
    //tft.fillRect(11, 106, 5 * volume, 13, bckgrnd_color == 4 ? colors[8] : colors[4]);
    if (WiFi.status() != WL_CONNECTED)
    {
        reader.drawBMP("/wifi_icon0.bmp", tft, 5, 6, true, colors[bckgrnd_color]);
    }
    else if (WiFi.RSSI() > -60)
    {
        reader.drawBMP("/wifi_icon3.bmp", tft, 5, 6, true, colors[bckgrnd_color]);
        //tft.drawBitmap(130, 10, wifi_icon2, 26, 18, bckgrnd_color == 8 ? colors[4] : colors[8]);
    }
    else if (WiFi.RSSI() > -90)
    {
        reader.drawBMP("/wifi_icon2.bmp", tft, 5, 6, true, colors[bckgrnd_color]);
        //tft.drawBitmap(130, 10, wifi_icon1, 26, 18, bckgrnd_color == 8 ? colors[4] : colors[8]);
    }
    else if (WiFi.RSSI() > -120)
    {
        reader.drawBMP("/wifi_icon1.bmp", tft, 5, 6, true, colors[bckgrnd_color]);
        //tft.drawBitmap(130, 10, wifi_icon0, 26, 18, bckgrnd_color == 8 ? colors[4] : colors[8]);
    }

    if (volume == 0)
    {
        reader.drawBMP("/speaker0_color.bmp", tft, 40, 2, true, colors[bckgrnd_color]);
        //tft.drawBitmap(121, 102, speaker0, 30, 23, bckgrnd_color == 4 ? colors[8] : colors[4]);
    }
    else if (volume < 8)
    {
        reader.drawBMP("/speaker1_color.bmp", tft, 40, 2, true, colors[bckgrnd_color]);
        //tft.drawBitmap(121, 102, speaker1, 30, 23, bckgrnd_color == 4 ? colors[8] : colors[4]);
    }
    else if (volume < 15)
    {
        reader.drawBMP("/speaker2_color.bmp", tft, 40, 2, true, colors[bckgrnd_color]);
        //tft.drawBitmap(121, 102, speaker2, 30, 23, bckgrnd_color == 4 ? colors[8] : colors[4]);
    }
    else
    {
        reader.drawBMP("/speaker3_color.bmp", tft, 40, 2, true, colors[bckgrnd_color]);
        //tft.drawBitmap(121, 102, speaker3, 30, 23, bckgrnd_color == 4 ? colors[8] : colors[4]);
    }
}

void drawMenu()
{
    tft.setFont(&Inter5pt7b);
    tft.setRotation(3);
    tft.fillScreen(colors[bckgrnd_color]);
    tft.setTextColor(colors[text_color]);
    tft.setCursor(5, 17);
    tft.print("Choose station: ");
    int offset = menu_index - menu_index % 9;
    uint8_t i = 0;
    while (i < 9 && (offset + i) < stations_number)
    {
        tft.setCursor(10, 6 + 11 * (i + 2));
        tft.println(current_station[i + offset]);
        i++;
    }
    tft.drawRect(8, 20 + (menu_index - offset) * 11, 120, 11 , colors[text_color]);
}

void drawMenuOption()
{
    uint8_t i = 0;
    int8_t temp_hour = 0;
    time_t temp_epoch = 0;
    switch (main_menu)
    {
        case 1:
            tft.setFont(&Inter5pt7b);
            tft.setRotation(3);
            tft.fillScreen(colors[bckgrnd_color]);
            tft.setTextColor(colors[text_color]);
            tft.setCursor(5, 17);
            tft.print("Choose text color: ");
            while (i < 11 && (i) < sizeof(colors))
            {
                tft.setCursor(10, 6 + 11 * (i + 2));
                tft.println(color_names[i]);
                i++;
            }
            tft.drawRect(8, 20 + menu_index * 11, 120, 11 , colors[text_color]);
            break;
        case 2:
            tft.setFont(&Inter5pt7b);
            tft.setRotation(3);
            tft.fillScreen(colors[bckgrnd_color]);
            tft.setTextColor(colors[text_color]);
            tft.setCursor(5, 17);
            tft.print("Choose background: ");
            while (i < 11 && (i) < sizeof(colors))
            {
                tft.setCursor(10, 6 + 11 * (i + 2));
                tft.println(color_names[i]);
                i++;
            }
            tft.drawRect(8, 20 + menu_index * 11, 120, 11 , colors[text_color]);
            break;
        case 3:
            tft.setFont(&Inter10pt7b);
            tft.setRotation(3);
            tft.fillScreen(colors[bckgrnd_color]);
            tft.setCursor(5, 17);
            tft.setTextColor(colors[text_color]);
            tft.print("Backlight: ");
            tft.setCursor(40, 75);
            tft.setTextSize(2);
            tft.setTextColor(colors[bckgrnd_color]);
            tft.fillRect(35, 40, 50, 43 , colors[text_color]);
            tft.print(backlight_pwm);
            tft.setTextSize(1);
            break;
        case 4:
            tft.setFont(&Inter10pt7b);
            tft.setRotation(3);
            tft.fillScreen(colors[bckgrnd_color]);
            tft.setCursor(5, 17);
            tft.setTextColor(colors[text_color]);
            tft.print("Time zone: ");
            tft.setCursor(40, 75);
            tft.setTextSize(2);
            tft.setTextColor(colors[bckgrnd_color]);
            if (time_offset < -9)
            {
                tft.fillRect(35, 40, 70, 43 , colors[text_color]);
            }
            else
            {
                tft.fillRect(35, 40, 50, 43 , colors[text_color]);
            }
            tft.print(time_offset);
            tft.setTextSize(1);
            tft.setTextColor(colors[text_color]);
            tft.setCursor(40, 110);
            temp_hour = rtc.getHour(1);
            temp_epoch = rtc.getEpoch();
            if (time_offset != prev_time_offset)
            {
                Serial.println(time_offset - prev_time_offset);
                Serial.println(temp_epoch);
                temp_epoch += (time_offset - prev_time_offset) * 3600LL;
                rtc.setTime(temp_epoch);
            }
            rtc.getHour(true) < 10 ? tft.print("0") : 0;
            tft.print(rtc.getHour(true));
            tft.print(":");
            rtc.getMinute() < 10 ? tft.print("0") : 0;
            tft.print(rtc.getMinute());
            prev_time_offset = time_offset;
            break;
        case 5:
            tft.setFont(&Inter5pt7b);
            tft.setRotation(3);
            tft.fillScreen(colors[bckgrnd_color]);
            tft.setTextColor(colors[text_color]);
            tft.setCursor(10, 17);
            tft.print("If you want to add more");
            tft.setCursor(10, 27);
            tft.print("radio stations, please ");
            tft.setCursor(10, 37);
            tft.print("open browser on your PC");
            tft.setCursor(10, 47);
            tft.print("or phone and type \n   ");
            tft.setCursor(10, 57);
            tft.print(WiFi.localIP());
            tft.setCursor(10, 67);
            tft.print("in URL bar. There you");
            tft.setCursor(10, 77);
            tft.print("can add more radio");
            tft.setCursor(10, 87);
            tft.print("stations.");
            break;
        default:
            menu = 0;
            main_menu = 0;
            break;
    }
}

void drawMainMenu()
{
    tft.setFont(&Inter5pt7b);
    tft.setRotation(3);
    tft.fillScreen(colors[bckgrnd_color]);
    tft.setTextColor(colors[text_color]);
    tft.setCursor(5, 16);
    tft.print("Settings: ");
    tft.setCursor(10, 28);
    tft.println("Text color");
    tft.setCursor(10, 39);
    tft.println("Background color");
    tft.setCursor(10, 50);
    tft.println("Backlight");
    tft.setCursor(10, 61);
    tft.println("Time zone");
    tft.setCursor(10, 72);
    tft.println("Add more stations");
    tft.setCursor(10, 83);
    tft.println("Back");
    tft.drawRect(8, 20 + menu_index * 11, 120, 11 , colors[text_color]);
}

void setStation()
{
    memset(station_name, 0 , sizeof(*station_name));
    memset(currently_playing, 0 , sizeof(*currently_playing));
    bool hostConnected = audio->connecttohost(stream_links[current_station_index]);
    if (!hostConnected)
    {
        strcpy(station_name, "Unknown\0");
        strcpy(currently_playing, "No title information.");
    }
}

void volumeSet()
{
    if (last_encode + debounce_rotary_encoder < millis())
    {
        if ((digitalRead(33) != digitalRead(32)))
        {
            if (main_menu == 3)
            {
                backlight_pwm--;
            }
            else if (main_menu == 4)
            {
                time_offset--;
            }
            else if (menu && main_menu != 5)
            {
                menu_index--;
            }
            else if (main_menu != 5)
            {
                volume--;
            }
        }

        if (digitalRead(33) == digitalRead(32))
        {
            if (main_menu == 3)
            {
                backlight_pwm++;
            }
            else if (main_menu == 4)
            {
                time_offset++;
            }
            else if (menu && main_menu != 5)
            {
                menu_index++;
            }
            else if (main_menu != 5)
            {
                volume++;
            }
        }
        last_encode = millis();
    }

    if (menu == 2 && main_menu == 0)
    {
        if (menu_index < 0)
            menu_index = 5;
        if (menu_index > 5)
            menu_index = 0;
    }

    else if (menu == 1)
    {
        if (menu_index < 0)
            menu_index = stations_number - 1;
        if (menu_index > stations_number - 1)
            menu_index = 0;
    }

    else if (menu == 2 && main_menu == 3)
    {
        if (backlight_pwm < 0)
            backlight_pwm = 0;
        if (backlight_pwm > 10)
            backlight_pwm = 10;
        ledcWrite(0, 250 - backlight_pwm * 25);
    }
    else if (menu == 2 && main_menu == 4)
    {
        if (time_offset < -12)
            time_offset = -12;
        if (time_offset > 12)
            time_offset = 12;
    }
    else if (menu == 2 && (main_menu == 1 || main_menu == 2))
    {
        if (menu_index < 0)
            menu_index = 8;
        if (menu_index > 8)
            menu_index = 0;
    }
    else
    {
        if (volume < 0)
            volume = 0;
        if (volume > 21)
            volume = 21;
    }
    flags_reg |= (1 << 2);
}

void buttonPress()
{
    if (last_button_release + debounce_button < millis())
    {
        if (digitalRead(ROTARY_PIN_BUTTON) == HIGH)
        {
            if (last_button_press + button_hold_timeout < millis())
            {
                //flags_reg |= (1 << 1);
                last_button_release = millis();
                flag_long_pressed = 0;
            }
            else
            {
                //flag_button_pressed = 1;
                flags_reg |= 1;
                last_button_release = millis();
            }
        }
        else
        {
            last_button_press = millis();
        }
    }
}

// optional
void audio_info(const char *info)
{
    Serial.print("info        ");
    Serial.println(info);
}

void audio_id3data(const char *info)
{   //id3 metadata
    Serial.print("id3data     ");
    Serial.println(info);
}

void audio_eof_mp3(const char *info)
{   //end of file
    Serial.print("eof_mp3     ");
    Serial.println(info);
}

void audio_showstation(const char *info)
{
    Serial.print("station     ");
    int i = 0, offset = 0;
    while (*(info +  i + offset) != '\0' && i < 21)
    {
        if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x86 || *(info +  i + offset + 1) == 0x8C))
        {
            offset++;
            *(station_name + i) = 'C';
        }
        else if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x87 || *(info +  i + offset + 1) == 0x8D))
        {
            offset++;
            *(station_name + i) = 'c';
        }
        else if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x90))
        {
            offset++;
            *(station_name + i) = 'D';
        }
        else if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x91))
        {
            offset++;
            *(station_name + i) = 'd';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xA0))
        {
            offset++;
            *(station_name + i) = 'S';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xA1))
        {
            offset++;
            *(station_name + i) = 's';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xBD))
        {
            offset++;
            *(station_name + i) = 'Z';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xBE))
        {
            offset++;
            *(station_name + i) = 'z';
        }
        else
        {
            *(station_name + i) = *(info +  i + offset);
        }
        i++;
    }
    *(station_name + i) = '\0';
    flags_reg |= (1 << 2);
    Serial.println(info);
}

void audio_showstreamtitle(const char *info)
{
    Serial.print("streamtitle ");
    int i = 0, offset = 0;
    while (*(info +  i + offset) != '\0' && i < 43)
    {
        if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x86 || *(info +  i + offset + 1) == 0x8C))
        {
            offset++;
            *(currently_playing +  i) = 'C';
        }
        else if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x87 || *(info +  i + offset + 1) == 0x8D))
        {
            offset++;
            *(currently_playing +  i) = 'c';
        }
        else if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x90))
        {
            offset++;
            *(currently_playing +  i) = 'D';
        }
        else if (*(info +  i + offset) == 0xC4 && (*(info +  i + offset + 1) == 0x91))
        {
            offset++;
            *(currently_playing +  i) = 'd';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xA0))
        {
            offset++;
            *(currently_playing +  i) = 'S';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xA1))
        {
            offset++;
            *(currently_playing +  i) = 's';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xBD))
        {
            offset++;
            *(currently_playing +  i) = 'Z';
        }
        else if (*(info +  i + offset) == 0xC5 && (*(info +  i + offset + 1) == 0xBE))
        {
            offset++;
            *(currently_playing +  i) = 'z';
        }
        else
        {
            *(currently_playing +  i) = *(info +  i + offset);
        }
        i++;
    }
    *(currently_playing + i) = '\0';
    flags_reg |= (1 << 2);
    Serial.println(info);
}

void audio_bitrate(const char *info)
{
    Serial.print("bitrate     ");
    Serial.println(info);
}

void audio_commercial(const char *info)
{   //duration in sec
    Serial.print("commercial  ");
    Serial.println(info);
}

void audio_icyurl(const char *info)
{   //homepage
    Serial.print("icyurl      ");
    Serial.println(info);
}

void audio_lasthost(const char *info)
{   //stream URL played
    Serial.print("lasthost    ");
    Serial.println(info);
}

// Initialize SPIFFS
void initSPIFFS() {
    if (!SPIFFS.begin(true)) {
        Serial.println("An error has occurred while mounting SPIFFS");
    }
    Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path) {
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
        Serial.println("- failed to open file for reading");
        return String();
    }

    String fileContent;
    while (file.available()) {
        fileContent = file.readStringUntil('\n');
        break;
    }
    return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message)) {
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }
}
