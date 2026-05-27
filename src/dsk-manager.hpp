#pragma once

#include <obs-module.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include <chrono>

// Per-source show/hide transition config.
// When set, the manager applies these to the scene item so that toggling
// visibility uses the configured transition (including obs-move).
struct DskTransitionConfig {
    std::string showType;       // e.g. "fade_transition", "move_transition"
    uint32_t    showDuration = 300;
    std::string showSettings;  // JSON blob of transition source settings
    std::string hideType;
    uint32_t    hideDuration = 300;
    std::string hideSettings;  // JSON blob of transition source settings
    uint32_t    autoDuration = 0; // seconds before auto-hide (0 = disabled)
    std::string buttonColor;   // hex color e.g. "#27ae60", empty = default green
};

class DskManager {
public:
    static DskManager &instance();

    void loadSettings();
    void saveSettings();

    // Disconnect from OBS and unregister hotkeys.
    // Must be called while OBS core is still alive (OBS_FRONTEND_EVENT_EXIT).
    // Safe to call more than once.
    void shutdown();

    // Which scene to use as the DSK source scene
    const std::string &sceneName() const { return m_sceneName; }
    void setSceneName(const std::string &name);

    int  httpPort() const   { return m_httpPort; }
    void setHttpPort(int p) { m_httpPort = p; }

    const std::string &dockState() const          { return m_dockState; }
    void setDockState(const std::string &state)   { m_dockState = state; }

    int  viewMode() const        { return m_viewMode; }
    void setViewMode(int mode)   { m_viewMode = mode; }

    // ── Item enumeration ──────────────────────────────────────────────────────
    struct ItemInfo {
        std::string sourceName;
        bool        visible;
    };
    // Returns all items currently in the DSK scene (snapshot, safe to call from UI thread)
    std::vector<ItemInfo> currentItems() const;

    // ── Item control ──────────────────────────────────────────────────────────
    void activate(const std::string &sourceName);
    void deactivate(const std::string &sourceName);
    void toggle(const std::string &sourceName);
    bool isActive(const std::string &sourceName) const;

    // ── Per-item transition config ────────────────────────────────────────────
    const DskTransitionConfig *transitionConfig(const std::string &sourceName) const;
    void setTransitionConfig(const std::string &sourceName, const DskTransitionConfig &cfg);

    // Apply stored transition config to a live scene item (called on load + item add)
    void applyTransitions(const std::string &sourceName);

    // Per-item button color (hex string, e.g. "#e74c3c"). Empty string = default.
    void setButtonColor(const std::string &sourceName, const std::string &colorHex);

    // Seconds remaining until auto-hide fires. Returns -1 if no countdown is active.
    double timeRemaining(const std::string &sourceName) const;

    // ── Setup helper ──────────────────────────────────────────────────────────
    // Nests the DSK scene at the top of every other scene in the collection
    void addDskToAllScenes();

    // ── UI callbacks ──────────────────────────────────────────────────────────
    // Fires (queued to Qt main thread by callers) when items change or state changes
    using RefreshCallback = std::function<void()>;
    using StateCallback   = std::function<void(const std::string &name, bool active)>;
    void setRefreshCallback(RefreshCallback cb) { m_refreshCb = std::move(cb); }
    void setStateCallback(StateCallback cb)     { m_stateCb   = std::move(cb); }

private:
    DskManager();
    ~DskManager();

    // Returns the DSK scene (does NOT addref — caller must not release)
    obs_scene_t *dskScene() const;
    // Returns the scene item for a named source inside the DSK scene (no addref)
    obs_sceneitem_t *findItem(const std::string &sourceName) const;

    void registerHotkeys();
    void unregisterAllHotkeys();
    void connectSceneSignals(obs_source_t *sceneSource);
    void disconnectSceneSignals(obs_source_t *sceneSource);

    static void cbItemAdd(void *data, calldata_t *cd);
    static void cbItemRemove(void *data, calldata_t *cd);
    static void cbHotkeyToggle(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed);

    struct HotkeyCtx {
        DskManager *mgr;
        std::string name;
    };
    struct HotkeyEntry {
        obs_hotkey_id id;
        std::string   sourceName;
        HotkeyCtx    *ctx = nullptr;
    };

    std::string m_sceneName = "[DSK Layer]";
    int         m_httpPort  = 4488;

    std::string m_dockState;
    int         m_viewMode = 0; // 0 = list, 1 = grid

    // Per-source auto-hide timer sequence numbers (incremented to cancel pending timers)
    std::unordered_map<std::string, uint64_t> m_timerSeq;
    // Expiry time points for active auto-hide countdowns (used by timeRemaining())
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_expiryTime;

    bool m_shutdown = false;

    std::unordered_map<std::string, DskTransitionConfig> m_transitions;
    std::vector<HotkeyEntry>                             m_hotkeys;

    RefreshCallback m_refreshCb;
    StateCallback   m_stateCb;
};
