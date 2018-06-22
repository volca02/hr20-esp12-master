#include "wifi.h"

WiFiManager wifiManager;

void setupWifi()
{

    //uncomment for testing
    //wifiManager.resetSettings();
    if (!wifiManager.autoConnect("OpenHR20 is not dead"))
    {
        Serial.println("failed to connect, we should reset and see if it connects");
        delay(3000);
        ESP.reset();
        delay(5000);
    }

    Serial.println("Connected :)");
    Serial.println(WiFi.localIP());
}
