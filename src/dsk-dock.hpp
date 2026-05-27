#pragma once

#include "dsk-timer-button.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QString>
#include <string>
#include <vector>

class DskDock : public QWidget {
    Q_OBJECT

public:
    explicit DskDock(QWidget *parent = nullptr);

    // Safe to call from any thread — queued to main thread internally
    void scheduleRefresh();

private slots:
    void refresh();
    void onToggleClicked(const QString &sourceName);
    void onTransitionClicked(const QString &sourceName);
    void onSettingsClicked();
    void onStateChanged(const QString &sourceName, bool active);

private:
    void buildUI();
    void buildItemRow(QWidget *container, QVBoxLayout *layout,
                      const std::string &sourceName, bool active);

    DskTimerButton *findTimerButton(const QString &sourceName) const;

    QLabel       *m_sceneLabel    = nullptr;
    QScrollArea  *m_scroll        = nullptr;
    QWidget      *m_itemContainer = nullptr;
};
