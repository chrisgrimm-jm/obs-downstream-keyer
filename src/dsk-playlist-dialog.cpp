#include "dsk-playlist-dialog.hpp"
#include "dsk-manager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
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
    setWindowTitle("Sponsor Loop");
    setMinimumWidth(420);
    buildUI();
}

void DskPlaylistDialog::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    auto *hint = new QLabel(
        QString("Sources inside the \"%1\" group rotate automatically, in that group's "
                "order. Add or remove sponsors by editing that group in OBS's Sources "
                "panel — select an entry below to set its on-air/gap duration.")
            .arg(QString::fromStdString(DskManager::sponsorLoopGroupName())));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #888; font-size: 10px;");
    root->addWidget(hint);

    m_list = new QListWidget();
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMinimumHeight(160);
    root->addWidget(m_list);

    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &DskPlaylistDialog::onSelectionChanged);

    auto *form = new QHBoxLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(6);

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

    m_setDurationBtn = new QPushButton("Set Duration");
    m_setDurationBtn->setStyleSheet(kDlgBtn);
    m_setDurationBtn->setEnabled(false);
    connect(m_setDurationBtn, &QPushButton::clicked, this, &DskPlaylistDialog::onSetDuration);
    form->addWidget(m_setDurationBtn);

    root->addLayout(form);

    m_playNowBtn = new QPushButton("\xe2\x96\xb6 Play Now");
    m_playNowBtn->setStyleSheet(kDlgBtn);
    m_playNowBtn->setEnabled(false);
    m_playNowBtn->setToolTip("Punch the selected sponsor to air immediately; the loop resumes from here afterward");
    connect(m_playNowBtn, &QPushButton::clicked, this, &DskPlaylistDialog::onPlayNow);
    root->addWidget(m_playNowBtn);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(btns);

    populateList();
}

void DskPlaylistDialog::populateList()
{
    QString selectedName;
    if (m_list->currentRow() >= 0)
        selectedName = m_list->currentItem()->data(Qt::UserRole).toString();

    m_list->clear();
    for (const auto &e : DskManager::instance().currentLoopEntries()) {
        auto *item = new QListWidgetItem(
            QString("%1    ON: %2s   GAP: %3s")
                .arg(QString::fromStdString(e.sourceName))
                .arg(e.onDuration)
                .arg(e.offDuration));
        item->setData(Qt::UserRole,     QString::fromStdString(e.sourceName));
        item->setData(Qt::UserRole + 1, (int)e.onDuration);
        item->setData(Qt::UserRole + 2, (int)e.offDuration);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        m_list->addItem(item);

        if (!selectedName.isEmpty() && selectedName == QString::fromStdString(e.sourceName))
            m_list->setCurrentItem(item);
    }
    updateButtons();
}

void DskPlaylistDialog::updateButtons()
{
    bool hasSelection = m_list->currentRow() >= 0;
    m_setDurationBtn->setEnabled(hasSelection);
    m_playNowBtn->setEnabled(hasSelection);
}

void DskPlaylistDialog::onSelectionChanged()
{
    updateButtons();
    QListWidgetItem *item = m_list->currentItem();
    if (!item) return;
    m_onSpin->setValue(item->data(Qt::UserRole + 1).toInt());
    m_offSpin->setValue(item->data(Qt::UserRole + 2).toInt());
}

void DskPlaylistDialog::onSetDuration()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item) return;
    std::string name = item->data(Qt::UserRole).toString().toStdString();

    DskManager::instance().setLoopDuration(name, (uint32_t)m_onSpin->value(), (uint32_t)m_offSpin->value());
    DskManager::instance().saveSettings();
    populateList();
}

void DskPlaylistDialog::onPlayNow()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item) return;
    DskManager::instance().playNow(item->data(Qt::UserRole).toString().toStdString());
}
