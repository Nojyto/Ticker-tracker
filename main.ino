#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "SSD1306.h"

#define ssid       "<wifiName>"
#define pass       "<wifiPass>"
#define hostName   "esptt"
//exmpl url => https://finnhub.io/api/v1/quote?symbol=TSLA&token=c5kv562ad3ibikglgeq0
#define host       "finnhub.io"
#define apiUrl     "/api/v1/quote?symbol="
#define apiKey     "&token=c5kv562ad3ibikglgeq0"
//changes every few months
#define fingPr     "95 E6 C6 0D D3 9E C0 B2 40 7C 47 66 B5 89 8F 51 72 81 27 A6"
#define fadeTime   1000
#define upInterval 300000

/*
  --Pinout--
  Addr => 0x3C (oled specific)
  D2   => SCK  (serial Clock)
  D1   => SDA  (serial Data)
*/
SSD1306            dp(0x3C, D2, D1);
ESP8266WebServer   ws(80);
WiFiClientSecure   client;

const int cW  = dp.getWidth()  / 2;
const int cH  = dp.getHeight() / 2;
String ticker = "";
unsigned long prvTime = 0;

void setup() {
    dp.init();
    dp.flipScreenVertically();
    dp.setFont(ArialMT_Plain_16);
    dp.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
    updateDisplay("Connecting...");

    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostName);
    WiFi.begin(ssid, pass);
    while(WiFi.status() != WL_CONNECTED) delay(500);
    client.setFingerprint(fingPr);

    ws.on("/", handleRoot);
    ws.on("/change", handleUpdateCall);
    ws.onNotFound([](){
        ws.sendHeader("Location", String("/"), true);
        ws.send(302, "text/plain", "Invalid url.");
    });
    ws.begin();

    EEPROM.begin(1024);
    for(int i = 1; i < int(EEPROM.read(0)); i++)
        ticker += char(EEPROM.read(i));
    EEPROM.end();

    updateDisplay("Connected.\n" + String(WiFi.RSSI()) + " dBm");
    updateDisplay(String(ssid) + "\n"  + WiFi.hostname() + "." + "\n" + WiFi.localIP().toString());
    makeHTTPRequest();
}

void loop() {
    ws.handleClient();
    delay(500);

    if(millis() - prvTime >= upInterval) {
        prvTime = millis();
        makeHTTPRequest();
    }
}

void updateDisplay(const String msg){
    dp.clear();
    dp.drawString(cW, cH, msg);
    dp.display();
    delay(fadeTime);
}

void handleRoot(){
    ws.send(200, "text/html",
        R""""(
            <!DOCTYPE html>
            <html lang='en'>
            <head>
                <meta charset='UTF-8'/>
                <meta http-equiv='X-UA-Compatible' content='IE=edge'/>
                <meta name='viewport' content='width=device-width, initial-scale=1.0'/>
                <title>Ticker Tracker</title>
                <style>
                    @import url('https://fonts.googleapis.com/css2?family=Titillium+Web:ital,wght@0,200;0,300;0,400;0,600;0,700;0,900;1,200;1,300;1,400;1,600;1,700&display=swap');

                    * {
                        margin: 0;
                        padding: 0;
                        box-sizing: border-box;
                        font-family: 'Titillium Web', sans-serif;
                        
                        -webkit-touch-callout: none;
                        -webkit-user-select: none;
                        -khtml-user-select: none;
                        -moz-user-select: none;
                        -ms-user-select: none;
                        user-select: none;
                    }

                    :root {
                        --fullwhite: #FFFFFF;
                        --white:     #F1F1F1;
                        --darkgrey:  #292929;
                        --black:     #1B1B1B;
                        --fullblack: #000000;
                    }

                    ::-webkit-scrollbar {
                        width: 7px;
                        height: 7px;
                    }

                    ::-webkit-scrollbar-track {
                        background-color: var(--darkgrey);
                        box-shadow: inset 0 0 2px var(--fullblack);
                        border-radius: 6px;
                    }

                    ::-webkit-scrollbar-thumb {
                        background-color: var(--white);
                        border-radius: 6px;
                    }

                    html {
                        background-color: var(--darkgrey);
                        scroll-behavior: smooth;
                    }

                    body{
                        display: flex;
                        justify-content: center;
                        align-items: center;
                        min-height: 100%;
                    }

                    main {
                        padding: 64px;
                        margin: 64px;
                        color: var(--white);
                        background-color: var(--black);
                        box-shadow: 0 0 15px var(--fullblack);
                    }

                    label[for=ticker], input[name=ticker], input[value=Submit]{
                        width: 140px;
                        margin: 5px;
                    }
                </style>
            </head>
            <body>
                <main>
                    <form action='/change'>
                        <label for='ticker'>Input a new ticker:</label>
                        <input type='text' id='ticker' name='ticker' placeholder='Ticker'>
                        <input type="submit" value="Submit">
                    </form>
                </main>
            </body>
            </html>
        )""""
    );
}

//http://<Wemos(Ip/DNS)>/change?ticker=<ticker>
void handleUpdateCall() {
    String tmp = ws.arg("ticker");

    if(tmp.length() > 5){
        ws.send(404, "text/plain", "Invalid input. Max ticker length is 5.");
        return;
    }
    for(int i = 0; i < tmp.length(); i++){
        if(!isupper(tmp[i])){
            ws.send(404, "text/plain", "Invalid input. Upper case letters only.");
            return;
        }
    }
    
    ticker = tmp;

    EEPROM.begin(1024);
    EEPROM.write(0, ticker.length() + 1);
    for(int i = 1; i < ticker.length() + 1; i++)
        EEPROM.write(i, ticker[i - 1]);
    EEPROM.commit();

    ws.send(200, "text/plain", "Ticker updated to " + ticker + ".");
    updateDisplay("Ticker updated.");
    makeHTTPRequest();
    prvTime = millis();
}

void makeHTTPRequest(){
    if(!client.connect(host, 443)){
        updateDisplay("Connection\nfailed.");
        return;
    }
    
    yield();

    client.println("GET " + String(apiUrl) + String(ticker) + String(apiKey) + " HTTP/1.0");
    client.println("Host: " + String(host));
    client.println("Cache-Control: no-cache");

    if(client.println() == 0){
        updateDisplay("Failed to send\n request.");
        return;
    }

    if(!client.find("\r\n\r\n")){
        updateDisplay("Invalid\n response.");
        return;
    }

    DynamicJsonDocument doc(512);
    deserializeJson(doc, client.readString());
    JsonObject obj = doc.as<JsonObject>();

    String stockValue = obj["c"] + "$";
    String stockChange = obj["dp"];

    stockChange.remove(stockChange.indexOf('.') + 3, stockChange.length() - 1);
    stockChange += "%";
    if(stockChange[0] != '-')
        stockChange = "+" + stockChange;

    updateDisplay(ticker + "\n" + stockValue + "\n" + stockChange);
}
