# This is Smart Distribution Board based on ESP8266

Watch how it works https://youtu.be/YGajnfcQebY

Read this if you have any problems: https://github.com/electrical-pro/SmartBoard/issues/1

[![IMAGE ALT TEXT](http://img.youtube.com/vi/YGajnfcQebY/0.jpg)](http://www.youtube.com/watch?v=YGajnfcQebY "Video Title")

This is the final curcuit:
<img src="circuit.jpg">

# This is components you need for the project: 

PZEM-004T: [https://s.click.aliexpress.com/e/_c3LJ53kj](https://s.click.aliexpress.com/e/_c3LJ53kj)

ESP8266: [https://s.click.aliexpress.com/e/_De5gu7f](https://s.click.aliexpress.com/e/_De5gu7f)

Resistors: [https://s.click.aliexpress.com/e/_9AslPB](https://s.click.aliexpress.com/e/_9AslPB)

Electrolytic Capacitor: [https://s.click.aliexpress.com/e/_A2atvx](https://s.click.aliexpress.com/e/_A2atvx)

PCB 6X8: [https://s.click.aliexpress.com/e/_c3icY5Ar](https://s.click.aliexpress.com/e/_c3icY5Ar)

Pin Header: [https://s.click.aliexpress.com/e/_c2zNwZSn](https://s.click.aliexpress.com/e/_c2zNwZSn)

5V relay module: [https://s.click.aliexpress.com/e/_AAXY9i](https://s.click.aliexpress.com/e/_AAXY9i)

1602 I2C Display: [https://s.click.aliexpress.com/e/_c2zNQH7p](https://s.click.aliexpress.com/e/_c2zNQH7p)


# UPDATE 
23/11/2025 Core 3.1.2, a lot of changes.

P.S. I modified the LiquidCrystal_I2C library, I removed Wire.begin(); we call it from setup instead, with our pins Wire.begin(4, 0); Use the one provided in use_these_libs.zip

# Uploading files from data folder
There is a file manager at 192.168.x.x:8089/littlefs

Content from data folder should be uploaded to littlefs (format & upload), should be like this:
<img width="958" height="726" alt="image" src="https://github.com/user-attachments/assets/2f9ab438-c72c-401d-b6c2-697d46007ecd" />
Ignore that it says ESP32, we use ESP8266 :)

After uploading, the main page is at 192.168.x.x:8089 (port is 8089)

# Serial
Note that I use Serial for PZEM004Tv30 module
```cpp
PZEM004Tv30 pzem(&Serial);
```
Other information goes to Serial1 not Serial (so you will not see things in serial monitor)
 ```cpp 
Serial1.begin(115200);
 ``` 
