# FridgeGroceryList
WIP ESP32 project using a small device with e-paper to make a Shopping list with Speak to text synchronized with a smartphone (using SimpleNote application).   


## Configuration
Edit the file include/personal_settings.h.txt and rename it personal_settings.h.   
Edit the file include/algorithm.h with your language.   


## Working mode
- After the first start, you will have the splash screen, just press shortly the BOOT button.   
- To add an item, long press on BOOT (3s), you will have 2 beep, say "add the butter" or "remove milk" then press again the button (or wait the 8s time limit), you will have again 2 beep.   
- The screen flash and update.   
- Press POWER to scroll the page (if you have more than 6 item).   
- The list is synchronised on your phone using the SimpleNote application.   
- After 1mn the device will shutdown.

To wake up the device just press shortly POWER, you will see the green led, mean the device is starting, when the led turn off, the device is ready for 1mn again.

## ect
