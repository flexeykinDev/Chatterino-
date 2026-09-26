// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/BaseWidget.hpp"

#include <QIcon>
#include <QPaintEvent>
#include <QWidget>

#include <functional>

namespace chatterino {

class SettingsPage;
class SettingsDialog;

enum class SettingsTabId {
    None,
    General,
    Accounts,
    Highlights,
    Moderation,
    About,
};

class SettingsDialogTab : public BaseWidget
{
    Q_OBJECT

public:
    SettingsDialogTab(SettingsDialog *dialog_,
                      std::function<SettingsPage *()> page_,
                      const QString &name, QString imageFileName,
                      SettingsTabId id);

    void setSelected(bool selected_);
    SettingsPage *page();
    SettingsTabId id() const;

    const QString &name() const;

    /// The width at which this tab's label is drawn in full.
    ///
    /// Depends on the tab's current height, because the icon and the padding
    /// around it are sized from it, so call this only once the height is
    /// settled. Translated labels are routinely longer than the English ones
    /// they replace, so a fixed width sized for English clips them.
    int naturalWidth() const;

Q_SIGNALS:
    void selectedChanged(bool);

private:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;

    struct {
        QString labelText;
        QIcon icon;
    } ui_;

    // Parent settings dialog
    SettingsDialog *dialog_{};
    SettingsPage *page_{};
    std::function<SettingsPage *()> lazyPage_;
    SettingsTabId id_;
    QString name_;

    bool selected_ = false;
};

}  // namespace chatterino
