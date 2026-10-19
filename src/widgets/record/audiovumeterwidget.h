#pragma once

#include <QWidget>

class AudioVuMeterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AudioVuMeterWidget(QWidget* parent = nullptr);

    void setLevel(double level);
    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_level;
    bool m_muted;
};
