#ifndef ALGORITHM_H
#define ALGORITHM_H

enum class Intent {
    ADD_ITEM,
    REMOVE_ITEM,
    CLEAR_LIST,
    READ_LIST,
    SET_QUANTITY,
    UPDATE_LIST,
    UNKNOWN
};

struct ParsedCommand {
    Intent      intent = Intent::UNKNOWN;
    std::string itemName;              // ex: "milk", "apples"
    int quantity = 0;                  // ex: 2
};

int ProcessString(std::string s);
ParsedCommand parseIntent(const std::string& transcript);

// Use the locale file for string
//#include "locale/french.h"
#include "locale/english.h"


#endif