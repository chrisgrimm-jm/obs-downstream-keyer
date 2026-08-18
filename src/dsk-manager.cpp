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

    obs_frontend_remove_event_callback(cbFrontendEvent, this);

    signal_handler_disconnect(obs_get_signal_handler(), "source_rename",
                              cbSourceRename, this);

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
        m_expiryTime[sourceName] = std::chrono::steady_clock::now()
                                 + std::chrono::milliseconds(ms);
        QTimer::singleShot(ms, QCoreApplication::instance(),
            [this, sourceName, seq]() {
                if (m_timerSeq[sourceName] == seq)
                    deactivate(sourceName);
            });
    } else {
        m_expiryTime.erase(sourceName); // no countdown — clear any stale entry
    }
}

void DskManager::deactivate(const std::string &sourceName)
{
    ++m_timerSeq[sourceName]; // invalidate any pending auto-hide timer
    m_expiryTime.erase(sourceName);
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

void DskManager::setButtonColor(const std::string &sourceName, const std::string &colorHex)
{
    m_transitions[sourceName].buttonColor = colorHex;
}

double DskManager::timeRemaining(const std::string &sourceName) const
{
    if (!isActive(sourceName)) return -1.0;
    auto it = m_expiryTime.find(sourceName);
    if (it == m_expiryTime.end()) return -1.0; // active but no countdown
    double rem = std::chrono::duration<double>(
        it->second - std::chrono::steady_clock::now()).count();
    return rem < 0.0 ? 0.0 : rem;
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

// ── Playlist ──────────────────────────────────────────────────────────────────

void DskManager::setPlaylist(std::vector<PlaylistEntry> entries)
{
    m_playlist = std::move(entries);
}

void DskManager::startPlaylist()
{
    if (m_playlist.empty()) return;
    m_playlistRunning = true;
    m_playlistInGap   = false;
    m_playlistIndex   = 0;
    ++m_playlistSeq;
    schedulePlaylistStep();
}

void DskManager::stopPlaylist()
{
    ++m_playlistSeq;
    if (m_playlistRunning && !m_playlistInGap && m_playlistIndex >= 0
        && m_playlistIndex < (int)m_playlist.size()) {
        deactivate(m_playlist[m_playlistIndex].sourceName);
    }
    m_playlistRunning = false;
    if (m_refreshCb) m_refreshCb();
}

void DskManager::schedulePlaylistStep()
{
    if (!m_playlistRunning) return;
    if (m_playlistIndex < 0 || m_playlistIndex >= (int)m_playlist.size()) {
        m_playlistRunning = false;
        if (m_refreshCb) m_refreshCb();
        return;
    }

    const PlaylistEntry &entry = m_playlist[m_playlistIndex];
    uint64_t seq = m_playlistSeq;

    if (!m_playlistInGap) {
        activate(entry.sourceName);
        uint32_t ms = entry.onDuration * 1000;
        m_playlistStepExpiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        if (m_refreshCb) m_refreshCb();
        QTimer::singleShot((int)ms, QCoreApplication::instance(),
            [this, seq, entry]() {
                if (m_playlistSeq != seq) return;
                deactivate(entry.sourceName);
                m_playlistInGap = true;
                schedulePlaylistStep();
            });
    } else {
        uint32_t ms = entry.offDuration * 1000;
        m_playlistStepExpiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        if (m_refreshCb) m_refreshCb();
        QTimer::singleShot((int)ms, QCoreApplication::instance(),
            [this, seq]() {
                if (m_playlistSeq != seq) return;
                m_playlistInGap = false;
                m_playlistIndex = (m_playlistIndex + 1) % (int)m_playlist.size();
                schedulePlaylistStep();
            });
    }
}

DskManager::PlaylistStatus DskManager::playlistStatus() const
{
    PlaylistStatus s;
    s.running = m_playlistRunning;
    if (!m_playlistRunning) return s;
    s.inGap = m_playlistInGap;
    s.index = m_playlistIndex;
    if (m_playlistIndex >= 0 && m_playlistIndex < (int)m_playlist.size())
        s.sourceName = m_playlist[m_playlistIndex].sourceName;
    double rem = std::chrono::duration<double>(
        m_playlistStepExpiry - std::chrono::steady_clock::now()).count();
    s.secondsLeft = rem < 0.0 ? 0.0 : rem;
    return s;
}

// ── Reorder ───────────────────────────────────────────────────────────────────

void DskManager::reorderItem(const std::string &sourceName, int newIndex)
{
    obs_scene_t *scene = dskScene();
    if (!scene) return;

    // Snapshot current item order
    std::vector<obs_sceneitem_t *> items;
    obs_scene_enum_items(scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
            static_cast<std::vector<obs_sceneitem_t *> *>(param)->push_back(item);
            return true;
        },
        &items);

    // Find the item being moved
    int fromIdx = -1;
    for (int i = 0; i < (int)items.size(); i++) {
        obs_source_t *src = obs_sceneitem_get_source(items[i]);
        if (src && obs_source_get_name(src) == sourceName) {
            fromIdx = i;
            break;
        }
    }
    if (fromIdx < 0) return;

    newIndex = std::max(0, std::min(newIndex, (int)items.size() - 1));
    if (fromIdx == newIndex) return;

    // Move within the vector and apply to OBS
    obs_sceneitem_t *moved = items[fromIdx];
    items.erase(items.begin() + fromIdx);
    items.insert(items.begin() + newIndex, moved);

    obs_scene_reorder_items(scene, items.data(), items.size());

    if (m_refreshCb) m_refreshCb();
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

// ── Staging scene ─────────────────────────────────────────────────────────────

static void copyTransform(obs_sceneitem_t *from, obs_sceneitem_t *to)
{
    struct obs_transform_info info;
    obs_sceneitem_get_info2(from, &info);
    obs_sceneitem_set_info2(to, &info);

    struct obs_sceneitem_crop crop;
    obs_sceneitem_get_crop(from, &crop);
    obs_sceneitem_set_crop(to, &crop);
}

void DskManager::buildStagingScene()
{
    std::string stagingName = stagingSceneName();

    obs_source_t *stagingSrc = obs_get_source_by_name(stagingName.c_str());
    obs_scene_t  *stagingScene = nullptr;
    if (stagingSrc) {
        stagingScene = obs_scene_from_source(stagingSrc);
    } else {
        stagingScene = obs_scene_create(stagingName.c_str());
        if (!stagingScene) return;
        stagingSrc = obs_source_get_ref(obs_scene_get_source(stagingScene));
    }

    // Nest the current program scene as a background reference, bottom-most,
    // added once. Never touched again so it doesn't fight with manual reorders.
    obs_source_t *programSrc = obs_frontend_get_current_scene();
    if (programSrc) {
        const char *programName = obs_source_get_name(programSrc);
        if (programName && strcmp(programName, stagingName.c_str()) != 0 &&
            !obs_scene_find_source(stagingScene, programName)) {
            obs_sceneitem_t *bg = obs_scene_add(stagingScene, programSrc);
            if (bg) {
                obs_sceneitem_set_order(bg, OBS_ORDER_MOVE_BOTTOM);
                // Locked so it can't be dragged/selected by accident while
                // editing the DSK items on top of it.
                obs_sceneitem_set_locked(bg, true);
            }
        }
        obs_source_release(programSrc);
    }

    // Stage any live DSK item not already present, seeded at its live transform.
    obs_scene_t *live = dskScene();
    if (live) {
        for (const auto &it : currentItems()) {
            if (obs_scene_find_source(stagingScene, it.sourceName.c_str())) continue;

            obs_source_t *itemSrc = obs_get_source_by_name(it.sourceName.c_str());
            if (!itemSrc) continue;

            obs_sceneitem_t *liveItem   = obs_scene_find_source(live, it.sourceName.c_str());
            obs_sceneitem_t *stagedItem = obs_scene_add(stagingScene, itemSrc);
            if (stagedItem && liveItem) copyTransform(liveItem, stagedItem);

            obs_source_release(itemSrc);
        }
    }

    // Drop the operator straight into an editable canvas: Studio Mode's Preview
    // pane, showing the staging scene, ready to drag/resize with OBS's own
    // native item handles. No manual scene switching required.
    obs_frontend_set_preview_program_mode(true);
    obs_frontend_set_current_preview_scene(stagingSrc);

    obs_source_release(stagingSrc);
    blog(LOG_INFO, "[dsk] Built staging scene '%s'", stagingName.c_str());
}

void DskManager::pushStagingToLive()
{
    obs_source_t *stagingSrc = obs_get_source_by_name(stagingSceneName().c_str());
    if (!stagingSrc) return;

    obs_scene_t *staging = obs_scene_from_source(stagingSrc);
    obs_scene_t *live    = dskScene();

    int pushed = 0;
    if (staging && live) {
        for (const auto &it : currentItems()) {
            obs_sceneitem_t *stagedItem = obs_scene_find_source(staging, it.sourceName.c_str());
            obs_sceneitem_t *liveItem   = obs_scene_find_source(live, it.sourceName.c_str());
            if (stagedItem && liveItem) {
                copyTransform(stagedItem, liveItem);
                pushed++;
            }
        }
    }

    obs_source_release(stagingSrc);

    // Back to normal single-view output now that the edit is applied.
    obs_frontend_set_preview_program_mode(false);

    blog(LOG_INFO, "[dsk] Pushed %d staged position(s) to live", pushed);
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

void DskManager::cbSourceRename(void *data, calldata_t *cd)
{
    auto       *mgr  = static_cast<DskManager *>(data);
    const char *prev = calldata_string(cd, "prev_name");
    const char *next = calldata_string(cd, "new_name");
    if (!prev || !next) return;

    std::string prevStr(prev), nextStr(next);

    // Re-key every name-keyed map so settings survive the rename
    auto it = mgr->m_transitions.find(prevStr);
    if (it != mgr->m_transitions.end()) {
        mgr->m_transitions[nextStr] = std::move(it->second);
        mgr->m_transitions.erase(it);
    }

    auto seqIt = mgr->m_timerSeq.find(prevStr);
    if (seqIt != mgr->m_timerSeq.end()) {
        mgr->m_timerSeq[nextStr] = seqIt->second;
        mgr->m_timerSeq.erase(seqIt);
    }

    auto expIt = mgr->m_expiryTime.find(prevStr);
    if (expIt != mgr->m_expiryTime.end()) {
        mgr->m_expiryTime[nextStr] = expIt->second;
        mgr->m_expiryTime.erase(expIt);
    }

    // Rebuild hotkeys with the new name and refresh the dock
    mgr->unregisterAllHotkeys();
    mgr->registerHotkeys();

    if (mgr->m_refreshCb) mgr->m_refreshCb();
}

// ── Persistence ───────────────────────────────────────────────────────────────

// Returns a sanitised file path for the current scene collection's settings.
// Each collection gets its own file so switching collections loads the right DSK config.
std::string DskManager::collectionConfigPath() const
{
    char *col = obs_frontend_get_current_scene_collection();
    std::string name = col ? col : "default";
    bfree(col);

    // Replace anything that isn't alphanumeric, dash, or dot with underscore
    for (char &c : name)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '.')
            c = '_';

    std::string filename = "collection-" + name + ".json";
    char *path = obs_module_config_path(filename.c_str());
    std::string result = path;
    bfree(path);
    return result;
}

// Loads DSK scene name + per-item transition/color configs for the active collection.
// Falls back to the legacy settings.json if no collection file exists yet (migration).
void DskManager::loadCollectionSettings()
{
    std::string colPath = collectionConfigPath();
    obs_data_t *root    = obs_data_create_from_json_file(colPath.c_str());

    // First-run migration: fall back to the old monolithic settings.json
    if (!root) {
        char *legacy = obs_module_config_path("settings.json");
        root = obs_data_create_from_json_file(legacy);
        bfree(legacy);
    }

    if (root) {
        const char *scene = obs_data_get_string(root, "dsk_scene");
        if (scene && *scene) m_sceneName = scene;

        obs_data_array_t *items = obs_data_get_array(root, "item_transitions");
        if (items) {
            size_t count = obs_data_array_count(items);
            for (size_t i = 0; i < count; i++) {
                obs_data_t *entry = obs_data_array_item(items, i);
                const char *name  = obs_data_get_string(entry, "source");
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
                    const char *bc = obs_data_get_string(entry, "button_color");
                    if (bc) cfg.buttonColor = bc;
                    m_transitions[name] = cfg;
                }
                obs_data_release(entry);
            }
            obs_data_array_release(items);
        }
        obs_data_array_t *playlist = obs_data_get_array(root, "sponsor_playlist");
        if (playlist) {
            size_t count = obs_data_array_count(playlist);
            for (size_t i = 0; i < count; i++) {
                obs_data_t *e = obs_data_array_item(playlist, i);
                const char *src = obs_data_get_string(e, "source");
                if (src && *src) {
                    PlaylistEntry pe;
                    pe.sourceName  = src;
                    pe.onDuration  = (uint32_t)obs_data_get_int(e, "on_dur");
                    pe.offDuration = (uint32_t)obs_data_get_int(e, "off_dur");
                    if (pe.onDuration  == 0) pe.onDuration  = 30;
                    if (pe.offDuration == 0) pe.offDuration = 10;
                    m_playlist.push_back(pe);
                }
                obs_data_release(e);
            }
            obs_data_array_release(playlist);
        }

        obs_data_release(root);
    }

    // Connect to the DSK scene for this collection and register hotkeys
    obs_source_t *src = obs_get_source_by_name(m_sceneName.c_str());
    if (src) {
        connectSceneSignals(src);
        obs_source_release(src);
    }
    registerHotkeys();

    // Re-apply saved transitions to any items already in the scene
    obs_scene_t *scene = dskScene();
    if (scene) {
        obs_scene_enum_items(scene,
            [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
                auto *mgr = static_cast<DskManager *>(param);
                obs_source_t *s = obs_sceneitem_get_source(item);
                if (s) { const char *n = obs_source_get_name(s); if (n) mgr->applyTransitions(n); }
                return true;
            }, this);
    }
}

// Saves DSK scene name + per-item configs for the active collection.
void DskManager::saveCollectionSettings()
{
    char *dir = obs_module_config_path("");
    os_mkdirs(dir);
    bfree(dir);

    std::string colPath = collectionConfigPath();

    obs_data_t *root = obs_data_create();
    obs_data_set_string(root, "dsk_scene", m_sceneName.c_str());

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
        obs_data_set_string(entry, "button_color",  cfg.buttonColor.c_str());
        obs_data_array_push_back(items, entry);
        obs_data_release(entry);
    }
    obs_data_set_array(root, "item_transitions", items);
    obs_data_array_release(items);

    obs_data_array_t *playlist = obs_data_array_create();
    for (const auto &pe : m_playlist) {
        obs_data_t *e = obs_data_create();
        obs_data_set_string(e, "source",  pe.sourceName.c_str());
        obs_data_set_int(e,    "on_dur",  pe.onDuration);
        obs_data_set_int(e,    "off_dur", pe.offDuration);
        obs_data_array_push_back(playlist, e);
        obs_data_release(e);
    }
    obs_data_set_array(root, "sponsor_playlist", playlist);
    obs_data_array_release(playlist);

    obs_data_save_json_safe(root, colPath.c_str(), "tmp", "bak");
    obs_data_release(root);
}

void DskManager::cbFrontendEvent(enum obs_frontend_event event, void *data)
{
    auto *mgr = static_cast<DskManager *>(data);
    if (mgr->m_shutdown) return;

    if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING) {
        // Save the outgoing collection before OBS unloads it
        mgr->stopPlaylist();
        mgr->saveCollectionSettings();

        // Disconnect from the scene that's about to disappear
        obs_source_t *src = obs_get_source_by_name(mgr->m_sceneName.c_str());
        if (src) { mgr->disconnectSceneSignals(src); obs_source_release(src); }
        mgr->unregisterAllHotkeys();

        // Clear per-collection state ready for the incoming collection
        mgr->m_transitions.clear();
        mgr->m_timerSeq.clear();
        mgr->m_expiryTime.clear();
        mgr->m_playlist.clear();
        mgr->m_sceneName = "[DSK Layer]";

    } else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
        // Load settings for the newly active collection
        mgr->loadCollectionSettings();
        if (mgr->m_refreshCb) mgr->m_refreshCb();
    }
}

void DskManager::loadSettings()
{
    // ── Global preferences (shared across all collections) ────────────────────
    char *path = obs_module_config_path("settings.json");
    obs_data_t *root = obs_data_create_from_json_file(path);
    bfree(path);

    if (root) {
        m_httpPort = (int)obs_data_get_int(root, "http_port");
        if (m_httpPort <= 0 || m_httpPort > 65535) m_httpPort = 4488;

        const char *ds = obs_data_get_string(root, "dock_state");
        if (ds) m_dockState = ds;

        m_viewMode = (int)obs_data_get_int(root, "view_mode");

        int gc = (int)obs_data_get_int(root, "grid_columns");
        if (gc >= 2 && gc <= 8) m_gridColumns = gc;

        obs_data_release(root);
    }

    // ── Per-collection settings for the currently active collection ───────────
    loadCollectionSettings();

    // Global rename hook
    signal_handler_connect(obs_get_signal_handler(), "source_rename",
                           cbSourceRename, this);

    // Scene-collection change hook
    obs_frontend_add_event_callback(cbFrontendEvent, this);
}

void DskManager::saveSettings()
{
    char *dir = obs_module_config_path("");
    os_mkdirs(dir);
    bfree(dir);

    // ── Global preferences ────────────────────────────────────────────────────
    obs_data_t *root = obs_data_create();
    obs_data_set_int(root, "http_port",    m_httpPort);
    obs_data_set_string(root, "dock_state",   m_dockState.c_str());
    obs_data_set_int(root,    "view_mode",    m_viewMode);
    obs_data_set_int(root,    "grid_columns", m_gridColumns);

    char *gpath = obs_module_config_path("settings.json");
    obs_data_save_json_safe(root, gpath, "tmp", "bak");
    bfree(gpath);
    obs_data_release(root);

    // ── Per-collection settings ───────────────────────────────────────────────
    saveCollectionSettings();
}
