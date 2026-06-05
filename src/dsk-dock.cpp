#include "dsk-dock.hpp"
#include "dsk-manager.hpp"
#include "dsk-item-settings.hpp"
#include "dsk-settings.hpp"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QMetaObject>
#include <QSizePolicy>
#include <QFrame>
#include <QPushButton>
#include <QMenu>
#include <QColorDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <algorithm>

// ── Transition clipboard (in-memory, session-only) ────────────────────────────

static DskTransitionConfig s_clipboardTransition;
static bool                s_hasClipboard = false;

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
    m_viewMode = static_cast<ViewMode>(DskManager::instance().viewMode());
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

    m_viewToggle = new QPushButton();
    m_viewToggle->setStyleSheet(kSettingsBtn);
    m_viewToggle->setFixedHeight(22);
    m_viewToggle->setToolTip("Toggle list / grid view");
    connect(m_viewToggle, &QPushButton::clicked, this, &DskDock::onViewToggle);
    topBar->addWidget(m_viewToggle);

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

    updateViewToggleIcon();
    refresh();
}

void DskDock::buildListView(QVBoxLayout *layout)
{
    auto &mgr = DskManager::instance();

    for (const auto &item : mgr.currentItems()) {
        QString sname = QString::fromStdString(item.sourceName);

        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);

        auto *toggleBtn = new DskTimerButton(sname, this);
        toggleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toggleBtn->setMinimumHeight(44);
        toggleBtn->setActive(item.visible);

        // Apply saved button color
        const DskTransitionConfig *cfg =
            DskManager::instance().transitionConfig(item.sourceName);
        if (cfg && !cfg->buttonColor.empty())
            toggleBtn->setButtonColor(QColor(QString::fromStdString(cfg->buttonColor)));

        // Let drag events on the button flow through our event filter
        toggleBtn->setAcceptDrops(true);
        toggleBtn->installEventFilter(this);

        // If refresh() was deferred while a drag was in progress, run it now
        connect(toggleBtn, &DskTimerButton::dragEnded, this, [this]() {
            if (m_pendingRefresh)
                QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
        });

        connect(toggleBtn, &QPushButton::clicked, this, [this, sname]() {
            onToggleClicked(sname);
        });

        // Right-click menu
        toggleBtn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(toggleBtn, &QWidget::customContextMenuRequested, this,
            [this, sname, toggleBtn](const QPoint &pos) {
                QMenu menu(this);
                menu.addAction("Configure Transitions\xe2\x80\xa6", this, [this, sname]() {
                    onTransitionClicked(sname);
                });
                menu.addAction("Copy Transitions", this, [this, sname]() {
                    onCopyTransitions(sname);
                });
                auto *pasteAct = menu.addAction("Paste Transitions", this, [this, sname]() {
                    onPasteTransitions(sname);
                });
                pasteAct->setEnabled(s_hasClipboard);
                menu.addSeparator();
                menu.addAction("Set Color\xe2\x80\xa6", this, [this, sname]() {
                    onSetColorClicked(sname);
                });
                menu.addAction("Reset to Default Color", this, [this, sname]() {
                    onResetColorClicked(sname);
                });
                menu.exec(toggleBtn->mapToGlobal(pos));
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
}

void DskDock::buildGridView(QVBoxLayout *layout)
{
    auto &mgr   = DskManager::instance();
    auto  items = mgr.currentItems();
    if (items.empty()) return;

    auto *gridWidget = new QWidget();
    auto *grid       = new QGridLayout(gridWidget);
    const int cols   = mgr.gridColumns();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    for (int c = 0; c < cols; c++)
        grid->setColumnStretch(c, 1);

    int col = 0, row = 0;
    for (const auto &item : items) {
        QString sname = QString::fromStdString(item.sourceName);

        auto *btn = new DskTimerButton(sname, gridWidget);
        btn->setGridMode(true);
        btn->setMinimumHeight(55);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setActive(item.visible);

        // Apply saved button color
        const DskTransitionConfig *cfg = mgr.transitionConfig(item.sourceName);
        if (cfg && !cfg->buttonColor.empty())
            btn->setButtonColor(QColor(QString::fromStdString(cfg->buttonColor)));

        // Let drag events on the button flow through our event filter
        btn->setAcceptDrops(true);
        btn->installEventFilter(this);

        // If refresh() was deferred while a drag was in progress, run it now
        connect(btn, &DskTimerButton::dragEnded, this, [this]() {
            if (m_pendingRefresh)
                QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
        });

        connect(btn, &QPushButton::clicked, this, [this, sname]() {
            onToggleClicked(sname);
        });

        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QWidget::customContextMenuRequested, this,
            [this, sname, btn](const QPoint &pos) {
                onGridContextMenu(sname, btn->mapToGlobal(pos));
            });

        grid->addWidget(btn, row, col);
        if (++col >= cols) { col = 0; ++row; }
    }

    int insertAt = layout->count() - 1;
    layout->insertWidget(insertAt < 0 ? 0 : insertAt, gridWidget);
}

void DskDock::onGridContextMenu(const QString &sourceName, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction("Configure Transitions\xe2\x80\xa6", this, [this, sourceName]() {
        onTransitionClicked(sourceName);
    });
    menu.addAction("Copy Transitions", this, [this, sourceName]() {
        onCopyTransitions(sourceName);
    });
    auto *pasteAct = menu.addAction("Paste Transitions", this, [this, sourceName]() {
        onPasteTransitions(sourceName);
    });
    pasteAct->setEnabled(s_hasClipboard);
    menu.addSeparator();
    menu.addAction("Set Color\xe2\x80\xa6", this, [this, sourceName]() {
        onSetColorClicked(sourceName);
    });
    menu.addAction("Reset to Default Color", this, [this, sourceName]() {
        onResetColorClicked(sourceName);
    });
    menu.exec(globalPos);
}

void DskDock::updateViewToggleIcon()
{
    // Show the icon of the mode you'll switch TO when clicked
    m_viewToggle->setText(m_viewMode == ViewMode::List ? "\xe2\x8a\x9e" : "\xe2\x98\xb0");
}

DskTimerButton *DskDock::findTimerButton(const QString &sourceName) const
{
    const auto buttons = m_itemContainer->findChildren<DskTimerButton *>();
    for (auto *btn : buttons)
        if (btn->text() == sourceName)
            return btn;
    return nullptr;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void DskDock::refresh()
{
    // A drag holds live pointers to buttons inside m_itemContainer.
    // Deleting that widget mid-drag causes a crash (SIGSEGV on macOS,
    // access violation on Windows). Defer and retry once the drag ends.
    if (DskTimerButton::isDragActive()) {
        m_pendingRefresh = true;
        return;
    }
    m_pendingRefresh = false;

    auto &mgr = DskManager::instance();
    m_sceneLabel->setText(QString::fromStdString(mgr.sceneName()));

    delete m_itemContainer;
    m_itemContainer = new QWidget();
    m_itemContainer->setAcceptDrops(true);
    m_itemContainer->installEventFilter(this);

    // Thin green line shown between items during drag-over
    m_dropIndicator = new QWidget(m_itemContainer);
    m_dropIndicator->setFixedHeight(3);
    m_dropIndicator->setStyleSheet("background: #2ecc71; border-radius: 1px;");
    m_dropIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropIndicator->hide();

    auto *layout = new QVBoxLayout(m_itemContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    if (m_viewMode == ViewMode::List)
        buildListView(layout);
    else
        buildGridView(layout);

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

void DskDock::onViewToggle()
{
    m_viewMode = (m_viewMode == ViewMode::List) ? ViewMode::Grid : ViewMode::List;
    DskManager::instance().setViewMode(static_cast<int>(m_viewMode));
    DskManager::instance().saveSettings();
    updateViewToggleIcon();
    refresh();
}

void DskDock::onToggleClicked(const QString &sourceName)
{
    DskManager::instance().toggle(sourceName.toStdString());
}

void DskDock::onTransitionClicked(const QString &sourceName)
{
    auto *dlg = new DskItemSettings(sourceName, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, []() {
        DskManager::instance().saveSettings();
    });
    dlg->show();
}

void DskDock::onSetColorClicked(const QString &sourceName)
{
    std::string sname = sourceName.toStdString();

    // Seed dialog with the currently saved color (or the default dark gray)
    QColor current(0x2c, 0x2c, 0x2c);
    const DskTransitionConfig *cfg = DskManager::instance().transitionConfig(sname);
    if (cfg && !cfg->buttonColor.empty())
        current = QColor(QString::fromStdString(cfg->buttonColor));

    QColor picked = QColorDialog::getColor(current, this, "Choose Button Color");
    if (!picked.isValid()) return; // user cancelled

    DskManager::instance().setButtonColor(sname, picked.name().toStdString());
    DskManager::instance().saveSettings();

    // Update the live button without a full refresh
    DskTimerButton *btn = findTimerButton(sourceName);
    if (btn) btn->setButtonColor(picked);
}

void DskDock::onCopyTransitions(const QString &sourceName)
{
    const DskTransitionConfig *cfg =
        DskManager::instance().transitionConfig(sourceName.toStdString());
    s_clipboardTransition = cfg ? *cfg : DskTransitionConfig{};
    s_hasClipboard        = true;
}

void DskDock::onPasteTransitions(const QString &sourceName)
{
    if (!s_hasClipboard) return;
    std::string sname = sourceName.toStdString();

    // Carry over the target's existing button color — don't overwrite it
    DskTransitionConfig cfg = s_clipboardTransition;
    const DskTransitionConfig *existing =
        DskManager::instance().transitionConfig(sname);
    if (existing) cfg.buttonColor = existing->buttonColor;

    DskManager::instance().setTransitionConfig(sname, cfg);
    DskManager::instance().saveSettings();
}

void DskDock::onResetColorClicked(const QString &sourceName)
{
    DskManager::instance().setButtonColor(sourceName.toStdString(), "");
    DskManager::instance().saveSettings();

    DskTimerButton *btn = findTimerButton(sourceName);
    if (btn) btn->setButtonColor(QColor()); // invalid color → resets to default dark gray
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

// ── Drag-and-drop reordering ──────────────────────────────────────────────────

bool DskDock::eventFilter(QObject *obj, QEvent *event)
{
    // ── Drag events on a button ───────────────────────────────────────────────
    // Qt only delivers drag events to the topmost widget under the cursor.
    // Buttons sit on top of m_itemContainer, so without this block drag events
    // over buttons would never update the drop indicator.
    if (auto *btn = qobject_cast<DskTimerButton *>(obj)) {
        switch (event->type()) {
        case QEvent::DragEnter: {
            auto *e = static_cast<QDragEnterEvent *>(event);
            if (e->mimeData()->hasFormat("application/x-dsk-source"))
                e->acceptProposedAction();
            return true;
        }
        case QEvent::DragMove: {
            auto *e = static_cast<QDragMoveEvent *>(event);
            if (!e->mimeData()->hasFormat("application/x-dsk-source")) break;
            e->acceptProposedAction();
            // Translate from button-local coords to container coords
            updateDropIndicator(btn->mapTo(m_itemContainer, e->position().toPoint()));
            return true;
        }
        case QEvent::DragLeave:
            // Don't hide — cursor may be moving to the next button
            return true;
        case QEvent::Drop: {
            auto *e = static_cast<QDropEvent *>(event);
            if (!e->mimeData()->hasFormat("application/x-dsk-source")) break;
            QString name = QString::fromUtf8(e->mimeData()->data("application/x-dsk-source"));
            int idx = m_dropIndex;
            hideDropIndicator();
            e->acceptProposedAction();
            if (idx >= 0)
                DskManager::instance().reorderItem(name.toStdString(), idx);
            return true;
        }
        default: break;
        }
        return QWidget::eventFilter(obj, event);
    }

    // ── Drag events on the container itself (gaps between buttons) ────────────
    if (obj != m_itemContainer)
        return QWidget::eventFilter(obj, event);

    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *e = static_cast<QDragEnterEvent *>(event);
        if (e->mimeData()->hasFormat("application/x-dsk-source"))
            e->acceptProposedAction();
        return true;
    }
    case QEvent::DragMove: {
        auto *e = static_cast<QDragMoveEvent *>(event);
        if (!e->mimeData()->hasFormat("application/x-dsk-source")) break;
        e->acceptProposedAction();
        updateDropIndicator(e->position().toPoint());
        return true;
    }
    case QEvent::DragLeave:
        hideDropIndicator();
        return true;
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        if (!e->mimeData()->hasFormat("application/x-dsk-source")) break;
        QString name = QString::fromUtf8(
            e->mimeData()->data("application/x-dsk-source"));
        int idx = m_dropIndex;
        hideDropIndicator();
        e->acceptProposedAction();
        if (idx >= 0)
            DskManager::instance().reorderItem(name.toStdString(), idx);
        return true;
    }
    default: break;
    }
    return QWidget::eventFilter(obj, event);
}

void DskDock::updateDropIndicator(const QPoint &pos)
{
    if (!m_dropIndicator) return;

    // Collect all timer buttons, sorted top-to-bottom then left-to-right
    auto buttons = m_itemContainer->findChildren<DskTimerButton *>();
    std::sort(buttons.begin(), buttons.end(), [this](DskTimerButton *a, DskTimerButton *b) {
        QPoint pa = a->mapTo(m_itemContainer, QPoint(0, 0));
        QPoint pb = b->mapTo(m_itemContainer, QPoint(0, 0));
        return pa.y() != pb.y() ? pa.y() < pb.y() : pa.x() < pb.x();
    });

    if (buttons.isEmpty()) { hideDropIndicator(); return; }

    // Default: before the first item
    m_dropIndex = 0;
    int indicatorY = buttons.first()->mapTo(m_itemContainer, QPoint(0, 0)).y() - 3;

    for (int i = 0; i < buttons.size(); i++) {
        QPoint p = buttons[i]->mapTo(m_itemContainer, QPoint(0, 0));
        if (pos.y() > p.y() + buttons[i]->height() / 2) {
            m_dropIndex = i + 1;
            indicatorY  = p.y() + buttons[i]->height() + 1;
        }
    }

    m_dropIndicator->setGeometry(0, indicatorY, m_itemContainer->width(), 3);
    m_dropIndicator->show();
    m_dropIndicator->raise();
}

void DskDock::hideDropIndicator()
{
    m_dropIndex = -1;
    if (m_dropIndicator) m_dropIndicator->hide();
}
