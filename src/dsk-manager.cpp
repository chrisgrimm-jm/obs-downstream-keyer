#include "dsk-manager.hpp"

#include <obs-frontend-api.h>
#include <util/platform.h>
#include <callback/signal.h>

#include <QCoreApplication>
#include <QTimer>

#include <cstring>

// ── Singleton ─────────────────────────────────────────────────────────────────

DskManager &DskManager::instance()
{
    static DskManager inst;
    return inst;
}

DskManager::DskManager() {}

DskManager::~DskManager()
{
    // Do NOT call OBS APIs here — by the time static destructors run during
    // exit(), OBS has already torn down its internal state (mutexes, sources).
    // All OBS cleanup is done in shutdown(), called on OBS_FRONTEND_EVENT_EXIT.
}

void DskManager::shutdown()
{
    if (m_shutdown) return;
    m_shutdown = true;

    unregisterAllHotkeys();

    obs_source_t *src = obs_get_source_by_name(m_sceneName.c_str());
    if (src) {
        disconnectSceneSignals(src);
        obs_source_release(src);
    }
}

// ── DSK scene access ──────────────────────────────────────────────────────────

obs_scene_t *DskManager::dskScene() const
{
    obs_source_t *src = obs_get_source_by_name(m_sceneName.c_str());
    if (!src) return nullptr;
    obs_scene_t *scene = obs_scene_from_source(src);
    obs_source_release(src);
    return scene;
}

obs_sceneitem_t *DskManager::findItem(const std::string &sourceName) const
{
    obs_scene_t *scene = dskScene();
    if (!scene) return nullptr;
    return obs_scene_find_source(scene, sourceName.c_str());
}

// ── Scene name change ─────────────────────────────────────────────────────────

void DskManager::setSceneName(const std::string &name)
{
    if (name == m_sceneName) return;

    // Disconnect from old scene
    obs_source_t *old = obs_get_source_by_name(m_sceneName.c_str());
    if (old) {
        disconnectSceneSignals(old);
        obs_source_release(old);
    }
    unregisterAllHotkeys();

    m_sceneName = name;

    // Connect to new scene
    obs_source_t *next = obs_get_source_by_name(m_sceneName.c_str());
    if (next) {
        connectSceneSignals(next);
        obs_source_release(next);
    }
    registerHotkeys();

    if (m_refreshCb) m_refreshCb();
}

// ── Item enumeration ──────────────────────────────────────────────────────────

std::vector<DskManager::ItemInfo> DskManager::currentItems() const
{
    std::vector<ItemInfo> result;
    obs_scene_t *scene = dskScene();
    if (!scene) return result;

    obs_scene_enum_items(scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
            auto *out = static_cast<std::vector<ItemInfo> *>(param);
            obs_source_t *src = obs_sceneitem_get_source(item);
            if (src) {
                const char *name = obs_source_get_name(src);
                if (name && *name)
                    out->push_back({name, obs_sceneitem_visible(item)});
            }
            return true;
        },
        &result);

    return result;
}

// ── Item control ──────────────────────────────────────────────────────────────

bool DskManager::isActive(const std::string &sourceName) const
{
    obs_sceneitem_t *item = findItem(sourceName);
    return item ? obs_sceneitem_visible(item) : false;
}

void DskManager::activate(const std::string &sourceName)
{
    obs_sceneitem_t *item = findItem(sourceName);
    if (!item) return;
    obs_sceneitem_set_visible(item, true);
    if (m_stateCb) m_stateCb(sourceName, true);

    // Schedule auto-hide if configured
    auto it = m_transitions.find(sourceName);
    if (it != m_transitions.end() && it->second.autoDuration > 0) {
        uint64_t seq = ++m_timerSeq[sourceName];
        uint32_t ms  = it->second.autoDuration * 1000;
        QTimer::singleShot(ms, QCoreApplication::instance(),
            [this, sourceName, seq]() {
                if (m_timerSeq[sourceName] == seq)
                    deactivate(sourceName);
            });
    }
}

void DskManager::deactivate(const std::string &sourceName)
{
    ++m_timerSeq[sourceName]; // invalidate any pending auto-hide timer
    obs_sceneitem_t *item = findItem(sourceName);
    if (!item) return;
    obs_sceneitem_set_visible(item, false);
    if (m_stateCb) m_stateCb(sourceName, false);
}

void DskManager::toggle(const std::string &sourceName)
{
    if (isActive(sourceName))
        deactivate(sourceName);
    else
        activate(sourceName);
}

// ── Transition config ─────────────────────────────────────────────────────────

const DskTransitionConfig *DskManager::transitionConfig(const std::string &sourceName) const
{
    auto it = m_transitions.find(sourceName);
    if (it == m_transitions.end()) return nullptr;
    return &it->second;
}

void DskManager::setTransitionConfig(const std::string &sourceName,
                                     const DskTransitionConfig &cfg)
{
    m_transitions[sourceName] = cfg;
    applyTransitions(sourceName);
}

// Applies the stored show/hide transition to the live scene item.
// OBS will own the transition sources; we release our reference after setting.
void DskManager::applyTransitions(const std::string &sourceName)
{
    obs_sceneitem_t *item = findItem(sourceName);
    if (!item) return;

    auto it = m_transitions.find(sourceName);
    if (it == m_transitions.end()) return;

    const DskTransitionConfig &cfg = it->second;

    auto makeTransition = [](const std::string &type, const std::string &settingsJson) -> obs_source_t * {
        if (type.empty()) return nullptr;
        obs_data_t *s = settingsJson.empty() ? nullptr
                      : obs_data_create_from_json(settingsJson.c_str());
        obs_source_t *t = obs_source_create(type.c_str(), nullptr, s, nullptr);
        if (s) obs_data_release(s);
        return t;
    };

    if (!cfg.showType.empty()) {
        obs_source_t *t = makeTransition(cfg.showType, cfg.showSettings);
        if (t) {
            obs_sceneitem_set_transition(item, true, t);
            obs_sceneitem_set_transition_duration(item, true, cfg.showDuration);
            obs_source_release(t);
        }
    } else {
        obs_sceneitem_set_transition(item, true, nullptr);
    }

    if (!cfg.hideType.empty()) {
        obs_source_t *t = makeTransition(cfg.hideType, cfg.hideSettings);
        if (t) {
            obs_sceneitem_set_transition(item, false, t);
            obs_sceneitem_set_transition_duration(item, false, cfg.hideDuration);
            obs_source_release(t);
        }
    } else {
        obs_sceneitem_set_transition(item, false, nullptr);
    }
}

// ── Setup helper ──────────────────────────────────────────────────────────────

void DskManager::addDskToAllScenes()
{
    obs_source_t *dskSrc = obs_get_source_by_name(m_sceneName.c_str());
    if (!dskSrc) {
        // Create the scene if it doesn't exist yet
        obs_scene_t *newScene = obs_scene_create(m_sceneName.c_str());
        if (!newScene) return;
        dskSrc = obs_source_get_ref(obs_scene_get_source(newScene));
    }

    struct obs_frontend_source_list list = {};
    obs_frontend_get_scenes(&list);

    for (size_t i = 0; i < list.sources.num; i++) {
        obs_source_t *sceneSrc  = list.sources.array[i];
        const char   *sceneName = obs_source_get_name(sceneSrc);
        if (sceneName && strcmp(sceneName, m_sceneName.c_str()) == 0) continue;

        obs_scene_t *scene = obs_scene_from_source(sceneSrc);
        if (!scene) continue;

        obs_sceneitem_t *existing = obs_scene_find_source(scene, m_sceneName.c_str());
        if (existing) {
            obs_sceneitem_set_order(existing, OBS_ORDER_MOVE_TOP);
            continue;
        }

        obs_sceneitem_t *item = obs_scene_add(scene, dskSrc);
        if (item) obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
    }

    obs_frontend_source_list_free(&list);
    obs_source_release(dskSrc);
    blog(LOG_INFO, "[dsk] Added '%s' to all scenes", m_sceneName.c_str());
}

// ── Hotkeys ───────────────────────────────────────────────────────────────────

void DskManager::registerHotkeys()
{
    obs_scene_t *scene = dskScene();
    if (!scene) return;

    obs_scene_enum_items(scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
            auto *mgr = static_cast<DskManager *>(param);
            obs_source_t *src = obs_sceneitem_get_source(item);
            if (!src) return true;
            const char *name = obs_source_get_name(src);
            if (!name || !*name) return true;

            // Skip if hotkey already registered for this source
            std::string sname = name;
            for (const auto &hk : mgr->m_hotkeys)
                if (hk.sourceName == sname) return true;

            std::string id   = "dsk_toggle_" + sname;
            std::string desc = "DSK: Toggle \"" + sname + "\"";

            auto *ctx = new HotkeyCtx{mgr, sname};
            obs_hotkey_id hkId = obs_hotkey_register_frontend(
                id.c_str(), desc.c_str(), cbHotkeyToggle, ctx);

            mgr->m_hotkeys.push_back({hkId, sname, ctx});
            return true;
        },
        this);
}

void DskManager::unregisterAllHotkeys()
{
    for (const auto &hk : m_hotkeys) {
        obs_hotkey_unregister(hk.id);
        delete hk.ctx;
    }
    m_hotkeys.clear();
}

void DskManager::cbHotkeyToggle(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed) return;
    auto *ctx = static_cast<HotkeyCtx *>(data);
    ctx->mgr->toggle(ctx->name);
}

// ── Scene signals ─────────────────────────────────────────────────────────────

void DskManager::connectSceneSignals(obs_source_t *sceneSource)
{
    signal_handler_t *sh = obs_source_get_signal_handler(sceneSource);
    signal_handler_connect(sh, "item_add",    cbItemAdd,    this);
    signal_handler_connect(sh, "item_remove", cbItemRemove, this);
}

void DskManager::disconnectSceneSignals(obs_source_t *sceneSource)
{
    signal_handler_t *sh = obs_source_get_signal_handler(sceneSource);
    signal_handler_disconnect(sh, "item_add",    cbItemAdd,    this);
    signal_handler_disconnect(sh, "item_remove", cbItemRemove, this);
}

void DskManager::cbItemAdd(void *data, calldata_t *cd)
{
    auto *mgr = static_cast<DskManager *>(data);

    obs_sceneitem_t *item = nullptr;
    calldata_get_ptr(cd, "item", &item);
    if (item) {
        // Sources added to the DSK scene always start hidden — the operator
        // explicitly punches them in via the dock. This prevents newly-added
        // sources from appearing on-air the moment they're dragged in.
        obs_sceneitem_set_visible(item, false);

        obs_source_t *src = obs_sceneitem_get_source(item);
        if (src) {
            const char *name = obs_source_get_name(src);
            if (name) mgr->applyTransitions(name);
        }
    }

    // Re-register hotkeys to include the new item
    mgr->unregisterAllHotkeys();
    mgr->registerHotkeys();

    if (mgr->m_refreshCb) mgr->m_refreshCb();
}

void DskManager::cbItemRemove(void *data, calldata_t *cd)
{
    (void)cd;
    auto *mgr = static_cast<DskManager *>(data);

    mgr->unregisterAllHotkeys();
    mgr->registerHotkeys();

    if (mgr->m_refreshCb) mgr->m_refreshCb();
}

// ── Persistence ───────────────────────────────────────────────────────────────

void DskManager::loadSettings()
{
    char *path = obs_module_config_path("settings.json");
    obs_data_t *root = obs_data_create_from_json_file(path);
    bfree(path);

    if (!root) return;

    const char *scene = obs_data_get_string(root, "dsk_scene");
    if (scene && *scene) m_sceneName = scene;

    m_httpPort = (int)obs_data_get_int(root, "http_port");
    if (m_httpPort <= 0 || m_httpPort > 65535) m_httpPort = 4488;

    const char *ds = obs_data_get_string(root, "dock_state");
    if (ds) m_dockState = ds;

    m_viewMode = (int)obs_data_get_int(root, "view_mode");

    obs_data_array_t *items = obs_data_get_array(root, "item_transitions");
    if (items) {
        size_t count = obs_data_array_count(items);
        for (size_t i = 0; i < count; i++) {
            obs_data_t *entry = obs_data_array_item(items, i);
            const char *name = obs_data_get_string(entry, "source");
            if (name && *name) {
                DskTransitionConfig cfg;
                const char *st = obs_data_get_string(entry, "show_type");
                if (st) cfg.showType = st;
                cfg.showDuration = (uint32_t)obs_data_get_int(entry, "show_dur");
                const char *ss = obs_data_get_string(entry, "show_settings");
                if (ss) cfg.showSettings = ss;
                const char *ht = obs_data_get_string(entry, "hide_type");
                if (ht) cfg.hideType = ht;
                cfg.hideDuration = (uint32_t)obs_data_get_int(entry, "hide_dur");
                const char *hs = obs_data_get_string(entry, "hide_settings");
                if (hs) cfg.hideSettings = hs;
                cfg.autoDuration = (uint32_t)obs_data_get_int(entry, "auto_dur");
                m_transitions[name] = cfg;
            }
            obs_data_release(entry);
        }
        obs_data_array_release(items);
    }

    obs_data_release(root);

    // Connect to the DSK scene and register hotkeys
    obs_source_t *src = obs_get_source_by_name(m_sceneName.c_str());
    if (src) {
        connectSceneSignals(src);
        obs_source_release(src);
    }
    registerHotkeys();

    // Apply saved transitions to any existing items
    obs_scene_t *scene2 = dskScene();
    if (scene2) {
        obs_scene_enum_items(scene2,
            [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
                auto *mgr = static_cast<DskManager *>(param);
                obs_source_t *src = obs_sceneitem_get_source(item);
                if (src) {
                    const char *n = obs_source_get_name(src);
                    if (n) mgr->applyTransitions(n);
                }
                return true;
            },
            this);
    }
}

void DskManager::saveSettings()
{
    char *dir = obs_module_config_path("");
    os_mkdirs(dir);
    bfree(dir);

    obs_data_t *root = obs_data_create();
    obs_data_set_string(root, "dsk_scene", m_sceneName.c_str());
    obs_data_set_int(root, "http_port", m_httpPort);
    obs_data_set_string(root, "dock_state", m_dockState.c_str());
    obs_data_set_int(root,    "view_mode",  m_viewMode);

    obs_data_array_t *items = obs_data_array_create();
    for (const auto &[name, cfg] : m_transitions) {
        obs_data_t *entry = obs_data_create();
        obs_data_set_string(entry, "source",        name.c_str());
        obs_data_set_string(entry, "show_type",     cfg.showType.c_str());
        obs_data_set_int(entry,    "show_dur",      cfg.showDuration);
        obs_data_set_string(entry, "show_settings", cfg.showSettings.c_str());
        obs_data_set_string(entry, "hide_type",     cfg.hideType.c_str());
        obs_data_set_int(entry,    "hide_dur",      cfg.hideDuration);
        obs_data_set_string(entry, "hide_settings", cfg.hideSettings.c_str());
        obs_data_set_int(entry,    "auto_dur",      cfg.autoDuration);
        obs_data_array_push_back(items, entry);
        obs_data_release(entry);
    }
    obs_data_set_array(root, "item_transitions", items);
    obs_data_array_release(items);

    char *path = obs_module_config_path("settings.json");
    obs_data_save_json_safe(root, path, "tmp", "bak");
    bfree(path);
    obs_data_release(root);
}
