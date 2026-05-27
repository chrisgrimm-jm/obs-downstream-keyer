#include "dsk-settings.hpp"
#include "dsk-manager.hpp"
#include "companion-server.hpp"

#include <obs-frontend-api.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>

// ── Constructor ───────────────────────────────────────────────────────────────

DskSettings::DskSettings(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Downstream Keyer Settings");
    setMinimumWidth(480);
    buildUI();
}

// ── UI ────────────────────────────────────────────────────────────────────────

void DskSettings::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);

    // ── DSK scene selector ────────────────────────────────────────────────────
    auto *sceneGroup = new QGroupBox("DSK Source Scene");
    auto *sceneLayout = new QVBoxLayout(sceneGroup);

    auto *sceneHint = new QLabel(
        "Choose the OBS scene that contains all your DSK graphics. "
        "Build it in OBS normally — position your logos, lower thirds, and sponsor "
        "cards exactly where you want them. The dock will show every source in that "
        "scene as a toggle button.");
    sceneHint->setWordWrap(true);
    sceneHint->setStyleSheet("color: #999; font-size: 11px;");
    sceneLayout->addWidget(sceneHint);

    auto *sceneForm = new QFormLayout();
    m_sceneCombo = new QComboBox();
    populateSceneCombo();
    sceneForm->addRow("DSK scene:", m_sceneCombo);
    sceneLayout->addLayout(sceneForm);

    root->addWidget(sceneGroup);

    // ── Setup ─────────────────────────────────────────────────────────────────
    auto *setupGroup  = new QGroupBox("Scene Setup");
    auto *setupLayout = new QVBoxLayout(setupGroup);

    auto *setupHint = new QLabel(
        "Click below to automatically nest your DSK scene at the top of every "
        "other scene in the current collection. You only need to do this once, "
        "or again when you add new scenes.");
    setupHint->setWordWrap(true);
    setupHint->setStyleSheet("color: #999; font-size: 11px;");
    setupLayout->addWidget(setupHint);

    auto *btnRow  = new QHBoxLayout();
    btnRow->addStretch();
    auto *addBtn  = new QPushButton("Add DSK scene to all scenes");
    addBtn->setStyleSheet(
        "QPushButton { background:#2980b9; color:#fff; border:none;"
        "  border-radius:3px; padding:5px 14px; }"
        "QPushButton:hover { background:#3498db; }");
    connect(addBtn, &QPushButton::clicked, this, &DskSettings::onAddToAllScenes);
    btnRow->addWidget(addBtn);
    setupLayout->addLayout(btnRow);
    root->addWidget(setupGroup);

    // ── Companion HTTP ────────────────────────────────────────────────────────
    auto *httpGroup = new QGroupBox("Bitfocus Companion / HTTP API");
    auto *httpForm  = new QFormLayout(httpGroup);

    m_httpPortSpin = new QSpinBox();
    m_httpPortSpin->setRange(1024, 65535);
    m_httpPortSpin->setValue(DskManager::instance().httpPort());
    httpForm->addRow("HTTP port:", m_httpPortSpin);

    auto *apiHint = new QLabel(
        "GET /api/status   "
        "POST /api/item/:name/activate|deactivate|toggle");
    apiHint->setStyleSheet("color: #555; font-size: 11px; font-family: monospace;");
    httpForm->addRow(apiHint);
    root->addWidget(httpGroup);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &DskSettings::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);
}

void DskSettings::populateSceneCombo()
{
    m_sceneCombo->clear();

    QString current = QString::fromStdString(DskManager::instance().sceneName());

    struct obs_frontend_source_list list = {};
    obs_frontend_get_scenes(&list);
    for (size_t i = 0; i < list.sources.num; i++) {
        const char *name = obs_source_get_name(list.sources.array[i]);
        if (name) m_sceneCombo->addItem(QString::fromUtf8(name));
    }
    obs_frontend_source_list_free(&list);

    int idx = m_sceneCombo->findText(current);
    m_sceneCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void DskSettings::onAddToAllScenes()
{
    // Apply the selected scene name first
    DskManager::instance().setSceneName(
        m_sceneCombo->currentText().toStdString());
    DskManager::instance().addDskToAllScenes();

    QMessageBox::information(this, "DSK Setup",
        "Done! Your DSK scene is now nested at the top of every scene.\n\n"
        "Tip: Configure show/hide transitions per item using the T button "
        "in the dock. Per-item hotkeys are available in OBS Settings → Hotkeys.");
}

void DskSettings::onAccept()
{
    auto &mgr = DskManager::instance();
    mgr.setSceneName(m_sceneCombo->currentText().toStdString());

    int newPort = m_httpPortSpin->value();
    if (newPort != mgr.httpPort()) {
        mgr.setHttpPort(newPort);
        extern CompanionServer *g_companionServer;
        if (g_companionServer) {
            g_companionServer->stop();
            g_companionServer->start(static_cast<quint16>(newPort));
        }
    }

    accept();
}
