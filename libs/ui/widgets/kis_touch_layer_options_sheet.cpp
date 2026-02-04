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
#include <QPainter>
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

#include "kis_touch_ui_metrics.h"

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
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    const int toolButtonRadius = KisTouchUiMetrics::px(12.0, scale);
    const int toolButtonPadding = KisTouchUiMetrics::px(8.0, scale);

    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName(QStringLiteral("kisTouchLayerOptionsSheet"));

    setStyleSheet(QStringLiteral(
        "QFrame#kisTouchLayerOptionsSheet {"
        "  background: transparent;"
        "  color: rgb(240, 240, 240);"
        "}"
        "QLabel {"
        "  color: rgb(240, 240, 240);"
        "}"
        "QToolButton {"
        "  color: rgb(240, 240, 240);"
        "  background: rgba(255, 255, 255, 60);"
        "  border: 1px solid rgba(255, 255, 255, 90);"
        "  border-radius: %1px;"
        "  padding: %2px;"
        "}"
        "QToolButton:pressed {"
        "  background-color: rgba(255, 255, 255, 80);"
        "}"
        "QToolButton:checked {"
        "  background-color: rgba(90, 160, 255, 110);"
        "  border-color: rgba(90, 160, 255, 200);"
        "}"
        "QToolButton:checked:pressed {"
        "  background-color: rgba(90, 160, 255, 150);"
        "}")
                      .arg(toolButtonRadius)
                      .arg(toolButtonPadding));

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

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const qreal scale = KisTouchUiMetrics::scaleForScreen(screen);
    const int outerMargin = KisTouchUiMetrics::px(32.0, scale);

    // Let the layout drive the preferred size; this keeps the sheet compact and avoids
    // oversized tiles on tablets when the window is wider than the grid.
    QSize desiredSize = sizeHint();

    if (screen) {
        const QSize availSize = screen->availableGeometry().size();
        QSize maxSize = availSize - QSize(outerMargin, outerMargin);
        maxSize = maxSize.expandedTo(QSize(1, 1));

        desiredSize = desiredSize.boundedTo(maxSize);

        const int minWidth = KisTouchUiMetrics::px(360.0, scale);
        const int minHeight = KisTouchUiMetrics::px(300.0, scale);
        desiredSize.setWidth(qMin(maxSize.width(), qMax(desiredSize.width(), minWidth)));
        desiredSize.setHeight(qMin(maxSize.height(), qMax(desiredSize.height(), minHeight)));
    }

    resize(desiredSize);

    const QRect desiredRect(QPoint(globalPos.x() - width() / 2, globalPos.y() - height() / 2), size());
    const QRect finalRect = clampToScreen(desiredRect, globalPos);
    move(finalRect.topLeft());

    show();
    raise();
}

void KisTouchLayerOptionsSheet::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = KisTouchUiMetrics::px(16.0, scale);

    const QColor bg(30, 30, 30, 255);
    const QColor border(255, 255, 255, 90);

    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(r, radius, radius);
}

void KisTouchLayerOptionsSheet::rebuildUi()
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    constexpr qreal kLayerOptionsUiScale = 0.8; // shrink ~20% to match the compact Actions sheet

    const int sheetMargin = KisTouchUiMetrics::px(12.0 * kLayerOptionsUiScale, scale);
    const int rootSpacing = KisTouchUiMetrics::px(8.0 * kLayerOptionsUiScale, scale);
    const int headerSpacing = KisTouchUiMetrics::px(6.0 * kLayerOptionsUiScale, scale);
    const int closeButtonPx = KisTouchUiMetrics::px(44.0, scale, 44);
    const int opacityMinHeightPx = KisTouchUiMetrics::px(48.0, scale, 44);
    const int rowSpacing = KisTouchUiMetrics::px(8.0 * kLayerOptionsUiScale, scale);
    const int gridSpacing = KisTouchUiMetrics::px(8.0 * kLayerOptionsUiScale, scale);
    const int buttonIconPx = KisTouchUiMetrics::px(38.0 * kLayerOptionsUiScale, scale, 18);
    const int buttonMinWidthPx = KisTouchUiMetrics::px(140.0 * kLayerOptionsUiScale, scale, 80);
    const int buttonMinHeightPx = KisTouchUiMetrics::px(104.0 * kLayerOptionsUiScale, scale, 72);
    const QSize buttonMinSize(buttonMinWidthPx, buttonMinHeightPx);

    m_opacitySlider = nullptr;
    m_closeButton = nullptr;

    // This sheet is rebuilt frequently (on open, when action collection changes, etc).
    // Ensure we fully delete the previous widget tree; otherwise orphaned widgets from
    // the first (pre-layout) build can remain at (0,0) and show up as stray overlays.
    if (QLayout *oldLayout = layout()) {
        delete oldLayout;
    }
    const auto directChildren = findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : directChildren) {
        delete w;
    }

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(sheetMargin, sheetMargin, sheetMargin, sheetMargin);
    root->setSpacing(rootSpacing);

    QHBoxLayout *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(headerSpacing);

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
    m_closeButton->setFixedSize(QSize(closeButtonPx, closeButtonPx));
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
        opacityRow->setSpacing(rowSpacing);

        QLabel *label = new QLabel(i18n("Opacity"), this);
        opacityRow->addWidget(label, 0, Qt::AlignVCenter);

        m_opacitySlider = new KisSliderSpinBox(this);
        m_opacitySlider->setRange(0, 100);
        m_opacitySlider->setSingleStep(1);
        m_opacitySlider->setSuffix(QStringLiteral("%"));
        m_opacitySlider->setMinimumHeight(opacityMinHeightPx);
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
    grid->setHorizontalSpacing(gridSpacing);
    grid->setVerticalSpacing(gridSpacing);

    struct Entry {
        QString actionId;
        QString label;
        QString fallbackIconName;
    };

    const QList<Entry> entries = {
        {QStringLiteral("RenameCurrentLayer"), i18n("Rename"), QStringLiteral("edit-rename")},
        {QStringLiteral("layer_properties"), i18n("Properties"), QStringLiteral("document-properties")},
        {QStringLiteral("selectopaque"), i18n("Select"), QStringLiteral("edit-select")},
        {QStringLiteral("toggle_layer_alpha_lock"), i18n("Alpha Lock"), QStringLiteral("object-locked")},
        {QStringLiteral("toggle_layer_lock"), i18n("Lock"), QStringLiteral("lock")},
        {QStringLiteral("duplicatelayer"), i18n("Duplicate"), QStringLiteral("edit-copy")},
        {QStringLiteral("merge_layer"), i18n("Merge Down"), QStringLiteral("object-group")},
        {QStringLiteral("add_new_paint_layer"), i18n("New Layer"), QStringLiteral("list-add")},
        {QStringLiteral("remove_layer"), i18n("Delete"), QStringLiteral("edit-delete")},
    };

    const int columns = 3;
    int row = 0;
    int col = 0;

    for (const Entry &entry : entries) {
        QToolButton *button = buildActionButton(content, entry.actionId, entry.label, entry.fallbackIconName);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(buttonIconPx, buttonIconPx));
        button->setMinimumSize(buttonMinSize);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

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
