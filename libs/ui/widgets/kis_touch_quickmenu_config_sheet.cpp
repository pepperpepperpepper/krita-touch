/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_touch_quickmenu_config_sheet.h"

#include <algorithm>

#include <QAction>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <kactioncollection.h>
#include <klocalizedstring.h>

#include <kis_config.h>

#include "kis_touch_ui_metrics.h"
#include "kis_touch_quickmenu_icon_utils.h"

namespace {

QString stripAmpersands(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

QString quickMenuSlotLabelForActionId(const QString &actionId, const QAction *action)
{
    // Keep slot labels compact so the slot grid stays aligned with the
    // candidate grid and the sheet doesn't grow to accommodate long action
    // names (e.g. "Touch Selection Tool").
    if (actionId == QLatin1String("edit_undo")) {
        return i18n("Undo");
    }
    if (actionId == QLatin1String("edit_redo")) {
        return i18n("Redo");
    }
    if (actionId == QLatin1String("KisToolSelectTouch")) {
        return i18n("Select");
    }
    if (actionId == QLatin1String("KisToolTransform")) {
        return i18n("Transform");
    }
    if (actionId == QLatin1String("deselect")) {
        return i18n("Deselect");
    }
    if (actionId == QLatin1String("view_show_canvas_only")) {
        return i18n("Canvas Only");
    }

    return action ? stripAmpersands(action->text()) : i18n("…");
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

struct CandidateAction {
    QString id;
    QString labelOverride;
};

QList<CandidateAction> defaultCandidates()
{
    return QList<CandidateAction>{
        {QString(), i18n("Empty")},
        {QStringLiteral("edit_undo"), i18n("Undo")},
        {QStringLiteral("edit_redo"), i18n("Redo")},
        {QStringLiteral("deselect"), i18n("Deselect")},
        {QStringLiteral("view_show_canvas_only"), i18n("Canvas Only")},
        {QStringLiteral("touch_actions_sheet"), i18n("Actions")},
        {QStringLiteral("KisToolSelectTouch"), i18n("Select")},
        {QStringLiteral("KisToolTransform"), i18n("Transform")},
        {QStringLiteral("KritaShape/KisToolBrush"), i18n("Brush")},
        {QStringLiteral("eraser_preset_action"), i18n("Eraser")},
        {QStringLiteral("touch_copypaste_overlay"), i18n("Copy/Paste")},
        {QStringLiteral("clear"), i18n("Clear Layer")},
        {QStringLiteral("reset_display"), i18n("Reset View")},
        {QStringLiteral("activateNextLayer"), i18n("Next Layer")},
        {QStringLiteral("activatePreviousLayer"), i18n("Prev Layer")},
    };
}

} // namespace

KisTouchQuickMenuConfigSheet::KisTouchQuickMenuConfigSheet(KisKActionCollection *actionCollection, QWidget *parent)
    : QFrame(parent)
    , m_actionCollection(actionCollection)
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    const int toolButtonRadius = KisTouchUiMetrics::px(12.0, scale);
    const int toolButtonPadding = KisTouchUiMetrics::px(8.0, scale);

    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName(QStringLiteral("kisTouchQuickMenuConfigSheet"));

    setStyleSheet(QStringLiteral(
        "QFrame#kisTouchQuickMenuConfigSheet {"
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
        "QToolButton:checked {"
        "  background-color: rgba(90, 160, 255, 110);"
        "  border-color: rgba(90, 160, 255, 200);"
        "}"
        "QToolButton:pressed {"
        "  background-color: rgba(255, 255, 255, 80);"
        "}"
        "QToolButton:checked:pressed {"
        "  background-color: rgba(90, 160, 255, 150);"
        "}")
                      .arg(toolButtonRadius)
                      .arg(toolButtonPadding));

    rebuildUi();
}

KisTouchQuickMenuConfigSheet::~KisTouchQuickMenuConfigSheet() = default;

void KisTouchQuickMenuConfigSheet::setActionCollection(KisKActionCollection *actionCollection)
{
    if (m_actionCollection == actionCollection) {
        return;
    }

    m_actionCollection = actionCollection;
    rebuildUi();
}

void KisTouchQuickMenuConfigSheet::openAtGlobalPos(const QPoint &globalPos, int initialSlot)
{
    rebuildUi();
    reloadFromConfig();
    updateSlotButtons();
    if (initialSlot >= 0) {
        setSelectedSlot(initialSlot);
    }

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const qreal scale = KisTouchUiMetrics::scaleForScreen(screen);
    const int outerMargin = KisTouchUiMetrics::px(32.0, scale);

    // Let the layout drive the preferred size; clamp so we don't overflow the
    // viewport or become unusably small.
    QSize desiredSize = sizeHint();

    if (screen) {
        const QSize availSize = screen->availableGeometry().size();
        QSize maxSize = availSize - QSize(outerMargin, outerMargin);
        maxSize = maxSize.expandedTo(QSize(1, 1));

        desiredSize = desiredSize.boundedTo(maxSize);
    }

    resize(desiredSize);

    const QRect desiredRect(
        QPoint(globalPos.x() - width() / 2, globalPos.y() - height() / 2),
        size());
    const QRect finalRect = clampToScreen(desiredRect, globalPos);

    move(finalRect.topLeft());
    show();
    raise();
}

void KisTouchQuickMenuConfigSheet::paintEvent(QPaintEvent *event)
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

void KisTouchQuickMenuConfigSheet::rebuildUi()
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    constexpr qreal kQuickMenuUiScale = 0.8; // shrink ~20% relative to other touch sheets

    const int sheetMargin = KisTouchUiMetrics::px(12.0 * kQuickMenuUiScale, scale);
    const int rootSpacing = KisTouchUiMetrics::px(8.0 * kQuickMenuUiScale, scale);
    const int headerSpacing = KisTouchUiMetrics::px(6.0 * kQuickMenuUiScale, scale);
    const int headerButtonMinHeightPx = KisTouchUiMetrics::px(44.0, scale, 44);
    const int headerButtonMinSizePx = KisTouchUiMetrics::px(44.0, scale, 44);
    const int gridSpacing = KisTouchUiMetrics::px(8.0 * kQuickMenuUiScale, scale);
    const int slotIconPx = KisTouchUiMetrics::px(38.0 * kQuickMenuUiScale, scale, 18);
    const int slotMinWidthPx = KisTouchUiMetrics::px(140.0 * kQuickMenuUiScale, scale, 80);
    const int slotMinHeightPx = KisTouchUiMetrics::px(104.0 * kQuickMenuUiScale, scale, 72);
    const QSize slotMinSize(slotMinWidthPx, slotMinHeightPx);
    const int candidateMinWidthPx = KisTouchUiMetrics::px(140.0 * kQuickMenuUiScale, scale, 80);
    const int candidateMinHeightPx = KisTouchUiMetrics::px(104.0 * kQuickMenuUiScale, scale, 72);
    const QSize candidateMinSize(candidateMinWidthPx, candidateMinHeightPx);

    m_slotButtons.clear();
    m_resetButton = nullptr;
    m_closeButton = nullptr;
    m_selectedSlot = 0;
    m_slotGroup = nullptr;

    // This sheet is rebuilt on open and when config changes. Ensure we fully
    // delete the previous widget tree; otherwise orphaned labels from the
    // first (pre-layout) build can remain at (0,0) and show up as stray
    // "floating" titles over the UI.
    if (QLayout *oldLayout = layout()) {
        delete oldLayout;
    }
    const auto directChildren = findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : directChildren) {
        delete w;
    }

    m_slotActionIds = KisConfig(true).touchQuickMenuActionIds();

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(sheetMargin, sheetMargin, sheetMargin, sheetMargin);
    root->setSpacing(rootSpacing);

    // Header
    {
        QHBoxLayout *header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(headerSpacing);

        QLabel *title = new QLabel(i18n("QuickMenu"), this);
        QFont f = title->font();
        f.setPointSizeF(f.pointSizeF() + 2.0);
        f.setBold(true);
        title->setFont(f);
        header->addWidget(title, 1);

        m_resetButton = new QToolButton(this);
        m_resetButton->setText(i18n("Reset"));
        m_resetButton->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
        m_resetButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_resetButton->setMinimumHeight(headerButtonMinHeightPx);
        header->addWidget(m_resetButton);

        m_closeButton = new QToolButton(this);
        const QIcon closeIcon =
            QIcon::fromTheme(QStringLiteral("window-close"), style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        if (!closeIcon.isNull()) {
            m_closeButton->setIcon(closeIcon);
        } else {
            m_closeButton->setText(i18n("Close"));
        }
        m_closeButton->setToolTip(i18n("Close"));
        m_closeButton->setMinimumSize(QSize(headerButtonMinSizePx, headerButtonMinSizePx));
        header->addWidget(m_closeButton);

        root->addLayout(header);
    }

    QLabel *hint = new QLabel(i18n("Select a slot, then choose an action."), this);
    hint->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 200);"));
    root->addWidget(hint);

    // Slot buttons
    QWidget *slotsPanel = new QWidget(this);
    QGridLayout *slotsGrid = new QGridLayout(slotsPanel);
    slotsGrid->setContentsMargins(0, 0, 0, 0);
    slotsGrid->setHorizontalSpacing(gridSpacing);
    slotsGrid->setVerticalSpacing(gridSpacing);

    m_slotGroup = new QButtonGroup(this);
    m_slotGroup->setExclusive(true);

    for (int i = 0; i < 6; ++i) {
        QToolButton *button = new QToolButton(slotsPanel);
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(slotIconPx, slotIconPx));
        button->setMinimumSize(slotMinSize);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setAutoRaise(false);
        button->setText(i18n("Slot %1", i + 1));
        button->setToolTip(i18n("QuickMenu slot %1", i + 1));

        m_slotGroup->addButton(button, i);
        m_slotButtons.append(button);
        slotsGrid->addWidget(button, i / 3, i % 3);
    }

    // Keep the 3x2 grid packed like the candidate actions grid below.
    // Without an explicit "stretch" column/row, QGridLayout distributes extra
    // space between the real columns/rows, making the top slot tiles look
    // misaligned vs the candidate grid and unnecessarily large.
    slotsGrid->setRowStretch(2, 1);
    slotsGrid->setColumnStretch(3, 1);

    root->addWidget(slotsPanel);

    // Candidate actions
    QWidget *actionsPanel = new QWidget(this);
    QGridLayout *actionsGrid = new QGridLayout(actionsPanel);
    actionsGrid->setContentsMargins(0, 0, 0, 0);
    actionsGrid->setHorizontalSpacing(gridSpacing);
    actionsGrid->setVerticalSpacing(gridSpacing);

    const QList<CandidateAction> candidates = defaultCandidates();
    const int columns = 3;
    int row = 0;
    int col = 0;

    for (const CandidateAction &candidate : candidates) {
        QToolButton *button = new QToolButton(actionsPanel);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(slotIconPx, slotIconPx));
        button->setMinimumSize(candidateMinSize);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setAutoRaise(true);

        if (candidate.id.isEmpty()) {
            button->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
            button->setText(candidate.labelOverride);
            connect(button, &QToolButton::clicked, this, [this]() {
                if (m_selectedSlot < 0 || m_selectedSlot >= m_slotActionIds.size()) {
                    return;
                }
                m_slotActionIds[m_selectedSlot].clear();
                persistToConfig();
                updateSlotButtons();
            });
        } else {
            QAction *action = m_actionCollection ? m_actionCollection->action(candidate.id) : nullptr;
            if (action) {
                button->setIcon(KisTouchQuickMenuIconUtils::iconForActionId(candidate.id, action, QSize(slotIconPx, slotIconPx)));
                button->setText(candidate.labelOverride.isEmpty() ? stripAmpersands(action->text()) : candidate.labelOverride);
                button->setToolTip(stripAmpersands(action->text()));
                connect(button, &QToolButton::clicked, this, [this, id = candidate.id]() {
                    if (m_selectedSlot < 0 || m_selectedSlot >= m_slotActionIds.size()) {
                        return;
                    }
                    m_slotActionIds[m_selectedSlot] = id;
                    persistToConfig();
                    updateSlotButtons();
                });
            } else {
                button->setText(candidate.labelOverride.isEmpty() ? i18n("…") : candidate.labelOverride);
                button->setToolTip(i18n("Missing action: %1", candidate.id));
                button->setEnabled(false);
            }
        }

        actionsGrid->addWidget(button, row, col);

        col++;
        if (col >= columns) {
            col = 0;
            row++;
        }
    }

    actionsGrid->setRowStretch(row + 1, 1);
    actionsGrid->setColumnStretch(columns, 1);

    root->addWidget(actionsPanel, 1);

    connect(m_closeButton, &QToolButton::clicked, this, &QWidget::hide);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_slotGroup, &QButtonGroup::idClicked, this, [this](int id) { setSelectedSlot(id); });
#else
    connect(m_slotGroup,
            QOverload<int>::of(&QButtonGroup::buttonClicked),
            this,
            [this](int id) { setSelectedSlot(id); });
#endif

    connect(m_resetButton, &QToolButton::clicked, this, [this]() {
        m_slotActionIds = KisConfig::defaultTouchQuickMenuActionIds();
        persistToConfig();
        updateSlotButtons();
        setSelectedSlot(0);
    });

    setSelectedSlot(0);
    updateSlotButtons();
}

void KisTouchQuickMenuConfigSheet::reloadFromConfig()
{
    m_slotActionIds = KisConfig(true).touchQuickMenuActionIds();
    while (m_slotActionIds.size() < 6) {
        m_slotActionIds.append(QString());
    }
    if (m_slotActionIds.size() > 6) {
        m_slotActionIds = m_slotActionIds.mid(0, 6);
    }
}

void KisTouchQuickMenuConfigSheet::persistToConfig()
{
    KisConfig cfg(false);
    cfg.setTouchQuickMenuActionIds(m_slotActionIds);
}

void KisTouchQuickMenuConfigSheet::updateSlotButtons()
{
    for (int i = 0; i < m_slotButtons.size() && i < m_slotActionIds.size(); ++i) {
        QToolButton *button = m_slotButtons[i];
        if (!button) {
            continue;
        }

        const QSize iconSize = button->iconSize();
        const QString actionId = m_slotActionIds.at(i);
        QAction *action = (!actionId.isEmpty() && m_actionCollection) ? m_actionCollection->action(actionId) : nullptr;

        if (action) {
            button->setIcon(KisTouchQuickMenuIconUtils::iconForActionId(actionId, action, iconSize));
            button->setText(quickMenuSlotLabelForActionId(actionId, action));
        } else if (actionId.isEmpty()) {
            button->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
            button->setText(i18n("Empty"));
        } else {
            button->setIcon(QIcon());
            button->setText(i18n("…"));
        }
    }
}

void KisTouchQuickMenuConfigSheet::setSelectedSlot(int slot)
{
    if (m_slotButtons.isEmpty()) {
        return;
    }

    slot = std::clamp(slot, 0, m_slotButtons.size() - 1);
    m_selectedSlot = slot;

    if (QAbstractButton *button = m_slotGroup ? m_slotGroup->button(slot) : nullptr) {
        button->setChecked(true);
    }
}
