/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_touch_layer_options_sheet.h"

#include <QAction>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisViewManager.h>
#include <kactioncollection.h>
#include <klocalizedstring.h>
#include <kis_node.h>
#include <kis_node_manager.h>
#include <kis_slider_spin_box.h>

namespace {

QString stripAmpersands(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

QRect clampToScreen(const QRect &desired, const QPoint &referencePoint)
{
    QScreen *screen = QGuiApplication::screenAt(referencePoint);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return desired;
    }

    const QRect avail = screen->availableGeometry();
    QRect clamped = desired;

    if (clamped.left() < avail.left()) {
        clamped.moveLeft(avail.left());
    }
    if (clamped.right() > avail.right()) {
        clamped.moveRight(avail.right());
    }
    if (clamped.top() < avail.top()) {
        clamped.moveTop(avail.top());
    }
    if (clamped.bottom() > avail.bottom()) {
        clamped.moveBottom(avail.bottom());
    }

    return clamped;
}

} // namespace

KisTouchLayerOptionsSheet::KisTouchLayerOptionsSheet(KisKActionCollection *actionCollection, QWidget *parent)
    : QFrame(parent)
    , m_actionCollection(actionCollection)
{
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName(QStringLiteral("kisTouchLayerOptionsSheet"));

    setStyleSheet(QStringLiteral(
        "QFrame#kisTouchLayerOptionsSheet {"
        "  background-color: rgba(30, 30, 30, 245);"
        "  border: 1px solid rgba(255, 255, 255, 40);"
        "  border-radius: 16px;"
        "  color: rgb(240, 240, 240);"
        "}"
        "QLabel {"
        "  color: rgb(240, 240, 240);"
        "}"
        "QToolButton {"
        "  color: rgb(240, 240, 240);"
        "  background: rgba(255, 255, 255, 10);"
        "  border: 1px solid rgba(255, 255, 255, 25);"
        "  border-radius: 12px;"
        "  padding: 10px;"
        "}"
        "QToolButton:pressed {"
        "  background-color: rgba(255, 255, 255, 25);"
        "}"
        "QToolButton:checked {"
        "  background-color: rgba(90, 160, 255, 40);"
        "  border-color: rgba(90, 160, 255, 80);"
        "}"));

    rebuildUi();
}

KisTouchLayerOptionsSheet::~KisTouchLayerOptionsSheet() = default;

void KisTouchLayerOptionsSheet::setActionCollection(KisKActionCollection *actionCollection)
{
    if (m_actionCollection == actionCollection) {
        return;
    }
    m_actionCollection = actionCollection;
    rebuildUi();
}

void KisTouchLayerOptionsSheet::openAtGlobalPos(const QPoint &globalPos)
{
    rebuildUi();

    QSize desiredSize(720, 520);

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QSize maxSize = screen->availableGeometry().size() - QSize(32, 32);
        desiredSize = desiredSize.boundedTo(maxSize);
        desiredSize.setWidth(qMax(desiredSize.width(), 420));
        desiredSize.setHeight(qMax(desiredSize.height(), 340));
    }

    resize(desiredSize);

    const QRect desiredRect(QPoint(globalPos.x() - width() / 2, globalPos.y() - height() / 2), size());
    const QRect finalRect = clampToScreen(desiredRect, globalPos);
    move(finalRect.topLeft());

    show();
    raise();
}

void KisTouchLayerOptionsSheet::rebuildUi()
{
    m_opacitySlider = nullptr;

    QLayout *oldLayout = layout();
    if (oldLayout) {
        QLayoutItem *item = nullptr;
        while ((item = oldLayout->takeAt(0))) {
            if (QWidget *w = item->widget()) {
                delete w;
            }
            delete item;
        }
        delete oldLayout;
    }

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    QHBoxLayout *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    QLabel *title = new QLabel(i18n("Layer"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title, 1);

    m_closeButton = new QToolButton(this);
    const QIcon closeIcon =
        QIcon::fromTheme(QStringLiteral("window-close"), style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    if (!closeIcon.isNull()) {
        m_closeButton->setIcon(closeIcon);
        m_closeButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    } else {
        m_closeButton->setText(QStringLiteral("×"));
        m_closeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    m_closeButton->setAccessibleName(i18n("Close"));
    m_closeButton->setToolTip(i18n("Close"));
    m_closeButton->setAutoRaise(true);
    m_closeButton->setFixedSize(QSize(44, 44));
    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::hide);
    header->addWidget(m_closeButton, 0, Qt::AlignRight);

    root->addLayout(header);

    KisNodeManager *nodeManager = nullptr;
    {
        KisMainWindow *mainWindow = KisPart::instance() ? KisPart::instance()->currentMainwindow() : nullptr;
        KisViewManager *viewManager = mainWindow ? mainWindow->viewManager() : nullptr;
        nodeManager = viewManager ? viewManager->nodeManager() : nullptr;
    }

    // Opacity slider (Procreate-like quick access; affects active node).
    {
        QHBoxLayout *opacityRow = new QHBoxLayout();
        opacityRow->setContentsMargins(0, 0, 0, 0);
        opacityRow->setSpacing(10);

        QLabel *label = new QLabel(i18n("Opacity"), this);
        opacityRow->addWidget(label, 0, Qt::AlignVCenter);

        m_opacitySlider = new KisSliderSpinBox(this);
        m_opacitySlider->setRange(0, 100);
        m_opacitySlider->setSingleStep(1);
        m_opacitySlider->setSuffix(QStringLiteral("%"));
        m_opacitySlider->setMinimumHeight(48);
        m_opacitySlider->setToolTip(i18n("Adjust the opacity of the active layer."));

        KisNodeSP activeNode = nodeManager ? nodeManager->activeNode() : KisNodeSP();
        const int initialOpacity =
            activeNode ? qBound(0, qRound(activeNode->opacity() * 100.0 / 255.0), 100) : 100;
        m_opacitySlider->setValue(initialOpacity);
        m_opacitySlider->setEnabled(bool(nodeManager && activeNode));

        opacityRow->addWidget(m_opacitySlider, 1);
        root->addLayout(opacityRow);

        if (nodeManager) {
            connect(m_opacitySlider,
                    &KisSliderSpinBox::valueChanged,
                    this,
                    [nodeManager](int opacity) { nodeManager->nodeOpacityChanged(opacity); });
            connect(nodeManager,
                    &KisNodeManager::sigNodeActivated,
                    this,
                    [this](KisNodeSP node) {
                        if (!m_opacitySlider) {
                            return;
                        }
                        const int opacity =
                            node ? qBound(0, qRound(node->opacity() * 100.0 / 255.0), 100) : 100;
                        QSignalBlocker blocker(m_opacitySlider);
                        m_opacitySlider->setValue(opacity);
                        m_opacitySlider->setEnabled(bool(node));
                    },
                    Qt::UniqueConnection);
        }
    }

    QWidget *content = new QWidget(this);
    QGridLayout *grid = new QGridLayout(content);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    struct Entry {
        QString actionId;
        QString label;
        QString fallbackIconName;
    };

    const QList<Entry> entries = {
        {QStringLiteral("RenameCurrentLayer"), i18n("Rename"), QStringLiteral("edit-rename")},
        {QStringLiteral("layer_properties"), i18n("Properties"), QStringLiteral("document-properties")},
        {QStringLiteral("selectopaque"), i18n("Select Contents"), QStringLiteral("edit-select")},
        {QStringLiteral("toggle_layer_alpha_lock"), i18n("Alpha Lock"), QStringLiteral("object-locked")},
        {QStringLiteral("toggle_layer_lock"), i18n("Lock"), QStringLiteral("lock")},
        {QStringLiteral("duplicatelayer"), i18n("Duplicate"), QStringLiteral("edit-copy")},
        {QStringLiteral("merge_layer"), i18n("Merge Down"), QStringLiteral("object-group")},
        {QStringLiteral("add_new_paint_layer"), i18n("New Layer"), QStringLiteral("list-add")},
        {QStringLiteral("remove_layer"), i18n("Delete"), QStringLiteral("edit-delete")},
    };

    const int columns = 2;
    int row = 0;
    int col = 0;

    for (const Entry &entry : entries) {
        QToolButton *button = buildActionButton(content, entry.actionId, entry.label, entry.fallbackIconName);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(42, 42));
        button->setMinimumSize(QSize(160, 120));

        grid->addWidget(button, row, col);

        col++;
        if (col >= columns) {
            col = 0;
            row++;
        }
    }

    grid->setRowStretch(row + 1, 1);
    grid->setColumnStretch(columns, 1);

    root->addWidget(content, 1);
}

QToolButton *KisTouchLayerOptionsSheet::buildActionButton(QWidget *parent,
                                                          const QString &actionId,
                                                          const QString &labelOverride,
                                                          const QString &fallbackIconName)
{
    QToolButton *button = new QToolButton(parent);

    QAction *action = m_actionCollection ? m_actionCollection->action(actionId) : nullptr;
    if (action) {
        const QString label = labelOverride.isEmpty() ? stripAmpersands(action->text()) : labelOverride;
        const QIcon icon = !action->icon().isNull() ? action->icon() : QIcon::fromTheme(fallbackIconName);

        button->setIcon(icon);
        button->setText(label);
        button->setToolTip(stripAmpersands(action->text()));
        button->setCheckable(action->isCheckable());
        if (action->isCheckable()) {
            button->setChecked(action->isChecked());
            connect(action, &QAction::changed, button, [button, action]() { button->setChecked(action->isChecked()); });
        }

        connect(button, &QToolButton::clicked, this, [this, actionId]() { triggerAndClose(actionId); });
    } else {
        button->setIcon(QIcon::fromTheme(fallbackIconName));
        button->setText(labelOverride.isEmpty() ? i18n("…") : labelOverride);
        button->setToolTip(i18n("Missing action: %1", actionId));
        button->setEnabled(false);
    }

    return button;
}

void KisTouchLayerOptionsSheet::triggerAndClose(const QString &actionId)
{
    QAction *action = m_actionCollection ? m_actionCollection->action(actionId) : nullptr;
    if (action) {
        action->trigger();
    }
    hide();
}
