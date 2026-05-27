#include "dsk-item-settings.hpp"
#include "dsk-manager.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QTimer>

// ── Constructor / destructor ──────────────────────────────────────────────────

DskItemSettings::DskItemSettings(const QString &sourceName, QWidget *parent)
    : QDialog(parent), m_sourceName(sourceName)
{
    setWindowTitle("Transitions: " + sourceName);
    setMinimumWidth(400);
    setWindowModality(Qt::WindowModal);
    buildUI();
}

DskItemSettings::~DskItemSettings()
{
    if (m_showTransSrc) obs_source_release(m_showTransSrc);
    if (m_hideTransSrc) obs_source_release(m_hideTransSrc);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

obs_source_t *DskItemSettings::createTempSource(const QString &typeId,
                                                const std::string &settingsJson)
{
    if (typeId.isEmpty()) return nullptr;
    obs_data_t *s = settingsJson.empty()
                  ? nullptr
                  : obs_data_create_from_json(settingsJson.c_str());
    obs_source_t *src = obs_source_create(typeId.toUtf8().constData(), nullptr, s, nullptr);
    if (s) obs_data_release(s);
    return src;
}

void DskItemSettings::populateTransitionCombo(QComboBox *combo, const QString &currentType)
{
    combo->addItem("(none)", "");

    const char *typeId;
    size_t idx = 0;
    while (obs_enum_source_types(idx++, &typeId)) {
        if (!typeId) continue;
        QString id = QString::fromUtf8(typeId);
        if (!id.endsWith("_transition", Qt::CaseInsensitive)) continue;
        const char *displayName = obs_source_get_display_name(typeId);
        QString label = displayName ? QString::fromUtf8(displayName) : id;
        combo->addItem(label, id);
    }

    int sel = combo->findData(currentType);
    combo->setCurrentIndex(sel >= 0 ? sel : 0);
}

DskTransitionConfig DskItemSettings::buildConfig() const
{
    DskTransitionConfig cfg;
    cfg.showType     = m_showTypeCb->currentData().toString().toStdString();
    cfg.showDuration = (uint32_t)m_showDurSpin->value();
    cfg.hideType     = m_hideTypeCb->currentData().toString().toStdString();
    cfg.hideDuration = (uint32_t)m_hideDurSpin->value();
    cfg.autoDuration = (m_autoCheck->isChecked()) ? (uint32_t)m_autoDurSpin->value() : 0;

    if (m_showTransSrc) {
        obs_data_t *s = obs_source_get_settings(m_showTransSrc);
        if (s) {
            const char *json = obs_data_get_json(s);
            if (json) cfg.showSettings = json;
            obs_data_release(s);
        }
    }
    if (m_hideTransSrc) {
        obs_data_t *s = obs_source_get_settings(m_hideTransSrc);
        if (s) {
            const char *json = obs_data_get_json(s);
            if (json) cfg.hideSettings = json;
            obs_data_release(s);
        }
    }
    return cfg;
}

// ── UI ────────────────────────────────────────────────────────────────────────

void DskItemSettings::buildUI()
{
    auto &mgr = DskManager::instance();
    const DskTransitionConfig *cfg = mgr.transitionConfig(m_sourceName.toStdString());

    QString     showType     = cfg ? QString::fromStdString(cfg->showType)     : "";
    int         showDur      = cfg ? (int)cfg->showDuration                    : 300;
    std::string showSettings = cfg ? cfg->showSettings                         : "";
    QString     hideType     = cfg ? QString::fromStdString(cfg->hideType)     : "";
    int         hideDur      = cfg ? (int)cfg->hideDuration                    : 300;
    std::string hideSettings = cfg ? cfg->hideSettings                         : "";
    int         autoDur      = cfg ? (int)cfg->autoDuration                    : 0;

    // Create temp sources with any previously saved settings
    if (!showType.isEmpty()) m_showTransSrc = createTempSource(showType, showSettings);
    if (!hideType.isEmpty()) m_hideTransSrc = createTempSource(hideType, hideSettings);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto *hint = new QLabel(
        "These transitions fire when this DSK item is shown or hidden. "
        "Select <b>Move</b> from the obs-move plugin, then click "
        "<b>Configure\xe2\x80\xa6</b> to set direction, easing, and distance.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #999; font-size: 11px;");
    root->addWidget(hint);

    // ── Show transition ───────────────────────────────────────────────────────
    auto *showGroup = new QGroupBox("Show (keyer ON)");
    auto *showForm  = new QFormLayout(showGroup);

    // Type row: [combo] [Configure...]
    auto *showTypeRow  = new QWidget();
    auto *showTypeHBox = new QHBoxLayout(showTypeRow);
    showTypeHBox->setContentsMargins(0, 0, 0, 0);
    m_showTypeCb = new QComboBox();
    populateTransitionCombo(m_showTypeCb, showType);
    m_showConfigBtn = new QPushButton("Configure\xe2\x80\xa6");
    m_showConfigBtn->setEnabled(m_showTransSrc != nullptr);
    showTypeHBox->addWidget(m_showTypeCb, 1);
    showTypeHBox->addWidget(m_showConfigBtn);
    showForm->addRow("Transition:", showTypeRow);

    m_showDurSpin = new QSpinBox();
    m_showDurSpin->setRange(0, 10000);
    m_showDurSpin->setSuffix(" ms");
    m_showDurSpin->setSingleStep(50);
    m_showDurSpin->setValue(showDur);
    showForm->addRow("Duration:", m_showDurSpin);

    root->addWidget(showGroup);

    // ── Hide transition ───────────────────────────────────────────────────────
    auto *hideGroup = new QGroupBox("Hide (keyer OFF)");
    auto *hideForm  = new QFormLayout(hideGroup);

    auto *hideTypeRow  = new QWidget();
    auto *hideTypeHBox = new QHBoxLayout(hideTypeRow);
    hideTypeHBox->setContentsMargins(0, 0, 0, 0);
    m_hideTypeCb = new QComboBox();
    populateTransitionCombo(m_hideTypeCb, hideType);
    m_hideConfigBtn = new QPushButton("Configure\xe2\x80\xa6");
    m_hideConfigBtn->setEnabled(m_hideTransSrc != nullptr);
    hideTypeHBox->addWidget(m_hideTypeCb, 1);
    hideTypeHBox->addWidget(m_hideConfigBtn);
    hideForm->addRow("Transition:", hideTypeRow);

    m_hideDurSpin = new QSpinBox();
    m_hideDurSpin->setRange(0, 10000);
    m_hideDurSpin->setSuffix(" ms");
    m_hideDurSpin->setSingleStep(50);
    m_hideDurSpin->setValue(hideDur);
    hideForm->addRow("Duration:", m_hideDurSpin);

    root->addWidget(hideGroup);

    // ── Auto-hide ─────────────────────────────────────────────────────────────
    auto *autoGroup = new QGroupBox("Auto-hide");
    auto *autoForm  = new QFormLayout(autoGroup);

    auto *autoRow  = new QWidget();
    auto *autoHBox = new QHBoxLayout(autoRow);
    autoHBox->setContentsMargins(0, 0, 0, 0);
    m_autoCheck   = new QCheckBox("Enable");
    m_autoDurSpin = new QSpinBox();
    m_autoDurSpin->setRange(1, 3600);
    m_autoDurSpin->setSuffix(" sec");
    m_autoDurSpin->setValue(autoDur > 0 ? autoDur : 10);
    m_autoDurSpin->setEnabled(autoDur > 0);
    m_autoCheck->setChecked(autoDur > 0);
    autoHBox->addWidget(m_autoCheck);
    autoHBox->addWidget(m_autoDurSpin, 1);
    autoForm->addRow("Duration:", autoRow);

    connect(m_autoCheck, &QCheckBox::toggled, m_autoDurSpin, &QSpinBox::setEnabled);

    root->addWidget(autoGroup);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *btnBox  = new QDialogButtonBox();
    auto *testBtn = btnBox->addButton("Test", QDialogButtonBox::ActionRole);
    btnBox->addButton(QDialogButtonBox::Ok);
    btnBox->addButton(QDialogButtonBox::Cancel);

    connect(m_showTypeCb,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DskItemSettings::onShowTypeChanged);
    connect(m_hideTypeCb,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DskItemSettings::onHideTypeChanged);
    connect(m_showConfigBtn, &QPushButton::clicked, this, &DskItemSettings::onConfigureShow);
    connect(m_hideConfigBtn, &QPushButton::clicked, this, &DskItemSettings::onConfigureHide);
    connect(testBtn,  &QPushButton::clicked,       this, &DskItemSettings::onTest);
    connect(btnBox,   &QDialogButtonBox::accepted, this, &DskItemSettings::onAccept);
    connect(btnBox,   &QDialogButtonBox::rejected, this, &QDialog::reject);

    root->addWidget(btnBox);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void DskItemSettings::onShowTypeChanged(int)
{
    if (m_showTransSrc) { obs_source_release(m_showTransSrc); m_showTransSrc = nullptr; }
    QString typeId = m_showTypeCb->currentData().toString();
    if (!typeId.isEmpty())
        m_showTransSrc = createTempSource(typeId, "");
    m_showConfigBtn->setEnabled(m_showTransSrc != nullptr);
}

void DskItemSettings::onHideTypeChanged(int)
{
    if (m_hideTransSrc) { obs_source_release(m_hideTransSrc); m_hideTransSrc = nullptr; }
    QString typeId = m_hideTypeCb->currentData().toString();
    if (!typeId.isEmpty())
        m_hideTransSrc = createTempSource(typeId, "");
    m_hideConfigBtn->setEnabled(m_hideTransSrc != nullptr);
}

void DskItemSettings::onConfigureShow()
{
    if (m_showTransSrc)
        obs_frontend_open_source_properties(m_showTransSrc);
}

void DskItemSettings::onConfigureHide()
{
    if (m_hideTransSrc)
        obs_frontend_open_source_properties(m_hideTransSrc);
}

void DskItemSettings::onAccept()
{
    DskManager::instance().setTransitionConfig(m_sourceName.toStdString(), buildConfig());
    accept();
}

void DskItemSettings::onTest()
{
    auto &mgr = DskManager::instance();
    std::string name = m_sourceName.toStdString();
    mgr.setTransitionConfig(name, buildConfig());
    mgr.activate(name);

    DskTransitionConfig cfg = buildConfig();
    int holdMs = std::max((int)cfg.showDuration + 500, 1500);
    QTimer::singleShot(holdMs, this, [name]() {
        DskManager::instance().deactivate(name);
    });
}
