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
    void onAccept();
    void onSelectionChanged();
private:
    void buildUI();
    void populateList();
    void updateSourceCombo();
    void updateRemoveButton();

    QListWidget  *m_list        = nullptr;
    QComboBox    *m_sourceCombo = nullptr;
    QSpinBox     *m_onSpin      = nullptr;
    QSpinBox     *m_offSpin     = nullptr;
    QPushButton  *m_addBtn      = nullptr;
    QPushButton  *m_removeBtn   = nullptr;
    std::vector<PlaylistEntry> m_entries;
};
