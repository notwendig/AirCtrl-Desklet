-- AirCtrl-Desklet Lua-Automatik
-- Wochentage: 1=Montag ... 7=Sonntag. Zeiten verwenden die lokale Rechnerzeit.

airctrl.schedule {
    name = "nacht",
    at = "22:00",
    days = {1, 2, 3, 4, 5, 6, 7},
    set = { mode = "S", om = "s", uil = "0" }
}

airctrl.schedule {
    name = "tag",
    at = "07:00",
    days = {1, 2, 3, 4, 5, 6, 7},
    set = { mode = "P", uil = "1" }
}

function on_event(event)
    if event.type == "connected" then
        airctrl.log("info", "Gerät verbunden")
    elseif event.type == "alarm" and event.highest == "error" then
        airctrl.log("warning", "Geräte- oder Verbindungsfehler aktiv")
    end

    -- Beispiel für eine ereignisgesteuerte Regel (absichtlich deaktiviert):
    -- if event.type == "status" and event.changed.rh and event.status.rh < 35 then
    --     airctrl.set { func = "PH" }
    -- end
end
