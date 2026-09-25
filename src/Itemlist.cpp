#include <vector>
#include <string>
#include <Arduino.h>
#include <algorithm>

#include "itemlist.h"
#include "simplenote.h"

std::vector<std::string> myShoppingList;

// ---------------------------------------------------------------------------
// Remove accented characters from UTF-8 strings
// ---------------------------------------------------------------------------
std::string removeaccentued(const std::string& s)
{
    std::string out = s;

    static const std::string avecAccent[] = {
        "é", "è", "ê", "ë", "É", 
        "à", "â", "ä", "À", 
        "î", "ï", "Î", 
        "ô", "ö", "Ô", 
        "ù", "û", "ü", "Ù", 
        "ç", "Ç"
    };

    static const std::string sansAccent[] = {
        "e", "e", "e", "e", "E", 
        "a", "a", "a", "A", 
        "i", "i", "I", 
        "o", "o", "O", 
        "u", "u", "u", "U", 
        "c", "C"
    };

    const size_t taille = sizeof(avecAccent) / sizeof(avecAccent[0]);

    for (size_t i = 0; i < taille; ++i) {
        size_t pos = out.find(avecAccent[i]);
        while (pos != std::string::npos) {
            out.replace(pos, avecAccent[i].length(), sansAccent[i]);
            // Advance carefully by the size of the injected clean character string
            pos = out.find(avecAccent[i], pos + sansAccent[i].length());
        }
    }

    return out;
}

void initItemList(void)
{
    // Reserved for future storage/EEPROM initializations
}

// ---------------------------------------------------------------------------
// Add item to shopping list (prevents exact duplicates)
// ---------------------------------------------------------------------------
bool addItemList(const std::string& s)
{
    if (std::find(myShoppingList.begin(), myShoppingList.end(), s) == myShoppingList.end())
    {
        Serial.printf("[List] Add Item to list: %s\n", s.c_str());
        myShoppingList.push_back(s);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Remove item safely using exact word but without exact match
// ---------------------------------------------------------------------------
bool removeItemList(const std::string& lookFor)
{
    Serial.printf("[List] Remove Item request: %s\n", lookFor.c_str());

    size_t tailleAvant = myShoppingList.size();

    myShoppingList.erase(
        std::remove_if(myShoppingList.begin(), myShoppingList.end(), [&lookFor](const std::string& item) {
            size_t pos = item.find(lookFor);
            
            // Loop in case the word appears multiple times in an incorrect form
            while (pos != std::string::npos) {
                bool valideDebut = false;
                bool valideFin = false;

                // 1. Check the start of the word
                // It is valid if it is the start of the string OR if the preceding character is a space or punctuation
                if (pos == 0 || isspace(item[pos - 1]) || ispunct(item[pos - 1])) {
                    valideDebut = true;
                }

                // 2. Check the end of the word
                size_t finPos = pos + lookFor.length();
                // It is valid if it is the end of the string OR if the next character is a space or punctuation.
                if (finPos == item.length() || isspace(item[finPos]) || ispunct(item[finPos])) {
                    valideFin = true;
                }

                if (valideDebut && valideFin) {
                    return true; 
                }

                pos = item.find(lookFor, pos + 1);
            }
            
            return false;
        }), 
        myShoppingList.end()
    );

    return myShoppingList.size() < tailleAvant;
}


// ---------------------------------------------------------------------------
// Fetch item index formatted cleanly for e-Paper rendering fonts
// ---------------------------------------------------------------------------
const char *GetItemsFromList(short i)
{
    if (i < 0 || (size_t)i >= myShoppingList.size())
    {
        return "";
    }

    // This static allocation approach means we can only evaluate 
    // one index string per serial print/display pipeline sequence cleanly.
    static std::string bufferTransfert;
    bufferTransfert = removeaccentued(myShoppingList[i]);
    return bufferTransfert.c_str();
}

int GetTotalItem(void)
{
    return myShoppingList.size();
}

void ClearList(void)
{
    myShoppingList.clear();
}

void UpdateListToSN(void)
{
    // Send update to the SimpleNote engine
    simpleNoteUpdate(myShoppingList);
}
