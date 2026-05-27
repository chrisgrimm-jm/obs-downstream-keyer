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
#include <QPushButton>

// ── Styles ────────────────────────────────────────────────────────────────────

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

    QString sname = QString::fromStdString(sourceName);

    auto *toggleBtn = new DskTimerButton(sname, this);
    toggleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggleBtn->setMinimumHeight(44);
    toggleBtn->setActive(active); // no countdown on initial build — just show state

    connect(toggleBtn, &QPushButton::clicked, this, [this, sname]() {
        onToggleClicked(sname);
    });
    row->addWidget(toggleBtn);

    auto *transBtn = new QPushButton("T");
    transBtn->setStyleSheet(kTransBtn);
    transBtn->setFixedSize(28, 44);
    transBtn->setToolTip("Configure show/hide transitions for this item");
    connect(transBtn, &QPushButton::clicked, this, [this, sname]() {
        onTransitionClicked(sname);
    });
    row->addWidget(transBtn);

    int insertAt = layout->count() - 1;
    layout->insertLayout(insertAt < 0 ? 0 : insertAt, row);
}

DskTimerButton *DskDock::findTimerButton(const QString &sourceName) const
{
    auto *layout = qobject_cast<QVBoxLayout *>(m_itemContainer->layout());
    if (!layout) return nullptr;

    for (int i = 0; i < layout->count(); i++) {
        QLayoutItem *li = layout->itemAt(i);
        if (!li) continue;
        auto *row = qobject_cast<QHBoxLayout *>(li->layout());
        if (!row) continue;
        QLayoutItem *first = row->itemAt(0);
        if (!first) continue;
        auto *btn = qobject_cast<DskTimerButton *>(first->widget());
        if (btn && btn->text() == sourceName)
            return btn;
    }
    return nullptr;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void DskDock::refresh()
{
    auto &mgr = DskManager::instance();
    m_sceneLabel->setText(QString::fromStdString(mgr.sceneName()));

    delete m_itemContainer;
    m_itemContainer = new QWidget();
    auto *layout = new QVBoxLayout(m_itemContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    for (const auto &item : mgr.currentItems())
        buildItemRow(m_itemContainer, layout, item.sourceName, item.visible);

    layout->addStretch();
    m_scroll->setWidget(m_itemContainer);

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
    DskTimerButton *btn = findTimerButton(sourceName);
    if (!btn) return;

    int autoDurationMs = 0;
    if (active) {
        const DskTransitionConfig *cfg =
            DskManager::instance().transitionConfig(sourceName.toStdString());
        if (cfg && cfg->autoDuration > 0)
            autoDurationMs = (int)(cfg->autoDuration * 1000);
    }
    btn->setActive(active, autoDurationMs);
}

void DskDock::scheduleRefresh()
{
    QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
}
