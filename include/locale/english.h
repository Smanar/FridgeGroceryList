// ============================================================================
// locale/english.h — English vocabulary used by intent_parser.cpp
//
// Centralize all hardcoded keywords here (intent triggers,
// numbers written as words, stop words). To support another language,
// simply duplicate this file (e.g., locale/english.h) while keeping the
// same function names, then change the include directive in intent_parser.cpp.
// ============================================================================

#pragma once

#include <string>
#include <utility>
#include <vector>

#define LOCALE_LANGUAGE "English"

namespace locale_str {

// Numbers written out in full (up to ten — extend if necessary)
inline const std::vector<std::pair<std::string, int>>& numberWords()
{
    static const std::vector<std::pair<std::string, int>> WORDS = {
        {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4},
        {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}
    };
    return WORDS;
}

// Triggers for Intent::ADD_ITEM
inline const std::vector<std::string>& addItemTriggers()
{
    static const std::vector<std::string> WORDS = {
        "adds", "add", "put", "put", "addition"
    };
    return WORDS;
}

// Triggers for Intent::REMOVE_ITEM
inline const std::vector<std::string>& removeItemTriggers()
{
    static const std::vector<std::string> WORDS = {
        "remove", "take off", "withdraw", "delete"
    };
    return WORDS;
}

// Triggers for Intent::CLEAR_LIST
inline const std::vector<std::string>& clearListTriggers()
{
    static const std::vector<std::string> WORDS = {
        "empty", "clear", "reinitialize", "reset"
    };
    return WORDS;
}

// Triggers for Intent::SET_QUANTITY
inline const std::vector<std::string>& setQuantityTriggers()
{
    static const std::vector<std::string> WORDS = {
        "change", "modify", "quantity"
    };
    return WORDS;
}

// Triggers for Intent::UPDATE_LIST
inline const std::vector<std::string>& updateTriggers()
{
    static const std::vector<std::string> WORDS = {
        "update"
    };
    return WORDS;
}

// Stop words to ignore when extracting the item name
// (used by extractItemName, not by getRules()/numberWords(),
//  but grouped here because it is also hardcoded French vocabulary)
inline const std::vector<std::string>& stopwords()
{
    static const std::vector<std::string> WORDS = {
        "the", "and", "please", "to", "list", "some"
    };
    return WORDS;
}

} // namespace locale_str
