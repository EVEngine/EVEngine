#pragma once

// Minimal declarations from libX11-xcb-dev. Dawn loads these symbols at runtime,
// but its Linux common headers require the declarations even for headless builds.
#include <X11/Xlib.h>
#include <xcb/xcb.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    XlibOwnsEventQueue = 0,
    XCBOwnsEventQueue
} XEventQueueOwner;

xcb_connection_t *XGetXCBConnection(Display *display);
void XSetEventQueueOwner(Display *display, XEventQueueOwner owner);

#ifdef __cplusplus
}
#endif
