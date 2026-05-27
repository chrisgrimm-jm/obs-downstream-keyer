#include "dsk-dock.hpp"
#include "dsk-manager.hpp"
#include "dsk-item-settings.hpp"
#include "dsk-settings.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QMetaObject>
#include <QSizePolicy>
#include <QFrame>

// ── Styles ────────────────────────────────────────────────────────────────────

static const char *kActive =
    "QPushButton {"
    "  background: #27ae60; color: #fff;"
    "  border: 2px solid #1e8449; border-radius: 4px;"
    "  font-weight: bold; font-size: 12px; padding: 10px 8px;"
    "  text-align: left;"
    "}"
    "QPushButton:hover { background: #2ecc71; }";

static const char *kInactive =
    "QPushButton {"
    "  background: #2c2c2c; color: #aaa;"
    "  border: 2px solid #444; border-radius: 4px;"
    "  font-size: 12px; padding: 10px 8px;"
    "  text-align: left;"
    "}"
    "QPushButton:hover { background: #3a3a3a; color: #ddd; border-color: #666; }";

static const char *kTransBtn =
    "QPushButton {"
    "  background: #1a1a1a; color: #666;"
    "  border: 1px solid #333; border-radius: 3px;"
    "  font-size: 10px; padding: 2px 6px; min-width: 28px;"
    "}"
    "QPushButton:hover { color: #aaa; border-color: #555; }";

static const char *kSettingsBtn =
    "QPushButton {"
    "  background: #1a1a1a; color: #777;"
    "  border: 1px solid #333; border-radius: 3px;"
    "  font-size: 11px; padding: 3px 10px;"
    "}"
    "QPushButton:hover { color: #bbb; border-color: #555; }";

// ── Constructor ───────────────────────────────────────────────────────────────

DskDock::DskDock(QWidget *parent) : QWidget(parent)
{
    buildUI();

    auto &mgr = DskManager::instance();

    mgr.setRefreshCallback([this]() {
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    });

    mgr.setStateCallback([this](const std::string &name, bool active) {
        QMetaObject::invokeMethod(this, "onStateChanged",
            Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdString(name)),
            Q_ARG(bool, active));
    });
}

// ── UI build ──────────────────────────────────────────────────────────────────

void DskDock::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Top bar ───────────────────────────────────────────────────────────────
    auto *topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);

    m_sceneLabel = new QLabel();
    m_sceneLabel->setStyleSheet("color: #555; font-size: 10px; font-style: italic;");
    topBar->addWidget(m_sceneLabel);
    topBar->addStretch();

    auto *settingsBtn = new QPushButton("Settings");
    settingsBtn->setStyleSheet(kSettingsBtn);
    settingsBtn->setFixedHeight(22);
    connect(settingsBtn, &QPushButton::clicked, this, &DskDock::onSettingsClicked);
    topBar->addWidget(settingsBtn);
    root->addLayout(topBar);

    // ── Scrollable item list ──────────────────────────────────────────────────
    m_scroll = new QScrollArea();
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_itemContainer = new QWidget();
    auto *containerLayout = new QVBoxLayout(m_itemContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(4);
    containerLayout->addStretch();

    m_scroll->setWidget(m_itemContainer);
    root->addWidget(m_scroll);

    refresh();
}

void DskDock::buildItemRow(QWidget * /*container*/, QVBoxLayout *layout,
                            const std::string &sourceName, bool active)
{
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    // Main toggle button
    auto *toggleBtn = new QPushButton();
    toggleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggleBtn->setMinimumHeight(44);
    styleToggle(toggleBtn, active, QString::fromStdString(sourceName));

    QString sname = QString::fromStdString(sourceName);
    connect(toggleBtn, &QPushButton::clicked, this, [this, sname]() {
        onToggleClicked(sname);
    });
    row->addWidget(toggleBtn);

    // Transition config button
    auto *transBtn = new QPushButton("T");
    transBtn->setStyleSheet(kTransBtn);
    transBtn->setFixedSize(28, 44);
    transBtn->setToolTip("Configure show/hide transitions for this item");
    connect(transBtn, &QPushButton::clicked, this, [this, sname]() {
        onTransitionClicked(sname);
    });
    row->addWidget(transBtn);

    // Insert before the trailing stretch (last item)
    int insertAt = layout->count() - 1;
    layout->insertLayout(insertAt < 0 ? 0 : insertAt, row);
}

void DskDock::styleToggle(QPushButton *btn, bool active, const QString &label)
{
    btn->setText(label);
    btn->setStyleSheet(active ? kActive : kInactive);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void DskDock::refresh()
{
    auto &mgr = DskManager::instance();
    m_sceneLabel->setText(QString::fromStdString(mgr.sceneName()));

    // Rebuild the item container
    delete m_itemContainer;
    m_itemContainer = new QWidget();
    auto *layout = new QVBoxLayout(m_itemContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    for (const auto &item : mgr.currentItems())
        buildItemRow(m_itemContainer, layout, item.sourceName, item.visible);

    layout->addStretch();
    m_scroll->setWidget(m_itemContainer);

    // Show a hint when the scene is empty or doesn't exist
    if (mgr.currentItems().empty()) {
        auto *hint = new QLabel(
            "Open Settings to choose your\nDSK scene, then add sources\nto it in OBS.");
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("color: #555; font-size: 11px;");
        layout->insertWidget(0, hint);
    }
}

void DskDock::onToggleClicked(const QString &sourceName)
{
    DskManager::instance().toggle(sourceName.toStdString());
}

void DskDock::onTransitionClicked(const QString &sourceName)
{
    DskItemSettings dlg(sourceName, this);
    dlg.exec();
    // Changes are applied immediately inside the dialog
    DskManager::instance().saveSettings();
}

void DskDock::onSettingsClicked()
{
    DskSettings dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        DskManager::instance().saveSettings();
        refresh();
    }
}

void DskDock::onStateChanged(const QString &sourceName, bool active)
{
    // Find and restyle the toggle button for this source
    auto *layout = qobject_cast<QVBoxLayout *>(m_itemContainer->layout());
    if (!layout) return;

    for (int i = 0; i < layout->count(); i++) {
        QLayoutItem *li = layout->itemAt(i);
        if (!li) continue;
        QHBoxLayout *row = qobject_cast<QHBoxLayout *>(li->layout());
        if (!row) continue;

        QLayoutItem *first = row->itemAt(0);
        if (!first) continue;
        auto *btn = qobject_cast<QPushButton *>(first->widget());
        if (!btn) continue;

        if (btn->text() == sourceName) {
            styleToggle(btn, active, sourceName);
            break;
        }
    }
}

void DskDock::scheduleRefresh()
{
    QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
}
