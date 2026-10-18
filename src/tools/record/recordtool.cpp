#include "recordtool.h"

RecordTool::RecordTool(QObject* parent)
    : AbstractActionTool(parent)
{
}

bool RecordTool::closeOnButtonPressed() const
{
    return true;
}

QIcon RecordTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor);
    return QIcon(iconPath(background) + "video.svg");
}

QString RecordTool::name() const
{
    return tr("Record Video");
}

CaptureTool::Type RecordTool::type() const
{
    return CaptureTool::TYPE_RECORD;
}

QString RecordTool::description() const
{
    return tr("Record video and audio of the selected area");
}

CaptureTool* RecordTool::copy(QObject* parent)
{
    return new RecordTool(parent);
}

void RecordTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context);
    emit requestAction(REQ_RECORD_VIDEO);
    emit requestAction(REQ_CLOSE_GUI);
}
