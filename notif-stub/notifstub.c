/*
 * No-op stub for flutter_local_notifications_windows.dll.
 *
 * The real plugin calls WinRT toast-notification APIs that Wine does not
 * implement (RoGetActivationFactory fails, an unhandled C++ exception kills
 * the process).  This stub implements the same dart:ffi C API so the plugin
 * loads and every call becomes a harmless no-op: no toasts are shown, but
 * nothing crashes.
 *
 * ABI taken from upstream src/ffi_api.h
 * (MaikuB/flutter_local_notifications / flutter_local_notifications_windows).
 */
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#define E __declspec(dllexport)

typedef struct NativePlugin NativePlugin;
struct NativePlugin { int unused; };

typedef struct StringMapEntry { const char *key, *value; } StringMapEntry;
typedef struct NativeStringMap { const StringMapEntry *entries; int size; } NativeStringMap;
typedef struct NativeNotificationDetails { int id; } NativeNotificationDetails;
typedef enum { notification, action } NativeLaunchType;
typedef struct NativeLaunchDetails {
    bool didLaunch;
    NativeLaunchType launchType;
    const char *payload;
    NativeStringMap data;
} NativeLaunchDetails;
typedef void (*NativeNotificationCallback)(NativeLaunchDetails details);
typedef enum { success = 0, failed = 1, notFound = 2 } NativeUpdateResult;

E bool hasPackageIdentity(void) { return false; }

E bool isValidXml(char *xml) { return xml != NULL; }

E NativePlugin *createPlugin(void) { return calloc(1, sizeof(NativePlugin)); }

E void disposePlugin(NativePlugin *ptr) { free(ptr); }

E bool init(NativePlugin *plugin, char *appName, char *aumId, char *guid,
            char *iconPath, NativeNotificationCallback callback)
{
    (void)plugin; (void)appName; (void)aumId; (void)guid;
    (void)iconPath; (void)callback;
    fprintf(stderr, "[notifstub] init - notifications disabled (stub)\n");
    return true;
}

E bool showNotification(NativePlugin *plugin, int id, char *xml, NativeStringMap bindings)
{
    (void)plugin; (void)id; (void)xml; (void)bindings;
    return true;
}

E bool scheduleNotification(NativePlugin *plugin, int id, char *xml, int time)
{
    (void)plugin; (void)id; (void)xml; (void)time;
    return true;
}

E NativeUpdateResult updateNotification(NativePlugin *plugin, int id, NativeStringMap bindings)
{
    (void)plugin; (void)id; (void)bindings;
    return notFound;
}

E void cancelAll(NativePlugin *plugin) { (void)plugin; }

E void cancelNotification(NativePlugin *plugin, int id) { (void)plugin; (void)id; }

E NativeNotificationDetails *getActiveNotifications(NativePlugin *plugin, int *size)
{
    (void)plugin;
    *size = 0;
    return NULL;
}

E NativeNotificationDetails *getPendingNotifications(NativePlugin *plugin, int *size)
{
    (void)plugin;
    *size = 0;
    return NULL;
}

E void freeDetailsArray(NativeNotificationDetails *ptr) { free(ptr); }

E void freeLaunchDetails(NativeLaunchDetails details) { (void)details; }
