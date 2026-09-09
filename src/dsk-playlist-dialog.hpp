#pragma once
#include "dsk-manager.hpp"
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>

class DskPlaylistDialog : public QDialog {
    Q_OBJECT
public:
    explicit DskPlaylistDialog(QWidget *parent = nullptr);
private slots:
    void onSetDuration();
    void onPlayNow();
    void onSelectionChanged();
private:
    void buildUI();
    void populateList();
    void updateButtons();

    QListWidget  *m_list           = nullptr;
    QSpinBox     *m_onSpin         = nullptr;
    QSpinBox     *m_offSpin        = nullptr;
    QPushButton  *m_setDurationBtn = nullptr;
    QPushButton  *m_playNowBtn     = nullptr;
};
