#include "windowdetector.h"

#if defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#endif

#if defined(Q_OS_LINUX)
static Window getClientWindow(Display* disp, Window win)
{
    Atom stateAtom = XInternAtom(disp, "WM_STATE", True);
    if (stateAtom != None) {
        Atom type;
        int format;
        unsigned long nitems, bytes_after;
        unsigned char* prop = nullptr;
        if (XGetWindowProperty(disp, win, stateAtom, 0, 0, False, AnyPropertyType,
                               &type, &format, &nitems, &bytes_after, &prop) == Success) {
            if (prop) {
                XFree(prop);
                return win;
            }
        }
    }

    Window root, parent;
    Window* children = nullptr;
    unsigned int nchildren = 0;
    if (XQueryTree(disp, win, &root, &parent, &children, &nchildren) != 0 && children) {
        for (int i = (int)nchildren - 1; i >= 0; --i) {
            Window client = getClientWindow(disp, children[i]);
            if (client != None) {
                XFree(children);
                return client;
            }
        }
        XFree(children);
    }
    return None;
}
#endif

bool WindowDetector::detectWindowAt(const QPoint& globalPos,
                                    QRect& outGeometry,
                                    QString& outTitle,
                                    QString& outAppClass)
{
#if defined(Q_OS_LINUX)
    Display* disp = XOpenDisplay(nullptr);
    if (!disp) {
        return false;
    }

    Window root = DefaultRootWindow(disp);
    Window child = None;
    int rootX, rootY, winX, winY;
    unsigned int mask;

    // Find the immediate top window at coordinates
    if (!XQueryPointer(disp, root, &root, &child, &rootX, &rootY, &winX, &winY, &mask)) {
        XCloseDisplay(disp);
        return false;
    }

    if (child == None || child == root) {
        XCloseDisplay(disp);
        return false;
    }

    // Try finding client window
    Window client = getClientWindow(disp, child);
    Window target = (client != None) ? client : child;

    XWindowAttributes attrs;
    if (XGetWindowAttributes(disp, target, &attrs)) {
        int x = 0, y = 0;
        Window junk;
        XTranslateCoordinates(disp, target, root, 0, 0, &x, &y, &junk);

        outGeometry = QRect(x, y, attrs.width, attrs.height);

        // Fetch window name
        XTextProperty prop;
        if (XGetWMName(disp, target, &prop) && prop.value) {
            outTitle = QString::fromUtf8((char*)prop.value);
            XFree(prop.value);
        }

        // Fetch window class
        XClassHint ch;
        if (XGetClassHint(disp, target, &ch)) {
            if (ch.res_class) {
                outAppClass = QString::fromUtf8(ch.res_class);
                XFree(ch.res_class);
            }
            if (ch.res_name) {
                XFree(ch.res_name);
            }
        }

        XCloseDisplay(disp);
        return true;
    }

    XCloseDisplay(disp);
    return false;
#else
    Q_UNUSED(globalPos);
    Q_UNUSED(outGeometry);
    Q_UNUSED(outTitle);
    Q_UNUSED(outAppClass);
    return false;
#endif
}
