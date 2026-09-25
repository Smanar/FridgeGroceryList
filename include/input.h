#ifndef INPUT_H
#define INPUT_H


// ============================================================================
// Button management
// ============================================================================


enum ButtonEvent
{
    EVT_NONE,        // rien de pertinent
    EVT_BOOT_CLICK,  // BOOT cliqué (court)
    EVT_BOOT_HELD,   // BOOT tenu ≥ holdMs
    EVT_PWR_CLICK,   // PWR cliqué (court)
    EVT_PWR_HELD,    // PWR tenu ≥ holdMs
    EVT_COMBO_HELD   // BOOT + PWR tenus ensemble ≥ comboMs
};

// Used with pollButtons() wherever we only care about short clicks and want
// hold/combo detection effectively disabled (menus, sub-screens).
static const unsigned long NO_HOLD_THRESHOLD = 3600000UL; // 1 hour — never reached in practice

ButtonEvent pollButtons(unsigned long holdMs = 3000, unsigned long comboMs = 2000, unsigned long debounceMs = 200);
void waitForRelease(int pin);
bool heldPastThreshold(int pin, unsigned long thresholdMs);
void waitForAnyButton(void);


#endif