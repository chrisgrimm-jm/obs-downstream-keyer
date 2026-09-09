#pragma once
#include "dsk-manager.hpp"
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>
#include <vector>

class DskPlaylistDialog : public QDialog {
    Q_OBJECT
public:
    explicit DskPlaylistDialog(QWidget *parent = nullptr);
private slots:
    void onAdd();
    void onRemove();
    void onPlayNow();
    void onAccept();
    void onSelectionChanged();
private:
    void buildUI();
    void populateList();
    void updateSourceCombo();
    void updateRemoveButton();
    // Pushes the list widget's current entries/order to DskManager. Used by
    // both OK and Play Now, so Play Now works on unsaved edits without
    // requiring the operator to close and reopen the dialog first.
    void commitEntries();

    QListWidget  *m_list        = nullptr;
    QComboBox    *m_sourceCombo = nullptr;
    QSpinBox     *m_onSpin      = nullptr;
    QSpinBox     *m_offSpin     = nullptr;
    QPushButton  *m_addBtn      = nullptr;
    QPushButton  *m_removeBtn   = nullptr;
    QPushButton  *m_playNowBtn  = nullptr;
    std::vector<PlaylistEntry> m_entries;
};
