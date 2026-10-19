#include "windowdetector.h"

#if defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
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

    Atom netClientListStacking = XInternAtom(disp, "_NET_CLIENT_LIST_STACKING", True);
    Atom netClientList = XInternAtom(disp, "_NET_CLIENT_LIST", True);
    Atom netWmState = XInternAtom(disp, "_NET_WM_STATE", True);
    Atom netWmStateHidden = XInternAtom(disp, "_NET_WM_STATE_HIDDEN", True);
    Atom netWmWindowType = XInternAtom(disp, "_NET_WM_WINDOW_TYPE", True);
    Atom netWmWindowTypeDesktop = XInternAtom(disp, "_NET_WM_WINDOW_TYPE_DESKTOP", True);
    Atom netFrameExtents = XInternAtom(disp, "_NET_FRAME_EXTENTS", True);
    Atom netWmName = XInternAtom(disp, "_NET_WM_NAME", True);
    Atom utf8String = XInternAtom(disp, "UTF8_STRING", True);

    Atom listAtom = (netClientListStacking != None) ? netClientListStacking : netClientList;
    if (listAtom == None) {
        XCloseDisplay(disp);
        return false;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long nItems = 0;
    unsigned long bytesAfter = 0;
    unsigned char* propData = nullptr;

    int status = XGetWindowProperty(disp, root, listAtom, 0, 4096, False,
                                    XA_WINDOW, &actualType, &actualFormat,
                                    &nItems, &bytesAfter, &propData);

    if (status != Success || !propData || nItems == 0) {
        if (propData) XFree(propData);
        XCloseDisplay(disp);
        return false;
    }

    Window* windows = reinterpret_cast<Window*>(propData);
    bool found = false;

    // Search from top of stacking order down to bottom
    for (long i = static_cast<long>(nItems) - 1; i >= 0; --i) {
        Window win = windows[i];
        if (win == None || win == root) {
            continue;
        }

        // 1. Check window attributes & viewability
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(disp, win, &attrs)) {
            continue;
        }
        if (attrs.map_state != IsViewable) {
            continue;
        }

        // 2. Check if window is minimized/hidden
        if (netWmState != None && netWmStateHidden != None) {
            Atom stateType;
            int stateFormat;
            unsigned long stateItems = 0, stateBytesAfter = 0;
            unsigned char* stateData = nullptr;
            if (XGetWindowProperty(disp, win, netWmState, 0, 64, False,
                                   XA_ATOM, &stateType, &stateFormat,
                                   &stateItems, &stateBytesAfter, &stateData) == Success) {
                if (stateData) {
                    Atom* states = reinterpret_cast<Atom*>(stateData);
                    bool isHidden = false;
                    for (unsigned long s = 0; s < stateItems; ++s) {
                        if (states[s] == netWmStateHidden) {
                            isHidden = true;
                            break;
                        }
                    }
                    XFree(stateData);
                    if (isHidden) {
                        continue;
                    }
                }
            }
        }

        // 3. Check window type (skip desktop wallpaper window)
        if (netWmWindowType != None && netWmWindowTypeDesktop != None) {
            Atom typeType;
            int typeFormat;
            unsigned long typeItems = 0, typeBytesAfter = 0;
            unsigned char* typeData = nullptr;
            if (XGetWindowProperty(disp, win, netWmWindowType, 0, 16, False,
                                   XA_ATOM, &typeType, &typeFormat,
                                   &typeItems, &typeBytesAfter, &typeData) == Success) {
                if (typeData) {
                    Atom* types = reinterpret_cast<Atom*>(typeData);
                    bool isDesktop = false;
                    for (unsigned long t = 0; t < typeItems; ++t) {
                        if (types[t] == netWmWindowTypeDesktop) {
                            isDesktop = true;
                            break;
                        }
                    }
                    XFree(typeData);
                    if (isDesktop) {
                        continue;
                    }
                }
            }
        }

        // 4. Check class (skip flameshot's own capture window & desktop managers)
        XClassHint ch;
        QString resClass, resName;
        if (XGetClassHint(disp, win, &ch)) {
            if (ch.res_class) {
                resClass = QString::fromUtf8(ch.res_class);
                XFree(ch.res_class);
            }
            if (ch.res_name) {
                resName = QString::fromUtf8(ch.res_name);
                XFree(ch.res_name);
            }
        }

        if (resClass.contains(QStringLiteral("flameshot"), Qt::CaseInsensitive) ||
            resName.contains(QStringLiteral("flameshot"), Qt::CaseInsensitive) ||
            resClass.contains(QStringLiteral("xfdesktop"), Qt::CaseInsensitive)) {
            continue;
        }

        // 5. Calculate window frame geometry
        int winX = 0, winY = 0;
        Window junk;
        XTranslateCoordinates(disp, win, root, 0, 0, &winX, &winY, &junk);

        int left = 0, right = 0, top = 0, bottom = 0;
        if (netFrameExtents != None) {
            Atom extType;
            int extFormat;
            unsigned long extItems = 0, extBytesAfter = 0;
            unsigned char* extData = nullptr;
            if (XGetWindowProperty(disp, win, netFrameExtents, 0, 4, False,
                                   XA_CARDINAL, &extType, &extFormat,
                                   &extItems, &extBytesAfter, &extData) == Success) {
                if (extData && extItems == 4) {
                    unsigned long* ext = reinterpret_cast<unsigned long*>(extData);
                    left = static_cast<int>(ext[0]);
                    right = static_cast<int>(ext[1]);
                    top = static_cast<int>(ext[2]);
                    bottom = static_cast<int>(ext[3]);
                }
                if (extData) XFree(extData);
            }
        }

        int finalX = winX - left;
        int finalY = winY - top;
        int finalW = attrs.width + left + right;
        int finalH = attrs.height + top + bottom;

        // Skip tiny or zero-sized helper windows
        if (finalW < 30 || finalH < 30) {
            continue;
        }

        QRect geom(finalX, finalY, finalW, finalH);

        // 6. Check if cursor position is within this window
        if (geom.contains(globalPos)) {
            outGeometry = geom;
            outAppClass = resClass;

            // Fetch window title via _NET_WM_NAME (UTF-8) or standard WM_NAME
            outTitle.clear();
            if (netWmName != None && utf8String != None) {
                Atom nameType;
                int nameFormat;
                unsigned long nameItems = 0, nameBytesAfter = 0;
                unsigned char* nameData = nullptr;
                if (XGetWindowProperty(disp, win, netWmName, 0, 256, False,
                                       utf8String, &nameType, &nameFormat,
                                       &nameItems, &nameBytesAfter, &nameData) == Success) {
                    if (nameData) {
                        outTitle = QString::fromUtf8(reinterpret_cast<char*>(nameData));
                        XFree(nameData);
                    }
                }
            }
            if (outTitle.isEmpty()) {
                XTextProperty prop;
                if (XGetWMName(disp, win, &prop) && prop.value) {
                    outTitle = QString::fromUtf8(reinterpret_cast<char*>(prop.value));
                    XFree(prop.value);
                }
            }

            found = true;
            break;
        }
    }

    XFree(propData);
    XCloseDisplay(disp);
    return found;
#else
    Q_UNUSED(globalPos);
    Q_UNUSED(outGeometry);
    Q_UNUSED(outTitle);
    Q_UNUSED(outAppClass);
    return false;
#endif
}
