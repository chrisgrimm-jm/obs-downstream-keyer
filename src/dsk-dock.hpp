#pragma once

#include "dsk-timer-button.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QString>
#include <string>

enum class ViewMode { List, Grid };

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
    void onSetColorClicked(const QString &sourceName);
    void onResetColorClicked(const QString &sourceName);
    void onCopyTransitions(const QString &sourceName);
    void onPasteTransitions(const QString &sourceName);
    void onSettingsClicked();
    void onPlaylistClicked();
    void onPlaylistToggle();
    void onPlaylistTick();
    void onStateChanged(const QString &sourceName, bool active);
    void onRenameClicked(const QString &sourceName);
    void onViewToggle();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void buildUI();
    void buildListView(QVBoxLayout *layout);
    void buildGridView(QVBoxLayout *layout);
    void onGridContextMenu(const QString &sourceName, const QPoint &globalPos);

    DskTimerButton *findTimerButton(const QString &sourceName) const;
    void            updateViewToggleIcon();
    void            updateDropIndicator(const QPoint &posInContainer);
    void            hideDropIndicator();
    void            updatePlaylistBar();

    static QString fmtTime(int s) {
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }

    ViewMode     m_viewMode        = ViewMode::List;
    int          m_dropIndex       = -1;
    bool         m_pendingRefresh  = false;

    QLabel       *m_sceneLabel       = nullptr;
    QPushButton  *m_viewToggle       = nullptr;
    QScrollArea  *m_scroll           = nullptr;
    QWidget      *m_itemContainer    = nullptr;
    QWidget      *m_dropIndicator    = nullptr;
    QPushButton  *m_playlistToggleBtn = nullptr;
    QLabel       *m_playlistStatusLabel = nullptr;
    QTimer       *m_playlistTicker   = nullptr;
};
