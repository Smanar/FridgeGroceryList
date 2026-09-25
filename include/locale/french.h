// ============================================================================
// locale/french.h — vocabulaire français utilisé par intent_parser.cpp
//
// Centralise ici tous les mots-clés en dur (déclencheurs d'intention,
// nombres écrits en lettres, mots vides). Pour supporter une autre langue,
// il suffit de dupliquer ce fichier (ex: locale/english.h) en gardant les
// mêmes noms de fonctions, puis de changer l'include dans intent_parser.cpp.
// ============================================================================

#pragma once

#include <string>
#include <utility>
#include <vector>

#define LOCALE_LANGUAGE "Francais"

namespace locale_str {

// Chiffres écrits en toutes lettres (jusqu'à dix — étends si besoin)
inline const std::vector<std::pair<std::string, int>>& numberWords()
{
    static const std::vector<std::pair<std::string, int>> WORDS = {
        {"un", 1}, {"une", 1}, {"deux", 2}, {"trois", 3}, {"quatre", 4},
        {"cinq", 5}, {"six", 6}, {"sept", 7}, {"huit", 8}, {"neuf", 9}, {"dix", 10}
    };
    return WORDS;
}

// Déclencheurs pour Intent::ADD_ITEM
inline const std::vector<std::string>& addItemTriggers()
{
    static const std::vector<std::string> WORDS = {
        "ajoute", "ajouter", "rajoute", "mets", "mettre", "ajout"
    };
    return WORDS;
}

// Déclencheurs pour Intent::REMOVE_ITEM
inline const std::vector<std::string>& removeItemTriggers()
{
    static const std::vector<std::string> WORDS = {
        "enleve", "enlever", "retire", "retirer", "supprime", "supprimer"
    };
    return WORDS;
}

// Déclencheurs pour Intent::CLEAR_LIST
inline const std::vector<std::string>& clearListTriggers()
{
    static const std::vector<std::string> WORDS = {
        "vide", "efface", "reinitialise", "reset"
    };
    return WORDS;
}

// Déclencheurs pour Intent::SET_QUANTITY
inline const std::vector<std::string>& setQuantityTriggers()
{
    static const std::vector<std::string> WORDS = {
        "change", "modifie", "quantité"
    };
    return WORDS;
}

// Déclencheurs pour Intent::UPDATE_LIST
inline const std::vector<std::string>& updateTriggers()
{
    static const std::vector<std::string> WORDS = {
        "update", "jour", "met"
    };
    return WORDS;
}

// Mots vides à ignorer lors de l'extraction du nom d'article
// (utilisé par extractItemName, pas par getRules()/numberWords(),
//  mais regroupé ici car c'est aussi du vocabulaire français en dur)
inline const std::vector<std::string>& stopwords()
{
    static const std::vector<std::string> WORDS = {
        "la", "le", "les", "de", "des", "du", "à", "au", "aux", "liste",
        "et", "s'il", "vous", "plaît", "svp", "stp", "a", "l"
    };
    return WORDS;
}

} // namespace locale_str
