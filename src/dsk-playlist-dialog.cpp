#include "dsk-playlist-dialog.hpp"
#include "dsk-manager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QListWidgetItem>

static const char *kDlgBtn =
    "QPushButton {"
    "  background: #1a1a1a; color: #777;"
    "  border: 1px solid #333; border-radius: 3px;"
    "  font-size: 11px; padding: 3px 10px;"
    "}"
    "QPushButton:hover { color: #bbb; border-color: #555; }";

DskPlaylistDialog::DskPlaylistDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Sponsor Playlist");
    setMinimumWidth(460);
    m_entries = DskManager::instance().playlist();
    buildUI();
}

void DskPlaylistDialog::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto *hint = new QLabel(
        "Build a loop of sponsor sources. Each entry plays on-air for its set duration, "
        "then waits during the gap before the next sponsor fires.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #888; font-size: 10px;");
    root->addWidget(hint);

    m_list = new QListWidget();
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMinimumHeight(160);
    root->addWidget(m_list);

    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &DskPlaylistDialog::onSelectionChanged);

    // Add entry form
    auto *formWidget = new QWidget();
    auto *form = new QHBoxLayout(formWidget);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

    m_sourceCombo = new QComboBox();
    m_sourceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    form->addWidget(m_sourceCombo);

    form->addWidget(new QLabel("On-air:"));
    m_onSpin = new QSpinBox();
    m_onSpin->setRange(1, 3600);
    m_onSpin->setValue(30);
    m_onSpin->setSuffix(" sec");
    form->addWidget(m_onSpin);

    form->addWidget(new QLabel("Gap:"));
    m_offSpin = new QSpinBox();
    m_offSpin->setRange(1, 3600);
    m_offSpin->setValue(10);
    m_offSpin->setSuffix(" sec");
    form->addWidget(m_offSpin);

    m_addBtn = new QPushButton("Add");
    m_addBtn->setStyleSheet(kDlgBtn);
    connect(m_addBtn, &QPushButton::clicked, this, &DskPlaylistDialog::onAdd);
    form->addWidget(m_addBtn);

    root->addWidget(formWidget);

    m_removeBtn = new QPushButton("Remove Selected");
    m_removeBtn->setStyleSheet(kDlgBtn);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &DskPlaylistDialog::onRemove);
    root->addWidget(m_removeBtn);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &DskPlaylistDialog::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);

    populateList();
    updateSourceCombo();
}

void DskPlaylistDialog::populateList()
{
    m_list->clear();
    for (const auto &e : m_entries) {
        auto *item = new QListWidgetItem(
            QString("%1    ON: %2s   GAP: %3s")
                .arg(QString::fromStdString(e.sourceName))
                .arg(e.onDuration)
                .arg(e.offDuration));
        item->setData(Qt::UserRole,     QString::fromStdString(e.sourceName));
        item->setData(Qt::UserRole + 1, (int)e.onDuration);
        item->setData(Qt::UserRole + 2, (int)e.offDuration);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                       Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        m_list->addItem(item);
    }
}

void DskPlaylistDialog::updateSourceCombo()
{
    m_sourceCombo->clear();
    auto items = DskManager::instance().currentItems();
    for (const auto &info : items) {
        QString sname = QString::fromStdString(info.sourceName);
        bool already = false;
        for (const auto &e : m_entries)
            if (e.sourceName == info.sourceName) { already = true; break; }
        if (!already)
            m_sourceCombo->addItem(sname);
    }
    m_addBtn->setEnabled(m_sourceCombo->count() > 0);
}

void DskPlaylistDialog::updateRemoveButton()
{
    m_removeBtn->setEnabled(m_list->currentRow() >= 0);
}

void DskPlaylistDialog::onSelectionChanged()
{
    updateRemoveButton();
}

void DskPlaylistDialog::onAdd()
{
    if (m_sourceCombo->currentIndex() < 0) return;
    PlaylistEntry e;
    e.sourceName  = m_sourceCombo->currentText().toStdString();
    e.onDuration  = (uint32_t)m_onSpin->value();
    e.offDuration = (uint32_t)m_offSpin->value();
    m_entries.push_back(e);
    populateList();
    updateSourceCombo();
}

void DskPlaylistDialog::onRemove()
{
    int row = m_list->currentRow();
    if (row < 0 || row >= (int)m_entries.size()) return;
    m_entries.erase(m_entries.begin() + row);
    populateList();
    updateSourceCombo();
    updateRemoveButton();
}

void DskPlaylistDialog::onAccept()
{
    // Sync m_entries order from the list widget (user may have drag-reordered)
    std::vector<PlaylistEntry> ordered;
    for (int i = 0; i < m_list->count(); i++) {
        QListWidgetItem *item = m_list->item(i);
        PlaylistEntry e;
        e.sourceName  = item->data(Qt::UserRole).toString().toStdString();
        e.onDuration  = (uint32_t)item->data(Qt::UserRole + 1).toInt();
        e.offDuration = (uint32_t)item->data(Qt::UserRole + 2).toInt();
        ordered.push_back(e);
    }
    DskManager::instance().setPlaylist(std::move(ordered));
    DskManager::instance().saveSettings();
    accept();
}
