# FridgeGroceryList
WIP ESP32 project using a small device with e-paper to make a Shopping list with Speak to text synchronized with a smartphone (using SimpleNote application).   


## Configuration
You need:
- The device, it's a ESP32-S3-ePaper-1.54 from waveshare
- A SimpleNote account (free)
- A wit.ai acount (free) https://wit.ai/apps generate a Server Access Token.

Edit the file include/personal_settings.h.txt and rename it personal_settings.h.   
Edit the file include/algorithm.h with your language.   

Remark:
I use pioarduino instead of espressif32 because some libraries are outdated; you can switch them in platformio.ini if you wish, because installing pioarduino takes a long time.

## Working mode
- After the first start, you will have the splash screen, just press shortly the BOOT button.   
- To add an item, long press on BOOT (3s), you will have 2 beep, say "add the butter" or "remove milk" then press again the button (or wait the 8s time limit), you will have again 2 beep. There is 2 protections here, something to detect silence and a 8s timer.      
- The screen flash and update.   
- Press POWER to scroll the page (if you have more than 6 item).   
- The list is synchronised on your phone using the SimpleNote application.   
- After 1mn the device will shutdown.

To wake up the device just press shortly POWER, you will see the green led, mean the device is starting, when the led turn off, the device is ready for 1mn again.

## TODO
- It seem Wit.ai have some protection, I m using full streaming and their server can cut the connexion if there too much not reconised work or a too big silence   
`12:22:42.887 > [ 51057][E][ssl_client.cpp:41] _handle_error(): [send_ssl_data():460]: (-80) UNKNOWN ERROR CODE (0050)
12:22:42.896 > [ 51066][E][NetworkClientSecure.cpp:248] write(): Closing connection on failed write`   
  You can enable an option with `FALLBACK_ENABLED` to record the sound in memory and send it after.
  
