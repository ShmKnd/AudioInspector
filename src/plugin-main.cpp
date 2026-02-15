/*
AudioInspector - robust plugin-main (fixed signature usage)

This version avoids calling obs_frontend_add_dock_by_id with an incorrect
signature that caused crashes. It prefers the (id, title, widget) form
matching MasterLevelMeter, falls back to obs_frontend_add_dock if needed,
and silently skips if no dock API is available.
*/
#include <obs-module.h>
#include <dlfcn.h>
#include <QWidget>

#include "audio_inspector_widget.h"
#include "audio_inspector_core.h"

OBS_DECLARE_MODULE();
OBS_MODULE_USE_DEFAULT_LOCALE("AudioInspector", "en-US")

const char *obs_module_name(void)
{
    return "AudioInspector";
}

const char *obs_module_description(void)
{
    return "OBS Audio Inspector - visualizes audio sources and global devices.";
}

static AudioInspectorWidget *g_widget = nullptr;
static bool g_dock_added = false;
static const char *DOCK_ID = "audio_inspector";

/* Typedefs for API variants we will actually call.
   We intentionally prefer the (id, title, widget) variant used by MasterLevelMeter.
*/
using get_main_window_t = void *(*)(void);
using add_dock_by_id_title_t = void (*)(const char *, const char *, void *);
using add_dock_old_t = void (*)(const char *, void *);
using remove_dock_by_id_t = void (*)(const char *);
using remove_dock_old_t = void (*)(void *);

static void try_add_dock(QWidget *widget)
{
    void *handle = RTLD_DEFAULT;

    /* Prefer the (id, title, widget) signature (MasterLevelMeter style). */
    add_dock_by_id_title_t add_title =
        (add_dock_by_id_title_t)dlsym(handle, "obs_frontend_add_dock_by_id");
    if (add_title) {
        // Safe call: id, human title, widget pointer
        add_title(DOCK_ID, "Audio Inspector", widget);
        g_dock_added = true;
        return;
    }

    /* Fall back to older obs_frontend_add_dock(name, widget) */
    add_dock_old_t add_old = (add_dock_old_t)dlsym(handle, "obs_frontend_add_dock");
    if (add_old) {
        add_old("Audio Inspector", widget);
        g_dock_added = true;
        return;
    }

    /* No dock API available - keep widget parented to main window and optionally provide a Tools menu entry */
}

static void try_remove_dock()
{
    void *handle = RTLD_DEFAULT;

    remove_dock_by_id_t rem_by_id = (remove_dock_by_id_t)dlsym(handle, "obs_frontend_remove_dock_by_id");
    if (rem_by_id) {
        rem_by_id(DOCK_ID);
        return;
    }

    remove_dock_old_t rem_old = (remove_dock_old_t)dlsym(handle, "obs_frontend_remove_dock");
    if (rem_old) {
        rem_old(g_widget);
        return;
    }
}

bool obs_module_load(void)
{
    /* Try to get OBS main window via frontend API if available */
    QWidget *main_win = nullptr;
    void *handle = RTLD_DEFAULT;
    get_main_window_t get_main_window = (get_main_window_t)dlsym(handle, "obs_frontend_get_main_window");
    if (get_main_window) {
        main_win = (QWidget *)get_main_window();
    }

    /* Create our widget parented to OBS main window (or nullptr) */
    g_widget = new AudioInspectorWidget(main_win);

    /* Try to register as dock (runtime-resolved API) */
    try_add_dock(g_widget);

    /* If desired, add a Tools menu item fallback via obs_frontend_add_tools_menu_item (dlsym) so user can open widget
       even if dock registration was not possible. This is optional; implement if you want guaranteed access. */

    blog(LOG_INFO, "AudioInspector: module loaded");
    return true;
}

void obs_module_unload(void)
{
    if (g_dock_added) {
        try_remove_dock();
        g_dock_added = false;
        // OBS側がドックを追加した場合はウィジェットの所有権も取得しているため、
        // ここでdeleteしない（OBSが解放する）
        g_widget = nullptr;
    } else if (g_widget) {
        // ドックに追加されなかった場合のみ手動で解放
        delete g_widget;
        g_widget = nullptr;
    }

    blog(LOG_INFO, "AudioInspector: module unloaded");
}
