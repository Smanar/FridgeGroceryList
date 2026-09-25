#include <Arduino.h>

#include "hardware.h"
#include "input.h"



// ============================================================================
// Button helpers — every place in this file that needs to read BOOT_BUTTON
// or PWR_BUTTON goes through one of these three functions instead of
// hand-rolled digitalRead()/delay() loops.
// ============================================================================

// Blocks until `pin` reads HIGH (button released).
void waitForRelease(int pin)
{
    while (digitalRead(pin) == LOW) delay(50);
}


// Blocks while `pin` is held LOW. Returns true as soon as it's been held for
// at least thresholdMs (does NOT wait for release in that case — caller
// decides what happens next). Returns false if released earlier.
bool heldPastThreshold(int pin, unsigned long thresholdMs)
{
    unsigned long start = millis();
    while (digitalRead(pin) == LOW)
    {
        if (millis() - start >= thresholdMs) return true;
        delay(50);
    }
    return false;
}


// ============================================================================
// Poll unique pour les deux boutons. Bloque tant qu'un bouton est tenu
// (comportement identique à l'existant), mais centralise toute la logique
// de debounce / clic / hold / combo en un seul endroit.
// ============================================================================

ButtonEvent pollButtons(unsigned long holdMs, unsigned long comboMs, unsigned long debounceMs)
{
    // Lecture directe des états physiques (LOW = pressé)
    bool bootDown = (digitalRead(BOOT_BUTTON) == LOW);
    bool pwrDown  = (digitalRead(PWR_BUTTON)  == LOW);

    // Variables statiques pour mémoriser l'état d'un appel à l'autre
    static bool s_isPressing = false;
    static unsigned long s_pressStartTime = 0;
    static bool s_bootWasPressed = false;
    static bool s_pwrWasPressed = false;
    static bool s_eventTriggered = false;

    unsigned long now = millis();

    // --- Cas 1 : Aucun bouton n'est pressé ---
    if (!bootDown && !pwrDown) 
    {
        if (!s_isPressing) return EVT_NONE; // Rien ne se passait déjà

        // Anti-rebond au relâchement
        delay(debounceMs); 
        if ((digitalRead(BOOT_BUTTON) == LOW) || (digitalRead(PWR_BUTTON) == LOW)) {
            return EVT_NONE; // Faux relâchement (bruit)
        }

        // Si on arrive ici, l'utilisateur vient de TOUT relâcher
        ButtonEvent finalEvent = EVT_NONE;

        // On ne génère un clic court QUE si aucun appui long n'a déjà été déclenché
        if (!s_eventTriggered) 
        {
            if (s_bootWasPressed && !s_pwrWasPressed)      finalEvent = EVT_BOOT_CLICK;
            else if (s_pwrWasPressed && !s_bootWasPressed) finalEvent = EVT_PWR_CLICK;
        }

        // Réinitialisation complète de la machine d'état pour le prochain appui
        s_isPressing = false;
        s_eventTriggered = false;
        s_bootWasPressed = false;
        s_pwrWasPressed = false;

        return finalEvent;
    }

    // --- Cas 2 : Un ou plusieurs boutons viennent d'être pressés ---
    if (!s_isPressing) 
    {
        // Anti-rebond à l'appui
        delay(debounceMs);
        if (digitalRead(BOOT_BUTTON) != LOW && digitalRead(PWR_BUTTON) != LOW) {
            return EVT_NONE; // Bruit parasite
        }

        // Début de l'enregistrement de l'appui
        s_isPressing = true;
        s_pressStartTime = now;
        s_eventTriggered = false;
        s_bootWasPressed = (digitalRead(BOOT_BUTTON) == LOW);
        s_pwrWasPressed  = (digitalRead(PWR_BUTTON) == LOW);
        return EVT_NONE;
    }

    // Accumulation des boutons pressés pendant la durée de l'appui 
    // (ex: si on presse BOOT puis PWR 20ms après, on capture le combo)
    if (bootDown) s_bootWasPressed = true;
    if (pwrDown)  s_pwrWasPressed  = true;

    // Si un événement long a déjà été envoyé pour cet appui, on attend le relâchement complet
    if (s_eventTriggered) return EVT_NONE;

    // Calcul de la durée de l'appui actuel
    unsigned long heldDuration = now - s_pressStartTime;

    // --- Cas 3 : Évaluation des appuis longs / combos ---
    if (bootDown && pwrDown && heldDuration >= comboMs) 
    {
        s_eventTriggered = true; // Empêche de renvoyer l'event en boucle
        return EVT_COMBO_HELD;
    }
    
    if (bootDown && !pwrDown && heldDuration >= holdMs) 
    {
        s_eventTriggered = true;
        return EVT_BOOT_HELD;
    }
    
    if (pwrDown && !bootDown && heldDuration >= holdMs) 
    {
        s_eventTriggered = true;
        return EVT_PWR_HELD;
    }

    return EVT_NONE; // L'appui est en cours mais les seuils ne sont pas atteints
}


// ============================================================================
// Wait for any button press (used by info sub-screens)
// ============================================================================

void waitForAnyButton(void)
{
    // Garde-fou : si un bouton est encore tenu depuis l'action précédente,
    // on attend qu'il soit relâché avant de commencer à écouter un nouvel appui.
    waitForRelease(BOOT_BUTTON);
    waitForRelease(PWR_BUTTON);

    while (true)
    {
        ButtonEvent evt = pollButtons(/*holdMs=*/NO_HOLD_THRESHOLD, /*comboMs=*/NO_HOLD_THRESHOLD);
        if (evt != EVT_NONE) return;  // n'importe quel clic (BOOT ou PWR) suffit
        delay(50);
    }
}