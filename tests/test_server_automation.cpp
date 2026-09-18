#include "automation.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
int failures = 0;

void require(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "FEHLER: " << message << '\n';
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

airctrl::AutomationConfig configFor(const std::filesystem::path& directory,
                                    const std::string& name) {
    return {(directory / (name + ".lua")).string(),
            (directory / (name + ".json")).string(), false};
}
}

int main() {
    char pattern[] = "/tmp/airctrl-automation-test-XXXXXX";
    const char* created = mkdtemp(pattern);
    require(created != nullptr, "Temporärer Testordner konnte nicht angelegt werden.");
    if (created == nullptr) return 1;
    const std::filesystem::path directory(created);
    const std::string example = readFile(AUTOMATION_EXAMPLE_FILE);
    require(!example.empty(), "Lua-Beispiel fehlt.");

    {
        airctrl::AutomationEngine engine(configFor(directory, "example"), "1.09");
        std::string error;
        require(engine.saveScript(example, true, &error), "Beispiel wird geladen: " + error);
        const airctrl::Json state = engine.stateJson();
        require(state.value("loaded", false), "Aktiviertes Beispiel ist geladen.");
        require(state.value("schedule_count", 0) == 1,
            "Beispiel enthält eine vollständige Tag/Nacht-Regel.");
        require(state.value("lua_version", std::string{}) == "Lua 5.4.9", "Lua-Version ist 5.4.9.");
    }

    {
        airctrl::AutomationEngine engine(configFor(directory, "between"), "1.09");
        std::string error;
        require(engine.saveScript(R"lua(
            airctrl.schedule {
                name="tag/nacht", between="07:00-22:00",
                days={1,2,3,4,5,6,7}, catch_up=true,
                set={mode="P",uil="1"},
                outside={mode="S",om="s",uil="0"}
            }
        )lua", true, &error), "between-Tag/Nacht-Regel wird geladen: " + error);
        require(engine.stateJson().value("schedule_count", 0) == 1,
            "between wird als eine benannte Regel gemeldet.");
    }

    {
        airctrl::AutomationEngine engine(configFor(directory, "invalid"), "1.09");
        std::string error;
        require(!engine.saveScript(
            "airctrl.schedule{name='x',at='7:00',set={evil=1}}", true, &error),
            "Ungültiger Zeitplan wird abgelehnt.");
        require(error.find("HH:MM") != std::string::npos,
            "Ablehnung erklärt das Zeitformat.");
    }

    {
        airctrl::AutomationEngine engine(configFor(directory, "event"), "1.09");
        std::vector<airctrl::AutomationAction> actions;
        engine.setActionHandler([&](airctrl::AutomationAction action) {
            actions.push_back(std::move(action));
        });
        std::string error;
        require(engine.saveScript(R"lua(
            function on_event(event)
                if event.type == "status" and event.changed.rh and event.status.rh < 35 then
                    airctrl.set { func="PH" }
                end
            end
        )lua", true, &error), "Ereignisskript wird geladen: " + error);
        engine.statusEvent({{"rh", 34}, {"func", "P"}});
        require(actions.size() == 1U, "Statusereignis erzeugt genau einen Auftrag.");
        if (!actions.empty()) require(actions.front().values == airctrl::Json{{"func", "PH"}},
            "Statusereignis übergibt den erwarteten Steuerwert.");
        actions.clear();
        engine.statusEvent({{"rh", 34}, {"func", "P"}});
        require(actions.empty(), "Unveränderter Status löst die Regel nicht erneut aus.");
    }

    {
        airctrl::AutomationEngine engine(configFor(directory, "long-timer"), "1.09");
        std::vector<airctrl::AutomationAction> actions;
        engine.setActionHandler([&](airctrl::AutomationAction action) {
            actions.push_back(std::move(action));
        });
        std::string error;
        require(engine.saveScript(R"lua(
            function on_long_timer()
                airctrl.set { mode="P", uil="1" }
            end
        )lua", true, &error), "on_long_timer-Skript wird geladen: " + error);
        require(engine.setManualOverride(true, "Test-Sperre"),
            "Manuelle Sperre wird für den Langdrucktest aktiviert.");
        require(engine.longTimerEvent(), "on_long_timer wird trotz manueller Sperre ausgeführt.");
        require(actions.size() == 1U, "on_long_timer erzeugt genau einen Auftrag.");
        if (!actions.empty()) {
            require(actions.front().values == airctrl::Json{{"mode", "P"}, {"uil", "1"}},
                "on_long_timer übergibt die erwarteten Steuerwerte.");
            require(actions.front().source == "Lua-Ereignis long_timer",
                "Der Langdruckauftrag hat eine eindeutige Quelle.");
        }
    }

    {
        const airctrl::AutomationConfig config = configFor(directory, "persistent");
        std::string error;
        {
            airctrl::AutomationEngine writer(config, "1.09");
            require(writer.saveScript("airctrl.log('info', 'ok')", false, &error),
                "Deaktiviertes Skript wird gespeichert: " + error);
        }
        airctrl::AutomationEngine reader(config, "1.09");
        require(reader.initialize(&error), "Serverzustand wird erneut geladen: " + error);
        require(!reader.stateJson().value("enabled", true), "Deaktivierung bleibt serverseitig erhalten.");
        std::string stored;
        require(reader.readScript(&stored, &error) && stored == "airctrl.log('info', 'ok')",
            "Server liefert bytegleich das gespeicherte Skript.");
    }

    {
        const airctrl::AutomationConfig config = configFor(directory, "manual-override");
        std::vector<airctrl::AutomationAction> actions;
        std::string error;
        {
            airctrl::AutomationEngine engine(config, "1.09");
            engine.setActionHandler([&](airctrl::AutomationAction action) {
                actions.push_back(std::move(action));
            });
            require(engine.saveScript(R"lua(
                function on_event(event)
                    if event.type == "status" and event.status.rh < 35 then
                        airctrl.set { func="PH" }
                    end
                end
            )lua", true, &error), "Sperrtest-Skript wird geladen: " + error);
            require(engine.setManualOverride(true, "Manueller Modus"),
                "Manuelle Automatik-Sperre wird aktiviert.");
            engine.statusEvent({{"rh", 34}, {"func", "P"}});
            require(actions.empty(), "Während der Sperre wird keine Lua-Aktion ausgelöst.");
            require(engine.stateJson().value("manual_override", false),
                "Sperre wird an Clients gemeldet.");
        }
        airctrl::AutomationEngine engine(config, "1.09");
        engine.setActionHandler([&](airctrl::AutomationAction action) {
            actions.push_back(std::move(action));
        });
        require(engine.initialize(&error), "Gesperrter Zustand wird neu geladen: " + error);
        require(engine.manualOverride(), "Manuelle Sperre überlebt einen Serverneustart.");
        require(engine.setManualOverride(false), "Manuelle Sperre wird aufgehoben.");
        engine.reevaluate({{"rh", 34}, {"func", "P"}});
        require(actions.size() == 1U,
            "Freigabe wertet das Skript mit dem aktuellen Gerätestatus sofort neu aus.");
    }

    std::filesystem::remove_all(directory);
    if (failures == 0) std::cout << "Alle serverseitigen Lua-Tests erfolgreich.\n";
    return failures == 0 ? 0 : 1;
}
