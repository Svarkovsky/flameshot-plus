add_subdirectory(panel)
add_subdirectory(capture)

# Required to generate MOC
target_sources(
        flameshot
        PRIVATE
        infowindow.ui
        capturelauncher.ui
        uploadhistory.ui
        uploadlineitem.ui

        capturelauncher.h
        draggablewidgetmaker.h
        imagelabel.h
        trayicon.h
        infowindow.h
        loadspinner.h
        notificationwidget.h
        orientablepushbutton.h
        uploadhistory.h
        uploadlineitem.h
        updatenotificationwidget.h
        colorpickerwidget.h
        imguploaddialog.h
        capture/capturetoolobjects.h
        record/recordwidget.h
        record/recordcanvaswidget.h
        record/audiovumeterwidget.h
        record/recordpreviewdialog.h
        record/blocktrimslider.h
)

target_sources(
        flameshot
        PRIVATE capturelauncher.cpp
        draggablewidgetmaker.cpp
        imagelabel.cpp
        trayicon.cpp
        infowindow.cpp
        loadspinner.cpp
        notificationwidget.cpp
        orientablepushbutton.cpp
        uploadhistory.cpp
        uploadlineitem.cpp
        updatenotificationwidget.cpp
        colorpickerwidget.cpp
        imguploaddialog.cpp
        capture/capturetoolobjects.cpp
        record/recordwidget.cpp
        record/recordcanvaswidget.cpp
        record/audiovumeterwidget.cpp
        record/recordpreviewdialog.cpp
        record/blocktrimslider.cpp
)
