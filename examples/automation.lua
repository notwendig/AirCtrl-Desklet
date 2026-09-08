-- AirCtrl-Desklet Lua-Automatik
--
-- Diese Datei ist zugleich die Vorlage des integrierten Editors.
-- Die Automatik ist nach der Installation ausgeschaltet. Erst prüfen, dann
-- über Rechtsklick -> Lua-Automatik aktivieren.
--
-- ==========================================================================
-- API
-- ==========================================================================
-- airctrl.schedule { ... }       Zeitplan beim Laden registrieren (maximal 64)
-- airctrl.set { ... }            genau einen Geräteauftrag im aktuellen Ereignis
-- airctrl.status()               Kopie des letzten bestätigten Gerätestatus
-- airctrl.log(stufe, nachricht)  stufe: "debug", "info", "warning", "error"
-- airctrl.version                AirCtrl-Desklet-Version
-- airctrl.lua_version            eingebettete Lua-Version
--
-- airctrl.set ist nur in connected, time, status und alarm erlaubt, höchstens
-- einmal je Ereignis. startup, disconnected und command dürfen nicht schalten.
-- Ein Auftrag benutzt dieselbe Positivliste und Statusbestätigung wie die GUI.
--
-- ==========================================================================
-- ALLE EREIGNISSE: function on_event(event)
-- ==========================================================================
-- Jedes Ereignis:
--   event.type       Name des Ereignisses
--   event.timestamp  lokale Empfangs-/Ausführungszeit im ISO-Format
--
-- startup
--   Nach erfolgreichem Laden des Skripts.
--   event.time = { iso, date, time, year, month, day, weekday, hour, minute }
--
-- time
--   Einmal je lokale Kalenderminute.
--   event.iso, event.date, event.time, event.year, event.month, event.day,
--   event.weekday (1=Montag ... 7=Sonntag), event.hour, event.minute
--
-- connected
--   Beim Wechsel zu einer gültigen Statusverbindung.
--
-- disconnected
--   Beim Verbindungsverlust. event.reason enthält die Fehlermeldung.
--
-- status
--   Nach jeder übernommenen, gültigen Gerätemeldung.
--   event.status   vollständiger bestätigter Status (Felder siehe unten)
--   event.changed  nur Änderungen: event.changed.<feld>.old / .new
--                  nil bedeutet: Feld fehlte vorher oder wurde entfernt
--   event.first    true beim ersten Status nach Skriptstart
--
-- alarm
--   Wenn sich Alarmidentitäten oder Alarmstufen ändern.
--   event.alerts   Liste aus { id, level, message }
--   event.count    Anzahl aktiver Alarme
--   event.highest  "none", "warning" oder "error"
--
-- command
--   Ergebnis eines von Lua ausgelösten Auftrags.
--   event.source   auslösendes Ereignis oder Name des Zeitplans
--   event.ok       true = bestätigt, false = fehlgeschlagen
--   event.message  Ergebnistext
--   Aus command darf kein neuer Auftrag gestartet werden (Schleifenschutz).
--
-- ==========================================================================
-- BESTÄTIGTE/BEOBACHTETE STATUSFELDER UND BEDEUTUNG
-- ==========================================================================
-- Nicht jedes Modell und jede Firmware liefert jedes Feld. Unbekannte Werte
-- bleiben Rohwerte; das Skript sollte ein fehlendes Feld immer berücksichtigen.
--
-- pwr          "1" = Gerät ein, "0" = Gerät aus
-- cl           true = Kindersicherung ein, false = aus
-- mode         "P" = Automatik, "A" = Allergie, "S" = Nacht,
--              "M" = manuelle Lüfterwahl
-- om           "1", "2", "3" = Lüfterstufe, "s" = leise/Nacht,
--              "t" = Turbo
-- func         "P" = nur Luftreinigung, "PH" = Reinigen + Befeuchten
-- uil          "1" = Geräteanzeige beleuchtet, "0" = aus
-- rh           gemessene relative Luftfeuchte in Prozent
-- rhset        Zielfeuchte in Prozent (40, 50, 60 oder 70)
-- temp         Temperatur in Grad Celsius
-- pm25         PM2,5-Feinstaub in Mikrogramm pro Kubikmeter
-- iaql         Philips-Innenraum-Allergenindex (IAI), keine Konzentration
-- aqil         Helligkeit des Luftqualitäts-Lichtrings: 0..100
-- dt           Abschalttimer in Stunden; 0 = aus, sonst 1..12
-- wl           Wasserstatus; beim AC2729 bedeutet 0 Nachfüllen.
--              100 ist kein gesichert gemessener Prozentfüllstand.
-- rssi         WLAN-Signalstärke in dBm; weniger negativ = stärker
--
-- fltsts0      Restbetriebsstunden bis zur Vorfilterreinigung
-- fltsts1      Restbetriebsstunden bis HEPA-Wechsel (A3)
-- fltsts2      Restbetriebsstunden bis Aktivkohlefilter-Wechsel (C7)
-- wicksts      Restbetriebsstunden bis Befeuchtungsdocht-Wechsel (F1)
-- fltt1        HEPA-Filterkennung, beim AC2729 üblicherweise "A3"
-- fltt2        Aktivkohlefilterkennung, beim AC2729 üblicherweise "C7"
-- err          roher Geräte-Fehler-/Statuscode; unbekannte Bits nicht raten
--
-- ConnectType  vom Gerät gemeldeter Zustand, z.B. "Online"
-- StatusType   Antwortart, z.B. "status" oder "control"
-- name         frei vergebener Gerätename
-- modelid      vollständiges Modell, z.B. "AC2729/10"
-- type         Gerätebaureihe, z.B. "AC2729"
-- range        interne Philips-Plattform-/Familienkennung
-- swversion    Geräte-Firmwareversion
-- WifiVersion  Firmwarekennung des WLAN-Moduls
-- DeviceId     interne Gerätekennung (vor öffentlichen Logs entfernen)
-- ProductId    interne Produktkennung (vor öffentlichen Logs entfernen)
-- Runtime      interner Laufzeitzähler; keine bestätigten Lebensdauerstunden
-- free_memory  freier Gerätespeicher; Einheit nicht gesichert
-- aqit         interner Luftqualitätsindex; Skala nicht gesichert
-- aqit_ext     Philips-Zusatzfeld zur Luftqualität; Bedeutung nicht gesichert
-- ddp          ausgewählte Anzeige; 0=IAI, 1=PM2,5, 2=Gas in Referenzen;
--              weitere Werte sind nicht gesichert
-- rddp         zusätzliches Anzeigefeld; Zuordnung nicht gesichert
-- dtrs         vermutlich verbleibende Minuten des Geräte-Abschalttimers:
--              AC2729/10, Mitschnitt 2026-09-08: dt=6 -> dtrs=360,
--              etwa 60 s später 359. Beobachtung, keine Philips-Spezifikation.
--              Nur lesen; fehlt das Feld, ist es nil. dt bleibt Stunden.
--              Unabhängig von lokalen Tag/Nacht-Regeln (airctrl.schedule).
-- otacheck     vermutlich interne Firmware-Update-Prüfung
-- wifilog      vermutlich interne WLAN-Protokollierung
--
-- ==========================================================================
-- ALLE ERLAUBTEN STEUERWERTE FÜR airctrl.set UND schedule.set
-- ==========================================================================
-- pwr   = "0" | "1"                    Aus | Ein
-- cl    = false | true                  Kindersicherung Aus | Ein
-- mode  = "P" | "A" | "S" | "M"       Automatik | Allergie | Nacht | Manuell
-- om    = "1" | "2" | "3" | "s" | "t" Stufe 1..3 | leise | Turbo
-- func  = "P" | "PH"                   Reinigen | Reinigen+Befeuchten
-- uil   = "0" | "1"                    Geräteanzeige Aus | Ein
-- rhset = 40 | 50 | 60 | 70             Zielfeuchte in Prozent (Integer)
-- aqil  = 0 | 25 | 50 | 75 | 100        Lichtringhelligkeit (Integer)
-- dt    = 0..12                          Abschalttimer in Stunden (Integer)
--
-- Wegen der Geräte-/Backendkodierung dürfen Integerfelder (rhset, aqil, dt)
-- in einem einzelnen Auftrag nicht mit String-/Booleanfeldern gemischt werden.
-- Dafür getrennte Ereignisse oder Zeitpläne mit mindestens einer Minute Abstand
-- verwenden.
--
-- ==========================================================================
-- ZEITPLÄNE
-- ==========================================================================
-- name      eindeutiger Name: 1..64 Zeichen aus A-Z, a-z, 0-9, _ . -
-- at        lokale Rechnerzeit exakt als "HH:MM"
-- days      optional: 1=Montag ... 7=Sonntag; ohne days täglich
-- catch_up  optionaler Boolean, Standard true:
--           true  = nach einem Start nur jüngsten fälligen Termin nachholen
--           false = ausschließlich in der exakten Minute ausführen
-- set       erlaubte Steuerwerte aus der Liste oben
--
-- Gleiche Uhrzeit darf sich bei zwei Zeitplänen nicht am selben Wochentag
-- überschneiden. Jeder übergebene Termin wird höchstens einmal versucht.

airctrl.schedule {
    name = "nacht",
    at = "22:00",
    days = {1, 2, 3, 4, 5, 6, 7},
    catch_up = true,
    set = { mode = "S", om = "s", uil = "0" }
}

airctrl.schedule {
    name = "tag",
    at = "07:00",
    days = {1, 2, 3, 4, 5, 6, 7},
    catch_up = true,
    set = { mode = "P", uil = "1" }
}

function on_event(event)
    if event.type == "connected" then
        airctrl.log("info", "Gerät verbunden")
    elseif event.type == "disconnected" then
        airctrl.log("warning", "Verbindung getrennt: " .. (event.reason or "unbekannt"))
    elseif event.type == "alarm" and event.highest == "error" then
        airctrl.log("warning", "Geräte- oder Verbindungsfehler aktiv")
    end

    -- Beispiel für eine ereignisgesteuerte Regel (absichtlich deaktiviert):
    -- if event.type == "status" and event.changed.rh and event.status.rh < 35 then
    --     airctrl.set { func = "PH" }
    -- end
end
