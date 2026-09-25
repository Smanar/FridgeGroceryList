/*
    Simplenote.h
    ~~~~~~~~~~~~~~

    C++ adaptation (ESP32 / Arduino) of simplenote.py
    Uses HTTPClient + WiFiClientSecure + ArduinoJson (v6 or v7)

    Required libraries (install via Library Manager):
    - ArduinoJson (bblanchon)
    - (HTTPClient and WiFiClientSecure are included with the ESP32 core)
*/


#ifndef SIMPLENOTE_H
#define SIMPLENOTE_H

#include <Arduino.h>
#include <ArduinoJson.h>

class Simplenote {
public:
    Simplenote(void);

    // Authenticates and caches the token. Returns true on success.
    bool authenticate();

    // Returns the current token (obtains it via authenticate() if necessary).
    String getToken();

    // Retrieve a note. Populates `note Out`. version = -1 => latest version.
    // Returns true on success.
    bool getNote(const String &noteId, JsonDocument &noteOut, int version = -1);

    // Creates or updates a note. `note` must contain at least "content". 
    // If `note` contains "key" (and optionally "version"), it is an update. 
    // The result (with updated key/version) is written to `note`.
    bool updateNote(JsonDocument &note);

    // Shortcut to create a note from plain text.
    bool addNote(const String &content, JsonDocument &noteOut);

    // Retrieves the list of notes (with automatic pagination via "mark"). 
    // If `withData` is false, only key/version are returned. 
    // Optional `tagFilter`: keep only notes with this tag.
    bool getNoteList(JsonDocument &notesOut, bool withData = true,
                      const String &since = "", const String &tagFilter = "");

    // Marks a note as deleted (deleted = true).
    bool trashNote(const String &noteId);

    // Permanently delete a note (first goes through trashNote).
    bool deleteNote(const String &noteId);

    void Init(const String &username, const String &password);

    // Last HTTP code received / error message, useful for debugging.
    int lastHttpCode = 0;
    String lastError = "";
    int lastVersion = 0;

private:
    String _username;
    String _password;
    String _token;
    String _current; // curseur Simperium

    static const char *APP_ID;
    static const char *API_KEY;
    static const char *AUTH_HOST;
    static const char *DATA_HOST;

    String authUrl() const;
    String dataUrl() const;

    // Performs a generic HTTPS request.
    // method: "GET", "POST", "DELETE"
    // extraHeaderName/Value: additional header (e.g., token) without using the headerAuth parameter
    // useToken: if true, adds the X-Simperium-Token header
    // Returns the response body; updates lastHttpCode.
    String httpRequest(const String &url, const String &method, const String &body, bool useToken, bool useApiKey = false);

    void addSimplenoteApiFields(JsonDocument &note, const String &noteId, int version);
    void removeSimplenoteApiFields(JsonDocument &note);
    String generateNoteId();
};

// Get the Shopping list key from all other notes
bool InitNote(void);

void GetItemsFromSN(const String &noteKey);
bool simpleNoteInit(void);
void simpleNoteUpdate(const std::vector<std::string>& content);
void simpleNoteRefresh(void);

#endif
