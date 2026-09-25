//
// C++ convertion from the python code https://github.com/simplenote-vim/simplenote.py
// Take care I m using the same API key for tests
//

// Web page https://app.simplenote.com/


#include <WiFi.h>
#include <ArduinoJson.h>

#include "simplenote.h"
#include "itemlist.h"
#include "storage.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

const char *Simplenote::APP_ID   = "chalk-bump-f49";  // Simplenote app id on Simperium
const char *Simplenote::API_KEY  = "c8c2b86337154cdabc989b23e30c6bf4"; // Aplication token, from the python project, just for test, TODO get a new API key.
const char *Simplenote::AUTH_HOST = "auth.simperium.com";
const char *Simplenote::DATA_HOST = "api.simperium.com";

String sMainTitle = "Shopping List"; // Main title

JsonDocument gNote;                    
unsigned long gNoteTimestamp = 0;      
bool gNoteValid = false;               
const unsigned long NOTE_CACHE_MAX_AGE = 24UL * 60UL * 60UL * 1000UL; 

Simplenote sn;

Simplenote::Simplenote(void) {}

String Simplenote::authUrl() const {
    return String("https://") + AUTH_HOST + "/1/" + APP_ID + "/authorize/";
}

String Simplenote::dataUrl() const {
    return String("https://") + DATA_HOST + "/1/" + APP_ID + "/note";
}

void Simplenote::Init(const String &username, const String &password)
{
    _username = username;
    _password = password;
}

String Simplenote::httpRequest(const String &url, const String &method, const String &body, bool useToken, bool useApiKey)
{
    WiFiClientSecure client;
    client.setInsecure(); 
    client.setHandshakeTimeout(30);
    client.setTimeout(15000);

    HTTPClient http;
    http.begin(client, url);

    const char *headerKeys[] = { "X-Simperium-Version" };
    http.collectHeaders(headerKeys, 1);

    if (useToken)  http.addHeader("X-Simperium-Token", _token);
    if (useApiKey) http.addHeader("X-Simperium-API-Key", API_KEY);
    if (body.length() > 0) http.addHeader("Content-Type", "application/json");

    int code;
    if (method == "GET") code = http.GET();
    else if (method == "POST") code = http.POST(body);
    else if (method == "DELETE") code = http.sendRequest("DELETE", body);
    else code = http.sendRequest(method.c_str(), body);

    lastHttpCode = code;
    String response;
    if (code > 0)
    {
        response = http.getString();
        String v = http.header("X-Simperium-Version");
        lastVersion = v.length() > 0 ? v.toInt() : 0;
    }
    else
    {
        lastError = http.errorToString(code);
        lastVersion = 0;
    }
    http.end();
    return response;
}

bool Simplenote::authenticate()
{
    JsonDocument body;
    body["username"] = _username;
    body["password"] = _password;

    String payload;
    serializeJson(body, payload);

    String response = httpRequest(authUrl(), "POST", payload, false, true);

    if (lastHttpCode != 200)
    {
        lastError = "Login to Simplenote API failed! (HTTP " + String(lastHttpCode) + ")";
        _token = "";
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err)
    {
        lastError = "JSON parse error on auth response";
        return false;
    }

    if (!doc["access_token"].is<const char*>())
    {
        lastError = "No access_token in auth response";
        return false;
    }

    _token = doc["access_token"].as<String>();
    return true;
}

String Simplenote::getToken() {
    if (_token.length() == 0)
    {
        authenticate();
    }
    return _token;
}

void Simplenote::addSimplenoteApiFields(JsonDocument &note, const String &noteId, int version) {
    note["key"] = noteId;
    note["version"] = version;
    if (!note["modificationDate"].isNull())   note["modifydate"] = note["modificationDate"];
    if (!note["creationDate"].isNull())       note["createdate"] = note["creationDate"];
    if (!note["systemTags"].isNull())         note["systemtags"] = note["systemTags"];
}

void Simplenote::removeSimplenoteApiFields(JsonDocument &note) {
    note.remove("key");
    note.remove("version");

    if (!note["modifydate"].isNull()) {
        note["modificationDate"] = note["modifydate"];
        note.remove("modifydate");
    }
    if (!note["createdate"].isNull()) {
        note["creationDate"] = note["createdate"];
        note.remove("createdate");
    }
    if (!note["systemtags"].isNull()) {
        note["systemTags"] = note["systemtags"];
        note.remove("systemtags");
    }

    time_t createDate = time(nullptr);
    if (note["tags"].isNull())             note["tags"].to<JsonArray>();
    if (note["systemTags"].isNull())       note["systemTags"].to<JsonArray>();
    if (note["creationDate"].isNull())     note["creationDate"] = (double)createDate;
    if (note["modificationDate"].isNull()) note["modificationDate"] = (double)createDate;
    if (note["deleted"].isNull())          note["deleted"] = false;
    if (note["shareURL"].isNull())         note["shareURL"] = "";
    if (note["publishURL"].isNull())       note["publishURL"] = "";
}

String Simplenote::generateNoteId() {
    const char *hexChars = "0123456789abcdef";
    String id = "";
    id.reserve(32); 
    for (int i = 0; i < 32; i++) {
        id += hexChars[random(0, 16)];
    }
    return id;
}

bool Simplenote::getNote(const String &noteId, JsonDocument &noteOut, int version)
{
    if (getToken().length() == 0) return false;

    String url = dataUrl() + "/i/" + noteId;
    if (version >= 0) {
        url += "/v/" + String(version);
    }

    String response = httpRequest(url, "GET", "", true);

    if (lastHttpCode == 401) {
        lastError = "Login to Simplenote API failed! Check Token.";
        return false;
    }
    if (lastHttpCode != 200) {
        lastError = "get_note failed (HTTP " + String(lastHttpCode) + ")";
        return false;
    }

    noteOut.clear(); 
    DeserializationError err = deserializeJson(noteOut, response);
    if (err) {
        lastError = "JSON parse error on get_note response";
        return false;
    }

    addSimplenoteApiFields(noteOut, noteId, version >= 0 ? version : lastVersion);
    return true;
}

bool Simplenote::updateNote(JsonDocument &note)
{
    if (getToken().length() == 0) return false;

    String noteId = note["key"].isNull() ? generateNoteId() : note["key"].as<String>();
    String url;
    bool hasVersion = !note["version"].isNull();
    int version = hasVersion ? note["version"].as<int>() : -1;

    removeSimplenoteApiFields(note); 

    if (hasVersion) {
        url = dataUrl() + "/i/" + noteId + "/v/" + String(version) + "?response=1";
    } else {
        url = dataUrl() + "/i/" + noteId + "?response=1";
    }

    String payload;
    serializeJson(note, payload);

    String response = httpRequest(url, "POST", payload, true);

    if (lastHttpCode == 401) {
        lastError = "Login to Simplenote API failed! Check Token.";
        return false;
    }
    if (lastHttpCode < 200 || lastHttpCode >= 300) {
        lastError = "update_note failed (HTTP " + String(lastHttpCode) + ")";
        return false;
    }

    JsonDocument updated;
    DeserializationError err = deserializeJson(updated, response);
    if (err) {
        lastError = "JSON parse error on update_note response";
        return false;
    }

    addSimplenoteApiFields(updated, noteId, lastVersion > 0 ? lastVersion : (hasVersion ? version + 1 : 1));
    note = updated;
    return true;
}

bool Simplenote::addNote(const String &content, JsonDocument &noteOut) {
    noteOut.clear();
    noteOut["content"] = content;
    return updateNote(noteOut);
}

bool Simplenote::getNoteList(JsonDocument &notesOut, bool withData, const String &since, const String &tagFilter)
{
    if (getToken().length() == 0) return false;

    const int NOTE_FETCH_LENGTH = 1000;
    notesOut.clear();
    JsonArray index = notesOut["index"].to<JsonArray>();

    String params = "/index?limit=" + String(NOTE_FETCH_LENGTH);
    if (since.length() > 0) params += "&since=" + since;
    if (withData) params += "&data=true";

    String mark = "";
    do {
        String url = dataUrl() + params;
        if (mark.length() > 0) url += "&mark=" + mark;

        String response = httpRequest(url, "GET", "", true);

        if (lastHttpCode == 401) {
            lastError = "Login to Simplenote API failed! Check Token.";
            return false;
        }
        if (lastHttpCode != 200) {
            lastError = "get_note_list failed (HTTP " + String(lastHttpCode) + ")";
            return false;
        }

        JsonDocument page;
        DeserializationError err = deserializeJson(page, response);
        if (err) {
            lastError = "JSON parse error on get_note_list response";
            return false;
        }

        for (JsonObject n : page["index"].as<JsonArray>()) {
            JsonObject d = n["d"];
            JsonObject noteObj = index.add<JsonObject>();
            if (!d.isNull()) {
                noteObj.set(d);
            }
            noteObj["key"] = n["id"].as<String>();
            noteObj["version"] = n["v"].as<int>();
            if (!noteObj["modificationDate"].isNull()) noteObj["modifydate"] = noteObj["modificationDate"];
            if (!noteObj["creationDate"].isNull())     noteObj["createdate"] = noteObj["creationDate"];
            if (!noteObj["systemTags"].isNull())       noteObj["systemtags"] = noteObj["systemTags"];
        }

        _current = page["current"].as<String>();
        mark = page["mark"].isNull() ? "" : page["mark"].as<String>();
    } while (mark.length() > 0);

    if (tagFilter.length() > 0)
    {
        JsonDocument filtered;
        JsonArray filteredIndex = filtered["index"].to<JsonArray>();
        for (JsonObject n : index) {
            for (JsonVariant t : n["tags"].as<JsonArray>()) {
                if (t.as<String>() == tagFilter) {
                    filteredIndex.add(n);
                    break;
                }
            }
        }
        notesOut = filtered;
    }

    return true;
}

bool Simplenote::trashNote(const String &noteId) {
    JsonDocument note;
    if (!getNote(noteId, note)) return false;

    if (note["deleted"].isNull() || note["deleted"] == false) {
        note["deleted"] = true;
        note["modificationDate"] = (double)time(nullptr);
        return updateNote(note);
    }
    return true; 
}

bool Simplenote::deleteNote(const String &noteId) {
    if (!trashNote(noteId)) return false;

    String url = dataUrl() + "/i/" + noteId;
    httpRequest(url, "DELETE", "", true);

    if (lastHttpCode == 401) {
        lastError = "Login to Simplenote API failed! Check Token.";
        return false;
    }
    if (lastHttpCode < 200 || lastHttpCode >= 300) {
        lastError = "delete_note failed (HTTP " + String(lastHttpCode) + ")";
        return false;
    }
    return true;
}

bool isNoteCacheValid()
{
    if (!gNoteValid) return false; 
    unsigned long age = millis() - gNoteTimestamp;
    return age < NOTE_CACHE_MAX_AGE;
}

static void parseNoteContentToList()
{
    ClearList();

    String contenu = gNote["content"].as<String>();

    Serial.println("[SimpleNote] Content >\n" + contenu);
    Serial.println("[SimpleNote] End of content\n");

    if (!contenu.endsWith("\n")) contenu += "\n";

    int indexDebut = 0;
    int indexFin = contenu.indexOf('\n');

    while (indexFin != -1)
    {
        String ligne = contenu.substring(indexDebut, indexFin);
        ligne.trim();

        if (ligne.startsWith("- [ ]")) {
            String ingredient = ligne.substring(5);
            ingredient.trim();
            addItemList(ingredient.c_str());
        }

        indexDebut = indexFin + 1;
        indexFin = contenu.indexOf('\n', indexDebut);
    }
}

bool simpleNoteInit(void)
{
    sn.Init(g_SN_id.c_str(), g_SN_pass.c_str());
    return InitNote();
}

void simpleNoteRefresh(void)
{
    if (!gNoteValid) {
        InitNote();
        return;
    }

    String noteKey = gNote["key"].as<String>();
    GetItemsFromSN(noteKey);
}

void simpleNoteUpdate(const std::vector<std::string>& content)
{
    if (!gNoteValid) return;   

    String noteKey = gNote["key"].as<String>();

    if (!isNoteCacheValid())
    {
        if (!sn.getNote(noteKey, gNote))
        {
            Serial.println("[SimpleNote] Failed to read note: " + sn.lastError);
            gNoteValid = false;
            return;
        }
        gNoteTimestamp = millis();
    }

    String contenu = sMainTitle + "\n";
    for (const auto& item : content)
    {
        contenu += "- [ ] " + String(item.c_str()) + "\n";
    }
    gNote["content"] = contenu;

    if (sn.updateNote(gNote))
    {
        gNoteTimestamp = millis();
        Serial.println("[SimpleNote] Updated list, key = " + gNote["key"].as<String>());
        return;
    }

    Serial.println("[SimpleNote] Update failed, retrying: " + sn.lastError);

    if (!sn.getNote(noteKey, gNote))
    {
        Serial.println("[SimpleNote] Note retrieval verification failed: " + sn.lastError);
        gNoteValid = false;
        return;
    }
    gNoteTimestamp = millis();

    gNote["content"] = contenu;
    if (sn.updateNote(gNote))
    {
        gNoteTimestamp = millis();
        Serial.println("[SimpleNote] Updated list (after retry), key = " + gNote["key"].as<String>());
    }
    else
    {
        Serial.println("[SimpleNote] Permanent update failure: " + sn.lastError);
    }
}

bool InitNote(void)
{
    if (sn.authenticate()) {
        Serial.println("[SimpleNote] Auth success.");
    } else {
        Serial.println("[SimpleNote] Auth Failed: " + sn.lastError);
        return false;
    }

    JsonDocument list;
    if (sn.getNoteList(list))
    {
        JsonArray index = list["index"].as<JsonArray>();
        Serial.println("[SimpleNote] Total active notes: " + String(index.size()));
    
        for (JsonObject n : index)
        {
            if (n["deleted"].as<bool>()) continue; 
            if (n["content"].as<String>().indexOf(sMainTitle) == -1) continue;

            gNote.clear(); 
            gNote = n;
            gNoteValid = true;
            gNoteTimestamp = millis();

            Serial.println("[SimpleNote] Matching note identified, key = " + gNote["key"].as<String>());
            parseNoteContentToList();
            return true;
        }

        JsonDocument newNote;
        if (sn.addNote(sMainTitle, newNote))
        {
            Serial.println("[SimpleNote] Note generated successfully, key = " + newNote["key"].as<String>());
            gNote.clear();
            gNote = newNote;
            gNoteValid = true;
            gNoteTimestamp = millis();

            parseNoteContentToList();
            return true;
        }
        else
        {
            Serial.println("[SimpleNote] Failed note creation: " + sn.lastError);
        }
    }
    return false;
}

void GetItemsFromSN(const String &noteKey)
{
    if (sn.getNote(noteKey, gNote))
    {
        Serial.println("[SimpleNote] Read note with key" + noteKey);

        gNoteValid = true;
        gNoteTimestamp = millis();

        parseNoteContentToList();
    }
    else
    {
        Serial.println("[SimpleNote] Failed to read note " + noteKey  + ": " + sn.lastError);
    }

}