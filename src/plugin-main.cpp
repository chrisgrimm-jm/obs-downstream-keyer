#include "plugin-main.h"
#include "dsk-manager.hpp"
#include "dsk-dock.hpp"
#include "companion-server.hpp"

#include <QMainWindow>
#include <QTimer>


static DskDock *s_dock = nullptr;

static void on_frontend_event(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        DskManager::instance().loadSettings();

        s_dock = new DskDock();
        obs_frontend_add_dock_by_id(
            "DownstreamKeyersDock",
            "Downstream Keyers",
            s_dock);

        // Restore dock position after OBS finishes its own layout pass.
        const std::string saved = DskManager::instance().dockState();
        if (!saved.empty()) {
            QTimer::singleShot(200, []() {
                auto *mainWin = static_cast<QMainWindow *>(
                    obs_frontend_get_main_window());
                if (!mainWin) return;
                mainWin->restoreState(QByteArray::fromBase64(
                    QByteArray::fromStdString(
                        DskManager::instance().dockState())));
            });
        }

        g_companionServer = new CompanionServer();
        g_companionServer->start(
            static_cast<quint16>(DskManager::instance().httpPort()));

    } else if (event == OBS_FRONTEND_EVENT_EXIT) {
        auto *mainWin = static_cast<QMainWindow *>(
            obs_frontend_get_main_window());
        if (mainWin)
            DskManager::instance().setDockState(
                mainWin->saveState().toBase64().toStdString());

        DskManager::instance().saveSettings();
        DskManager::instance().shutdown();

        if (g_companionServer) {
            g_companionServer->stop();
            delete g_companionServer;
            g_companionServer = nullptr;
        }
    }
}

bool obs_module_load()
{
    blog(LOG_INFO, "[dsk] Loading v%s", PLUGIN_VERSION);
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    return true;
}

void obs_module_unload()
{
    blog(LOG_INFO, "[dsk] Unloading");
}

const char *obs_module_description()
{
    return "Downstream keyer dock — toggle DSK scene sources with configurable transitions";
}
