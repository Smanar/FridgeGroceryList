// ============================================================================
// intent_parser.cpp — intent extraction from transcribed text
// (output from a speech recognition engine, e.g., Whisper / Vosk / etc.)
//
// Deliberately simple approach: keyword and pattern matching rather than
// NLP/ML. Advantages in this context:
//   - no external dependencies; fits within a few KB of flash memory
//   - deterministic and debuggable (no "black box" model)
//   - sufficient as long as the command set remains limited (dozens,
//     not thousands)
//
// If the command vocabulary grows significantly, it would be better to switch
// to a proper classifier (embeddings + lightweight model) rather than stacking
// rules—though that is not necessary to get started.
//
// The entire French vocabulary (keywords, numbers written as words,
// stop words) now resides in locale/french.h. For another language, create
// an equivalent file (e.g., locale/italian.h, using the same function names)
// and update the include directive in algorithm.h.
// ============================================================================

#include <string>
#include <vector>
#include <algorithm>
#include <optional>
#include <cctype>
#include <sstream>
#include <iostream>

#include "algorithm.h"

// ----------------------------------------------------------------------------
// Utilitaires texte
// ----------------------------------------------------------------------------

static std::string toLower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}


static std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> tokens;
    //Lower string
    std::istringstream iss(toLower(text));

    std::string word;
    while (iss >> word)
    {
        // Remove the residual punctuation that speech recognition sometimes leaves behind.
        word.erase(std::remove_if(word.begin(), word.end(), [](char c) { return std::ispunct((unsigned char)c) && c != '\''; }), word.end());
        if (!word.empty()) tokens.push_back(word);
    }
    return tokens;
}

static bool isDigitToken(const std::string& tok)
{
    return !tok.empty() && std::all_of(tok.begin(), tok.end(), ::isdigit);
}

static bool isNumberToken(const std::string& tok) {
    if (isDigitToken(tok)) return true;
    for (const auto& [word, value] : locale_str::numberWords())
        if (tok == word) return true;
    return false;
}

// Extracts the first number found in the tokens (digits or simple words
// "un"/"deux"/... up to "dix" — extend the table in locale/french.h if needed).
static int extractQuantity(const std::vector<std::string>& tokens)
{
    for (const auto& tok : tokens) {
        if (isDigitToken(tok)) return std::stoi(tok);
        for (const auto& [word, value] : locale_str::numberWords())
            if (tok == word) return value;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Definition of recognized intentions
// ----------------------------------------------------------------------------

struct IntentRule {
    Intent intent;
    std::vector<std::string> triggerWords;  // At least one must match.
    bool needsItemName;                     // Should an article name be extracted after the trigger?
};

static const std::vector<IntentRule>& getRules() {
    static const std::vector<IntentRule> rules = {
        { Intent::ADD_ITEM,     locale_str::addItemTriggers(),     true  },
        { Intent::REMOVE_ITEM,  locale_str::removeItemTriggers(),  true  },
        { Intent::CLEAR_LIST,   locale_str::clearListTriggers(),   false },
        { Intent::SET_QUANTITY, locale_str::setQuantityTriggers(), true  },
        { Intent::UPDATE_LIST,  locale_str::updateTriggers(),     true  },
    };
    return rules;
}

static std::string extractItemName(const std::vector<std::string>& tokens, size_t triggerIndex, const std::vector<std::string>& ruleTriggerWords)
{
    const std::vector<std::string>& STOPWORDS = locale_str::stopwords();

    std::string result;
    std::string tmp = "";

    for (size_t i = triggerIndex + 1; i < tokens.size(); ++i)
    {
        const std::string& tok = tokens[i];
        if (std::find(STOPWORDS.begin(), STOPWORDS.end(), tok) != STOPWORDS.end())
        {
            if (!result.empty())
            {
                tmp+= tok;
                tmp += " ";
            }
            continue;
        }
        if (isNumberToken(tok))
            continue;
        if (std::find(ruleTriggerWords.begin(), ruleTriggerWords.end(), tok) != ruleTriggerWords.end())
            continue;
        if (!result.empty()) result += " ";
        result += tmp;
        tmp = "";
        result += tok;
    }
    return result;
}

// ----------------------------------------------------------------------------
// Entry point: raw text → structured command
// ----------------------------------------------------------------------------

ParsedCommand parseIntent(const std::string& transcript)
{
    ParsedCommand cmd;
    std::vector<std::string> tokens = tokenize(transcript);
    if (tokens.empty()) return cmd;  // UNKNOWN

    for (const auto& rule : getRules())
    {
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            auto it = std::find(rule.triggerWords.begin(), rule.triggerWords.end(), tokens[i]);
            if (it == rule.triggerWords.end()) continue;

            // Trigger found
            cmd.intent = rule.intent;

            if (rule.needsItemName) {
                cmd.itemName = extractItemName(tokens, i, rule.triggerWords);
            }

            cmd.quantity = extractQuantity(tokens);

            return cmd;
        }
    }

    return cmd;  // No trigger found → UNKNOWN
}

// ----------------------------------------------------------------------------
// Démo
// ----------------------------------------------------------------------------

static const char* intentName(Intent i) {
    switch (i) {
        case Intent::ADD_ITEM:     return "ADD_ITEM";
        case Intent::REMOVE_ITEM:  return "REMOVE_ITEM";
        case Intent::CLEAR_LIST:   return "CLEAR_LIST";
        case Intent::SET_QUANTITY: return "SET_QUANTITY";
        case Intent::UPDATE_LIST:  return "UPDATE_LIST";
        default:                   return "UNKNOWN";
    }
}

int ProcessString(std::string s) {

        ParsedCommand cmd = parseIntent(s);

        std::cout << "\"" << s << "\"\n"
                  << "  intent     = " << intentName(cmd.intent) << "\n"
                  << "  item       = " << (cmd.itemName.empty() ? "(aucun)" : cmd.itemName) << "\n"
                  << "  quantity   = " << (cmd.quantity ? std::to_string(cmd.quantity) : "(aucune)") << "\n\n";

    return 0;
}
