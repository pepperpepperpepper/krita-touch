/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_touch_actions_sheet.h"

#include <algorithm>

#include <QAction>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QScreen>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <kactioncollection.h>
#include <klocalizedstring.h>

#include "kis_touch_ui_metrics.h"

namespace {

QString stripAmpersands(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

class KisTouchCurrentPageStack final : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;

    QSize sizeHint() const override
    {
        if (QWidget *page = currentWidget()) {
            return page->sizeHint();
        }
        return QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        if (QWidget *page = currentWidget()) {
            return page->minimumSizeHint();
        }
        return QStackedWidget::minimumSizeHint();
    }
};

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

KisTouchActionsSheet::KisTouchActionsSheet(KisKActionCollection *actionCollection, QWidget *parent)
    : QFrame(parent)
    , m_actionCollection(actionCollection)
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    constexpr qreal kActionsUiScale = 0.8; // shrink ~20% to match other touch sheets

    const int listItemPaddingV = KisTouchUiMetrics::px(8.0 * kActionsUiScale, scale);
    const int listItemPaddingH = KisTouchUiMetrics::px(10.0 * kActionsUiScale, scale);
    const int listItemRadius = KisTouchUiMetrics::px(10.0 * kActionsUiScale, scale);
    const int toolButtonRadius = KisTouchUiMetrics::px(12.0 * kActionsUiScale, scale);
    const int toolButtonPadding = KisTouchUiMetrics::px(8.0 * kActionsUiScale, scale);

    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName(QStringLiteral("kisTouchActionsSheet"));

    setStyleSheet(QStringLiteral(
        "QFrame#kisTouchActionsSheet {"
        "  background: transparent;"
        "  color: rgb(240, 240, 240);"
        "}"
        "QLabel {"
        "  color: rgb(240, 240, 240);"
        "}"
        "QListWidget {"
        "  background: transparent;"
        "  border: 0px;"
        "  color: rgb(240, 240, 240);"
        "}"
        "QListWidget::item {"
        "  padding: %1px %2px;"
        "  border-radius: %3px;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: rgba(255, 255, 255, 70);"
        "}"
        "QToolButton {"
        "  color: rgb(240, 240, 240);"
        "  background: rgba(255, 255, 255, 60);"
        "  border: 1px solid rgba(255, 255, 255, 90);"
        "  border-radius: %4px;"
        "  padding: %5px;"
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
                      .arg(listItemPaddingV)
                      .arg(listItemPaddingH)
                      .arg(listItemRadius)
                      .arg(toolButtonRadius)
                      .arg(toolButtonPadding));

    rebuildUi();
}

KisTouchActionsSheet::~KisTouchActionsSheet() = default;

void KisTouchActionsSheet::setActionCollection(KisKActionCollection *actionCollection)
{
    if (m_actionCollection == actionCollection) {
        return;
    }

    m_actionCollection = actionCollection;
    rebuildUi();
}

void KisTouchActionsSheet::openAtGlobalPos(const QPoint &globalPos)
{
    rebuildUi();

    refitToCurrentCategory(globalPos);
    show();
    raise();
}

void KisTouchActionsSheet::paintEvent(QPaintEvent *event)
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

void KisTouchActionsSheet::setCurrentCategoryRow(int row)
{
    if (!m_categories) {
        rebuildUi();
    }
    if (!m_categories || m_categories->count() <= 0) {
        return;
    }

    row = std::clamp(row, 0, m_categories->count() - 1);
    m_categories->setCurrentRow(row);
}

void KisTouchActionsSheet::refitToCurrentCategory(const QPoint &referencePoint)
{
    if (m_categories && m_pages) {
        const QWidget *page = m_pages->currentWidget();
        const int pageHeightPx = page ? page->sizeHint().height() : 0;
        if (pageHeightPx > 0) {
            // Keep the popup compact by letting the content page drive the height.
            // The category list can scroll when it's taller than the current page.
            m_categories->setMaximumHeight(pageHeightPx);
        } else {
            m_categories->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    }

    if (layout()) {
        layout()->activate();
    }

    adjustSize();

    QScreen *screen = QGuiApplication::screenAt(referencePoint);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const qreal scale = KisTouchUiMetrics::scaleForScreen(screen);
    const int outerMargin = KisTouchUiMetrics::px(32.0, scale);

    if (screen) {
        QSize maxSize = screen->availableGeometry().size() - QSize(outerMargin, outerMargin);
        maxSize = maxSize.expandedTo(QSize(1, 1));
        resize(size().boundedTo(maxSize));
    }

    const QRect desiredRect(
        QPoint(referencePoint.x() - width() / 2, referencePoint.y() - height() / 2),
        size());
    const QRect finalRect = clampToScreen(desiredRect, referencePoint);
    move(finalRect.topLeft());
}

void KisTouchActionsSheet::rebuildUi()
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    constexpr qreal kActionsUiScale = 0.8; // shrink ~20% to match other touch sheets

    const int sheetMargin = KisTouchUiMetrics::px(12.0 * kActionsUiScale, scale);
    const int rootSpacing = KisTouchUiMetrics::px(8.0 * kActionsUiScale, scale);
    const int headerSpacing = KisTouchUiMetrics::px(6.0 * kActionsUiScale, scale);
    const int bodySpacing = KisTouchUiMetrics::px(10.0 * kActionsUiScale, scale);
    const int closeButtonPx = KisTouchUiMetrics::px(44.0, scale, 44);
    const int categoriesIconPx = KisTouchUiMetrics::px(26.0 * kActionsUiScale, scale, 20);
    const int categoriesWidthPx = KisTouchUiMetrics::px(180.0 * kActionsUiScale, scale, 110);
    const int categoryRowHeightPx = KisTouchUiMetrics::px(48.0 * kActionsUiScale, scale, 44);

    m_closeButton = nullptr;
    m_categories = nullptr;
    m_pages = nullptr;

    // This sheet is rebuilt frequently (on open, on action collection changes). Make
    // sure we fully tear down the previous widget tree; otherwise orphaned widgets
    // from the first (pre-layout) build can remain at (0,0) and appear as stray
    // "floating" labels over the UI.
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

    QLabel *title = new QLabel(i18n("Actions"), this);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() + 2.0);
    f.setBold(true);
    title->setFont(f);
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

    QHBoxLayout *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(bodySpacing);

    m_categories = new QListWidget(this);
    m_categories->setIconSize(QSize(categoriesIconPx, categoriesIconPx));
    m_categories->setFixedWidth(categoriesWidthPx);
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_categories->setSelectionMode(QAbstractItemView::SingleSelection);

    m_pages = new KisTouchCurrentPageStack(this);

    body->addWidget(m_categories);
    body->addWidget(m_pages, 1);
    root->addLayout(body, 1);

    struct Category {
        QString name;
        QString iconName;
        QList<QPair<QString, QString>> entries; // actionId, label override (optional)
    };

    const QList<Category> categories = {
        {i18n("Add"),
         QStringLiteral("list-add"),
         {
             {QStringLiteral("file_new"), i18n("New")},
             {QStringLiteral("file_open"), i18n("Open")},
             {QStringLiteral("file_import_file"), i18n("Import")},
         }},
        {i18n("Canvas"),
         QStringLiteral("transform-move"),
         {
             {QStringLiteral("view_show_canvas_only"), i18n("Canvas Only")},
             {QStringLiteral("fullscreen"), i18n("Fullscreen")},
             {QStringLiteral("reset_display"), i18n("Reset View")},
             {QStringLiteral("KisToolTransform"), i18n("Transform")},
         }},
        {i18n("Share"),
         QStringLiteral("document-export"),
         {
             {QStringLiteral("file_save"), i18n("Save")},
             {QStringLiteral("file_save_as"), i18n("Save As")},
             {QStringLiteral("file_export_file"), i18n("Export")},
             {QStringLiteral("file_export_advanced"), i18n("Export Adv")},
         }},
        {i18n("Prefs"),
         QStringLiteral("configure"),
         {
             {QStringLiteral("options_configure"), i18n("Settings")},
             {QStringLiteral("touch_mode_enabled"), i18n("Touch Mode")},
             {QStringLiteral("touch_right_handed"), i18n("Right-handed")},
             {QStringLiteral("touch_theme_light"), i18n("Light Theme")},
         }},
        {i18n("Gestures"),
         QStringLiteral("input-touchpad"),
         {
             {QStringLiteral("touch_painting_disabled"), i18n("Touch Paint Off")},
             {QStringLiteral("touch_painting_auto"), i18n("Touch Paint Auto")},
             {QStringLiteral("touch_painting_enabled"), i18n("Touch Paint On")},
             {QStringLiteral("touch_rotate_with_pinch"), i18n("Rotate with Pinch")},
             {QStringLiteral("touch_quick_pinch_to_fit_enabled"), i18n("Quick Pinch Fit")},
             {QStringLiteral("touch_quickshape_enabled"), i18n("QuickShape")},
             {QStringLiteral("touch_quickmenu_enabled"), i18n("QuickMenu")},
            {QStringLiteral("touch_quickmenu_configure"), i18n("QuickMenu Setup")},
             {QStringLiteral("touch_undo_redo_gestures_enabled"), i18n("Undo/Redo")},
             {QStringLiteral("touch_clipboard_gesture_enabled"), i18n("Copy/Paste")},
             {QStringLiteral("touch_clear_layer_gesture_enabled"), i18n("Clear Layer")},
             {QStringLiteral("touch_fullscreen_gesture_enabled"), i18n("Canvas Only")},
         }},
        {i18n("Help"),
         QStringLiteral("help-contents"),
         {
             {QStringLiteral("help_contents"), i18n("Handbook")},
             {QStringLiteral("help_about_app"), i18n("About")},
         }},
    };

    for (const Category &cat : categories) {
        QListWidgetItem *item = new QListWidgetItem(QIcon::fromTheme(cat.iconName), cat.name, m_categories);
        item->setSizeHint(QSize(categoriesWidthPx, categoryRowHeightPx));
        m_categories->addItem(item);

        QWidget *page = buildCategoryPage(cat.entries);
        m_pages->addWidget(page);
    }

    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_pages) {
            m_pages->setCurrentIndex(row);
        }

        if (isVisible()) {
            refitToCurrentCategory(geometry().center());
        }
    });

    if (m_categories->count() > 0) {
        m_categories->setCurrentRow(0);
    }
}

QWidget *KisTouchActionsSheet::buildCategoryPage(const QList<QPair<QString, QString>> &entries)
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    constexpr qreal kActionsUiScale = 0.8; // shrink ~20% to match other touch sheets

    const int gridSpacing = KisTouchUiMetrics::px(8.0 * kActionsUiScale, scale);
    const int buttonIconPx = KisTouchUiMetrics::px(38.0 * kActionsUiScale, scale, 18);
    const int buttonMinWidthPx = KisTouchUiMetrics::px(140.0 * kActionsUiScale, scale, 80);
    const int buttonMinHeightPx = KisTouchUiMetrics::px(104.0 * kActionsUiScale, scale, 72);
    const QSize buttonMinSize(buttonMinWidthPx, buttonMinHeightPx);

    QWidget *page = new QWidget(this);

    QGridLayout *grid = new QGridLayout(page);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(gridSpacing);
    grid->setVerticalSpacing(gridSpacing);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    const int columns = 3;
    int row = 0;
    int col = 0;

    for (const auto &entry : entries) {
        const QString actionId = entry.first;
        const QString labelOverride = entry.second;

        QAction *action = m_actionCollection ? m_actionCollection->action(actionId) : nullptr;
        QToolButton *button = new QToolButton(page);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(buttonIconPx, buttonIconPx));
        button->setMinimumSize(buttonMinSize);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        if (action) {
            button->setIcon(action->icon());
            const QString label = labelOverride.isEmpty() ? stripAmpersands(action->text()) : labelOverride;
            button->setText(label);
            button->setToolTip(stripAmpersands(action->text()));
            button->setCheckable(action->isCheckable());
            if (action->isCheckable()) {
                button->setChecked(action->isChecked());
                connect(action, &QAction::changed, button, [button, action]() { button->setChecked(action->isChecked()); });
            }
            connect(button, &QToolButton::clicked, this, [this, actionId]() { triggerAndClose(actionId); });
        } else {
            button->setIcon(QIcon());
            button->setText(labelOverride.isEmpty() ? i18n("…") : labelOverride);
            button->setToolTip(i18n("Missing action: %1", actionId));
            button->setEnabled(false);
        }

        grid->addWidget(button, row, col);

        col++;
        if (col >= columns) {
            col = 0;
            row++;
        }
    }

    return page;
}

void KisTouchActionsSheet::triggerAndClose(const QString &actionId)
{
    QAction *action = m_actionCollection ? m_actionCollection->action(actionId) : nullptr;
    if (action) {
        action->trigger();
    }
    hide();
}
