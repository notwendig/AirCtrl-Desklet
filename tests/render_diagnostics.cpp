// Public documentation preview, using only synthetic data and the real Qt UI.
#include "airctrl_version.hpp"
#include "desklet.hpp"
#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("AirControl-Demo");
    QCoreApplication::setApplicationName("airctrl-diagnostics-preview");
    QCoreApplication::setApplicationVersion(AIRCTRL_VERSION);
    QApplication::setStyle("Fusion");
    if (app.arguments().size() != 2) {
        std::fprintf(stderr, "Usage: diagnostics-preview output.png\n");
        return 2;
    }
    const QString output = app.arguments().at(1);
    if (QFileInfo::exists(output)) {
        std::fprintf(stderr, "Output already exists; choose a new filename.\n");
        return 2;
    }
    Preferences preferences;
    preferences.serverHost = "nadhh";
    preferences.serverPort = 5680;
    // Demo mode never starts the controller and never sends device commands.
    Desklet widget(preferences, "/demo/airctrl-backend", true);
    widget.applyStatus({
        {"ConnectType", "Online"}, {"DeviceId", "demo-device"},
        {"ProductId", "demo-product"}, {"Runtime", 3600000},
        {"StatusType", "status"}, {"WifiVersion", "Demo firmware"},
        {"aqil", 100}, {"aqit", 4}, {"aqit_ext", 0}, {"cl", false},
        {"ddp", "3"}, {"dt", 0}, {"dtrs", 0}, {"err", 49236},
        {"fltsts0", 325}, {"fltsts1", 87}, {"fltsts2", 87},
        {"fltt1", "A3"}, {"fltt2", "C7"}, {"func", "PH"},
        {"iaql", 1}, {"mode", "P"}, {"modelid", "AC2729/10"},
        {"name", "Demo"}, {"om", "s"}, {"pm25", 2}, {"pwr", "1"},
        {"rh", 55}, {"rhset", 50}, {"rssi", -45}, {"temp", 24},
        {"type", "AC2729"}, {"uil", "1"}, {"wicksts", 87}, {"wl", 100}
    });
    bool saved = false;
    QTimer::singleShot(100, &widget, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        dialog->resize(1060, 760);
        QTimer::singleShot(100, dialog, [&, dialog] {
            saved = dialog->grab().save(output, "PNG");
            dialog->accept();
        });
    });
    // Bound the headless helper if a modal dialog cannot be created/rendered.
    QTimer::singleShot(5000, &app, &QApplication::closeAllWindows);
    widget.showDetails();
    if (!saved) std::fprintf(stderr, "Could not render diagnostics preview.\n");
    return saved ? 0 : 1;
}
