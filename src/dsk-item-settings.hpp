#pragma once

#include <obs-module.h>
#include "dsk-manager.hpp"

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QString>
#include <string>

class DskItemSettings : public QDialog {
    Q_OBJECT

public:
    explicit DskItemSettings(const QString &sourceName, QWidget *parent = nullptr);
    ~DskItemSettings();

private slots:
    void onAccept();
    void onTest();
    void onShowTypeChanged(int index);
    void onHideTypeChanged(int index);
    void onConfigureShow();
    void onConfigureHide();

private:
    void buildUI();
    void populateTransitionCombo(QComboBox *combo, const QString &currentType);
    obs_source_t *createTempSource(const QString &typeId, const std::string &settingsJson);
    DskTransitionConfig buildConfig() const;

    QString      m_sourceName;

    QComboBox   *m_showTypeCb    = nullptr;
    QSpinBox    *m_showDurSpin   = nullptr;
    QPushButton *m_showConfigBtn = nullptr;

    QComboBox   *m_hideTypeCb    = nullptr;
    QSpinBox    *m_hideDurSpin   = nullptr;
    QPushButton *m_hideConfigBtn = nullptr;

    obs_source_t *m_showTransSrc = nullptr;
    obs_source_t *m_hideTransSrc = nullptr;
};
