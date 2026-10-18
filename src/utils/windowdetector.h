#pragma once

#include <QPoint>
#include <QRect>
#include <QString>

class WindowDetector
{
public:
    static bool detectWindowAt(const QPoint& globalPos,
                               QRect& outGeometry,
                               QString& outTitle,
                               QString& outAppClass);
};
