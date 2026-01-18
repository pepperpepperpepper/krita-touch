/*
 *  SPDX-FileCopyrightText: 2026
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_tool_select_touch.h"

#include <algorithm>

#include <QGridLayout>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolButton>

#include <KisOptionButtonStrip.h>
#include <KisOptionCollectionWidget.h>
#include <KoGroupButton.h>
#include <KisOptimizedBrushOutline.h>
#include <kis_slider_spin_box.h>

#include <KisViewManager.h>
#include <kactioncollection.h>
#include <canvas/kis_canvas2.h>
#include <kis_cursor.h>
#include <kis_debug.h>
#include <kis_default_bounds.h>
#include <kis_fill_painter.h>
#include <kis_image.h>
#include <kis_painter.h>
#include <kis_pixel_selection.h>
#include <kis_selection_manager.h>
#include <kis_selection_options.h>
#include <kis_selection_tool_helper.h>

#include <KisCursorOverrideLock.h>

#include <kis_command_utils.h>
#include <kis_selection_filters.h>

#include <klocalizedstring.h>
#include <ksharedconfig.h>

#include "kis_algebra_2d.h"

namespace {

constexpr qreal kOutlinePaddingPx = 6.0;
constexpr qreal kTapMaxDistancePx = 6.0;
constexpr qreal kClosePolygonDistancePx = 12.0;

QString stripAmpersands(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

QRectF paddedRect(const QRectF &rc, qreal pad)
{
    return rc.adjusted(-pad, -pad, pad, pad);
}

} // namespace

KisToolSelectTouch::KisToolSelectTouch(KoCanvasBase *canvas)
    : KisToolSelect(
        canvas,
        KisCursor::load("tool_outline_selection_cursor.png", 5, 5),
        i18n("Touch Selection Tool"))
{
    setObjectName("tool_select_touch");
}

KisToolSelectTouch::~KisToolSelectTouch() = default;

void KisToolSelectTouch::activate(const QSet<KoShape *> &shapes)
{
    KisToolSelect::activate(shapes);
    m_configGroup = KSharedConfig::openConfig()->group(toolId());
    loadSettings();
    updateMethodUi();
    updateMethodDependentUi();
    updateSelectionActionUi();
    updateFeatherUi();
    updateSavedSelectionUi();
}

void KisToolSelectTouch::deactivate()
{
    m_draggingShape = false;
    m_draggingFreehand = false;
    m_polygonActive = false;
    m_autoAdjustingThreshold = false;
    m_freehandPoints.clear();
    m_polygonPoints.clear();
    endSelectInteraction();

    KisToolSelect::deactivate();
}

QWidget *KisToolSelectTouch::createOptionWidget()
{
    KisToolSelect::createOptionWidget();
    KisSelectionOptions *selectionWidget = selectionOptionWidget();
    if (!selectionWidget) {
        return nullptr;
    }

    // Hide the default selection action + adjustments sections and provide a
    // touch-first surface that matches Procreate's selection UI.
    selectionWidget->setActionSectionVisible(false);
    selectionWidget->setAdjustmentsSectionVisible(false);

    // Selection method (Automatic / Freehand / Rectangle / Ellipse)
    m_methodButtons = new KisOptionButtonStrip;
    m_buttonAutomatic =
        m_methodButtons->addButton(KisIconUtils::loadIcon("tool_contiguous_selection"));
    m_buttonFreehand =
        m_methodButtons->addButton(KisIconUtils::loadIcon("tool_outline_selection"));
    m_buttonRectangle =
        m_methodButtons->addButton(KisIconUtils::loadIcon("tool_rect_selection"));
    m_buttonEllipse =
        m_methodButtons->addButton(KisIconUtils::loadIcon("tool_elliptical_selection"));

    m_buttonAutomatic->setToolTip(i18n("Automatic selection"));
    m_buttonFreehand->setToolTip(i18n("Freehand selection"));
    m_buttonRectangle->setToolTip(i18n("Rectangular selection"));
    m_buttonEllipse->setToolTip(i18n("Elliptical selection"));

    const auto configureStripButton = [](KoGroupButton *button) {
        if (!button) {
            return;
        }
        button->setIconSize(QSize(32, 32));
        button->setMinimumSize(QSize(56, 56));
    };

    configureStripButton(m_buttonAutomatic);
    configureStripButton(m_buttonFreehand);
    configureStripButton(m_buttonRectangle);
    configureStripButton(m_buttonEllipse);

    KisOptionCollectionWidgetWithHeader *sectionSelectionMethod =
        new KisOptionCollectionWidgetWithHeader(i18nc(
            "The selection method section label in Touch Selection Tool options",
            "Selection method"));
    sectionSelectionMethod->setPrimaryWidget(m_methodButtons);

    // Selection action (Replace / Intersect / Add / Subtract / Symmetric difference)
    m_sectionSelectionAction = new KisOptionCollectionWidgetWithHeader(i18nc(
        "The selection action section label in Touch Selection Tool options",
        "Selection action"));
    m_actionButtons = new KisOptionButtonStrip;

    m_buttonActionReplace = m_actionButtons->addButton(KisIconUtils::loadIcon("selection_replace"));
    m_buttonActionAdd = m_actionButtons->addButton(KisIconUtils::loadIcon("selection_add"));
    m_buttonActionSubtract = m_actionButtons->addButton(KisIconUtils::loadIcon("selection_subtract"));
    m_buttonActionIntersect = m_actionButtons->addButton(KisIconUtils::loadIcon("selection_intersect"));
    m_buttonActionSymmetricDifference = m_actionButtons->addButton(KisIconUtils::loadIcon("selection_symmetric_difference"));

    m_buttonActionReplace->setToolTip(i18nc("@info:tooltip", "Replace"));
    m_buttonActionAdd->setToolTip(i18nc("@info:tooltip", "Add"));
    m_buttonActionSubtract->setToolTip(i18nc("@info:tooltip", "Subtract"));
    m_buttonActionIntersect->setToolTip(i18nc("@info:tooltip", "Intersect"));
    m_buttonActionSymmetricDifference->setToolTip(i18nc("@info:tooltip", "Symmetric Difference"));

    configureStripButton(m_buttonActionReplace);
    configureStripButton(m_buttonActionAdd);
    configureStripButton(m_buttonActionSubtract);
    configureStripButton(m_buttonActionIntersect);
    configureStripButton(m_buttonActionSymmetricDifference);

    m_sectionSelectionAction->setPrimaryWidget(m_actionButtons);

    // Feather (touch-friendly slider)
    m_sectionFeather = new KisOptionCollectionWidgetWithHeader(i18nc(
        "The selection feather section label in Touch Selection Tool options",
        "Feather"));
    m_sliderFeather = new KisSliderSpinBox;
    m_sliderFeather->setPrefix(i18nc(
        "The 'feather' spinbox prefix in Touch Selection Tool options",
        "Feather: "));
    m_sliderFeather->setRange(0, 400);
    m_sliderFeather->setSoftRange(0, 40);
    m_sliderFeather->setSingleStep(1);
    m_sliderFeather->setSuffix(i18n(" px"));
    m_sliderFeather->setMinimumHeight(48);
    m_sliderFeather->setToolTip(i18n(
        "Feather softens selection edges. Higher values produce a softer edge."));
    m_sectionFeather->appendWidget("sliderFeather", m_sliderFeather);

    // Automatic selection options
    m_sectionAutomatic = new KisOptionCollectionWidgetWithHeader(i18nc(
        "The automatic selection section label in Touch Selection Tool options",
        "Automatic"));
    m_sliderThreshold = new KisSliderSpinBox;
    m_sliderThreshold->setPrefix(i18nc(
        "The 'threshold' spinbox prefix in Touch Selection Tool options",
        "Threshold: "));
    m_sliderThreshold->setRange(1, 100);
    m_sliderThreshold->setSingleStep(1);
    m_sliderThreshold->setToolTip(i18n(
        "Set the color similarity tolerance of the selection. "
        "Increasing threshold increases the range of similar colors to be selected."));
    m_sectionAutomatic->appendWidget("sliderThreshold", m_sliderThreshold);

    selectionWidget->insertWidget(0, "sectionSelectionMethod", sectionSelectionMethod);
    selectionWidget->insertWidget(1, "sectionSelectionAction", m_sectionSelectionAction);
    selectionWidget->insertWidget(2, "sectionFeather", m_sectionFeather);
    selectionWidget->insertWidget(3, "sectionAutomatic", m_sectionAutomatic);

    // Procreate-like operations (invert, fill, clear, copy/cut to new layer)
    {
        KisOptionCollectionWidgetWithHeader *sectionOperations =
            new KisOptionCollectionWidgetWithHeader(i18nc(
                "The selection operations section label in Touch Selection Tool options",
                "Operations"));

        QWidget *operationsGrid = new QWidget(sectionOperations);
        QGridLayout *grid = new QGridLayout(operationsGrid);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);

        KisKActionCollection *actions = nullptr;
        if (KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(canvas())) {
            if (KisViewManager *viewManager = kisCanvas->viewManager()) {
                actions = viewManager->actionCollection();
            }
        }

        auto addActionButton = [&](const QString &actionId, const QString &labelOverride) {
            QAction *action = actions ? actions->action(actionId) : nullptr;
            QToolButton *button = new QToolButton(operationsGrid);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(36, 36));
            button->setMinimumSize(QSize(160, 110));

            if (action) {
                button->setIcon(action->icon());
                const QString label =
                    labelOverride.isEmpty() ? stripAmpersands(action->text()) : labelOverride;
                const QString toolTip =
                    action->toolTip().isEmpty() ? stripAmpersands(action->text()) : stripAmpersands(action->toolTip());
                button->setText(label);
                button->setToolTip(toolTip);
                connect(button, &QToolButton::clicked, action, &QAction::trigger);
            } else {
                button->setText(labelOverride.isEmpty() ? i18n("…") : labelOverride);
                button->setToolTip(i18n("Missing action: %1", actionId));
                button->setEnabled(false);
            }

            const int index = grid->count();
            const int row = index / 2;
            const int col = index % 2;
            grid->addWidget(button, row, col);
        };

        addActionButton(QStringLiteral("invert_selection"), i18n("Invert"));
        addActionButton(QStringLiteral("deselect"), i18n("Deselect"));
        addActionButton(QStringLiteral("fill_selection_foreground_color"), i18n("Color Fill"));
        addActionButton(QStringLiteral("clear"), i18n("Clear"));
        addActionButton(QStringLiteral("copy_selection_to_new_layer"), i18n("Copy && Paste"));
        addActionButton(QStringLiteral("cut_selection_to_new_layer"), i18n("Cut && Paste"));

        grid->setRowStretch(3, 1);
        grid->setColumnStretch(2, 1);

        sectionOperations->setPrimaryWidget(operationsGrid);
        selectionWidget->insertWidget(4, "sectionTouchOperations", sectionOperations);
    }

    // Procreate-like Save & Load (v2: single-slot, in-memory)
    {
        m_sectionSavedSelection = new KisOptionCollectionWidgetWithHeader(i18nc(
            "The saved selection section label in Touch Selection Tool options",
            "Save & Load"));

        QWidget *savedGrid = new QWidget(m_sectionSavedSelection);
        QGridLayout *grid = new QGridLayout(savedGrid);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);

        const auto configureButton = [](QToolButton *button) {
            if (!button) {
                return;
            }
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(36, 36));
            button->setMinimumSize(QSize(160, 110));
        };

        m_buttonSaveSelection = new QToolButton(savedGrid);
        m_buttonSaveSelection->setIcon(KisIconUtils::loadIcon("document-save"));
        m_buttonSaveSelection->setText(i18nc("@action:button", "Save"));
        m_buttonSaveSelection->setToolTip(i18n("Save the current selection for later reuse."));
        configureButton(m_buttonSaveSelection);

        m_buttonLoadSelection = new QToolButton(savedGrid);
        m_buttonLoadSelection->setIcon(KisIconUtils::loadIcon("document-open"));
        m_buttonLoadSelection->setText(i18nc("@action:button", "Load"));
        m_buttonLoadSelection->setToolTip(i18n("Replace the current selection with the saved selection."));
        configureButton(m_buttonLoadSelection);

        grid->addWidget(m_buttonSaveSelection, 0, 0);
        grid->addWidget(m_buttonLoadSelection, 0, 1);
        grid->setRowStretch(1, 1);
        grid->setColumnStretch(2, 1);

        m_sectionSavedSelection->setPrimaryWidget(savedGrid);
        selectionWidget->insertWidget(5, "sectionSavedSelection", m_sectionSavedSelection);

        connect(m_buttonSaveSelection, &QToolButton::clicked, this, &KisToolSelectTouch::slot_saveSelectionClicked);
        connect(m_buttonLoadSelection, &QToolButton::clicked, this, &KisToolSelectTouch::slot_loadSelectionClicked);
    }

    // Load saved config values into widgets
    updateMethodUi();
    if (m_sliderThreshold) {
        m_sliderThreshold->setValue(m_threshold);
    }
    updateMethodDependentUi();
    updateSelectionActionUi();
    updateFeatherUi();
    updateSavedSelectionUi();

    connect(m_methodButtons,
            SIGNAL(buttonToggled(KoGroupButton*, bool)),
            this,
            SLOT(slot_methodButtonToggled(KoGroupButton*, bool)));
    connect(m_actionButtons,
            SIGNAL(buttonToggled(KoGroupButton*, bool)),
            this,
            SLOT(slot_actionButtonToggled(KoGroupButton*, bool)));
    connect(m_sliderThreshold,
            SIGNAL(valueChanged(int)),
            this,
            SLOT(slot_thresholdChanged(int)));
    connect(m_sliderFeather,
            SIGNAL(valueChanged(int)),
            this,
            SLOT(slot_featherChanged(int)));

    connect(selectionWidget,
            &KisSelectionOptions::actionChanged,
            this,
            [this](SelectionAction) {
                updateSelectionActionUi();
            });
    connect(selectionWidget,
            &KisSelectionOptions::featherSelectionChanged,
            this,
            [this](int) {
                updateFeatherUi();
            });

    return selectionWidget;
}

void KisToolSelectTouch::loadSettings()
{
    const int method = m_configGroup.readEntry<int>("touchSelectionMethod", int(SelectionMethod::Freehand));
    if (method >= int(SelectionMethod::Automatic) && method <= int(SelectionMethod::Ellipse)) {
        m_method = SelectionMethod(method);
    } else {
        m_method = SelectionMethod::Freehand;
    }

    m_threshold = m_configGroup.readEntry<int>("threshold", 8);
    m_threshold = std::clamp(m_threshold, 1, 100);
}

void KisToolSelectTouch::setMethod(SelectionMethod method, bool persist)
{
    if (m_method == method) {
        return;
    }

    // Cancel any in-flight interaction when switching methods.
    if (m_draggingShape || m_draggingFreehand || m_polygonActive || m_autoAdjustingThreshold) {
        m_draggingShape = false;
        m_draggingFreehand = false;
        m_polygonActive = false;
        m_autoAdjustingThreshold = false;
        m_freehandPoints.clear();
        m_polygonPoints.clear();
        endSelectInteraction();
        updateCanvasPixelRect(image()->bounds());
    }

    m_method = method;
    if (persist) {
        m_configGroup.writeEntry("touchSelectionMethod", int(m_method));
    }

    updateMethodUi();
    updateMethodDependentUi();
    resetCursorStyle();
}

void KisToolSelectTouch::updateMethodUi()
{
    if (!m_buttonAutomatic || !m_buttonFreehand || !m_buttonRectangle || !m_buttonEllipse) {
        return;
    }

    switch (m_method) {
    case SelectionMethod::Automatic:
        m_buttonAutomatic->setChecked(true);
        break;
    case SelectionMethod::Freehand:
        m_buttonFreehand->setChecked(true);
        break;
    case SelectionMethod::Rectangle:
        m_buttonRectangle->setChecked(true);
        break;
    case SelectionMethod::Ellipse:
        m_buttonEllipse->setChecked(true);
        break;
    }
}

void KisToolSelectTouch::updateMethodDependentUi()
{
    if (selectionOptionWidget() && m_sectionAutomatic) {
        const bool showAutomatic = m_method == SelectionMethod::Automatic;
        selectionOptionWidget()->setWidgetVisible("sectionAutomatic", showAutomatic);
    }
}

void KisToolSelectTouch::updateSelectionActionUi()
{
    KisSelectionOptions *selectionWidget = selectionOptionWidget();
    if (!selectionWidget || !m_actionButtons || !m_buttonActionReplace || !m_buttonActionAdd ||
        !m_buttonActionSubtract || !m_buttonActionIntersect || !m_buttonActionSymmetricDifference) {
        return;
    }

    QSignalBlocker blocker(m_actionButtons);

    switch (selectionWidget->action()) {
    case SELECTION_REPLACE:
        m_buttonActionReplace->setChecked(true);
        break;
    case SELECTION_ADD:
        m_buttonActionAdd->setChecked(true);
        break;
    case SELECTION_SUBTRACT:
        m_buttonActionSubtract->setChecked(true);
        break;
    case SELECTION_INTERSECT:
        m_buttonActionIntersect->setChecked(true);
        break;
    case SELECTION_SYMMETRICDIFFERENCE:
        m_buttonActionSymmetricDifference->setChecked(true);
        break;
    case SELECTION_DEFAULT:
    default:
        m_buttonActionReplace->setChecked(true);
        break;
    }
}

void KisToolSelectTouch::updateFeatherUi()
{
    KisSelectionOptions *selectionWidget = selectionOptionWidget();
    if (!selectionWidget || !m_sliderFeather) {
        return;
    }

    const int feather = std::clamp(selectionWidget->featherSelection(), 0, 400);
    if (m_sliderFeather->value() == feather) {
        return;
    }

    QSignalBlocker blocker(m_sliderFeather);
    m_sliderFeather->setValue(feather);
}

void KisToolSelectTouch::updateSavedSelectionUi()
{
    if (!m_buttonLoadSelection) {
        return;
    }

    m_buttonLoadSelection->setEnabled(bool(m_savedSelection));
}

void KisToolSelectTouch::slot_methodButtonToggled(KoGroupButton *button, bool checked)
{
    if (!checked) {
        return;
    }

    if (button == m_buttonAutomatic) {
        setMethod(SelectionMethod::Automatic, true);
    } else if (button == m_buttonFreehand) {
        setMethod(SelectionMethod::Freehand, true);
    } else if (button == m_buttonRectangle) {
        setMethod(SelectionMethod::Rectangle, true);
    } else if (button == m_buttonEllipse) {
        setMethod(SelectionMethod::Ellipse, true);
    }
}

void KisToolSelectTouch::slot_actionButtonToggled(KoGroupButton *button, bool checked)
{
    if (!checked) {
        return;
    }

    KisSelectionOptions *selectionWidget = selectionOptionWidget();
    if (!selectionWidget) {
        return;
    }

    if (button == m_buttonActionReplace) {
        selectionWidget->setAction(SELECTION_REPLACE);
    } else if (button == m_buttonActionAdd) {
        selectionWidget->setAction(SELECTION_ADD);
    } else if (button == m_buttonActionSubtract) {
        selectionWidget->setAction(SELECTION_SUBTRACT);
    } else if (button == m_buttonActionIntersect) {
        selectionWidget->setAction(SELECTION_INTERSECT);
    } else if (button == m_buttonActionSymmetricDifference) {
        selectionWidget->setAction(SELECTION_SYMMETRICDIFFERENCE);
    }
}

void KisToolSelectTouch::slot_thresholdChanged(int value)
{
    value = std::clamp(value, 1, 100);
    if (value == m_threshold) {
        return;
    }
    m_threshold = value;
    m_configGroup.writeEntry("threshold", value);
}

void KisToolSelectTouch::slot_featherChanged(int value)
{
    value = std::clamp(value, 0, 400);

    KisSelectionOptions *selectionWidget = selectionOptionWidget();
    if (!selectionWidget) {
        return;
    }

    if (value == selectionWidget->featherSelection()) {
        return;
    }

    selectionWidget->setFeatherSelection(value);
}

void KisToolSelectTouch::slot_saveSelectionClicked()
{
    KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(canvas());
    if (!kisCanvas) {
        return;
    }

    KisView *view = kisCanvas->imageView();
    if (!view) {
        return;
    }

    KisSelectionSP selection = view->selection();
    if (!selection || !selection->pixelSelection()) {
        return;
    }

    KisPixelSelectionSP pixelSelection = selection->pixelSelection();
    if (pixelSelection->selectedExactRect().isEmpty()) {
        return;
    }

    m_savedSelection = new KisPixelSelection(*pixelSelection);
    updateSavedSelectionUi();
}

void KisToolSelectTouch::slot_loadSelectionClicked()
{
    if (!m_savedSelection) {
        return;
    }

    KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(canvas());
    if (!kisCanvas) {
        return;
    }

    KisSelectionToolHelper helper(kisCanvas, kundo2_i18n("Load Selection"));
    KisPixelSelectionSP selectionCopy = new KisPixelSelection(*m_savedSelection);
    helper.selectPixelSelection(selectionCopy, SELECTION_REPLACE);
}

void KisToolSelectTouch::beginPrimaryAction(KoPointerEvent *event)
{
    KisToolSelect::beginPrimaryAction(event);
    if (isMovingSelection()) {
        return;
    }

    if (!currentNode() || !selectionEditable()) {
        event->ignore();
        return;
    }

    beginSelectInteraction();

    if (m_method == SelectionMethod::Automatic) {
        m_autoAdjustingThreshold = true;
        m_autoDragStart = convertToPixelCoord(event->point);
        m_autoImagePos = convertToImagePixelCoordFloored(event);
        m_autoStartThreshold = m_threshold;
        return;
    }

    if (m_method == SelectionMethod::Rectangle || m_method == SelectionMethod::Ellipse) {
        m_draggingShape = true;
        m_shapeStart = convertToImagePixelCoordFloored(event);
        m_shapeCurrent = m_shapeStart;
        m_shapeLastUpdateRect = QRectF(m_shapeStart, m_shapeCurrent);
        return;
    }

    // Freehand
    m_draggingFreehand = true;
    m_freehandPoints.clear();
    const QPointF p = convertToImagePixelCoordFloored(event);
    m_freehandPoints.append(p);
    m_lastCursorPos = p;
    m_freehandLastUpdateRect = QRectF(p, p);
}

void KisToolSelectTouch::continuePrimaryAction(KoPointerEvent *event)
{
    if (isMovingSelection()) {
        KisToolSelect::continuePrimaryAction(event);
        return;
    }

    if (m_method == SelectionMethod::Automatic && m_autoAdjustingThreshold) {
        const QPointF current = convertToPixelCoord(event->point);
        const qreal deltaX = current.x() - m_autoDragStart.x();
        const int deltaThreshold = int(deltaX / 4.0);
        const int newThreshold = std::clamp(m_autoStartThreshold + deltaThreshold, 1, 100);
        if (m_sliderThreshold && m_sliderThreshold->value() != newThreshold) {
            m_sliderThreshold->setValue(newThreshold);
        } else {
            m_threshold = newThreshold;
        }
        return;
    }

    if ((m_method == SelectionMethod::Rectangle || m_method == SelectionMethod::Ellipse) && m_draggingShape) {
        const QRectF oldRect = QRectF(m_shapeStart, m_shapeCurrent).normalized();
        m_shapeCurrent = convertToImagePixelCoordFloored(event);
        const QRectF newRect = QRectF(m_shapeStart, m_shapeCurrent).normalized();
        updateCanvasPixelRect(paddedRect(oldRect.united(newRect), kOutlinePaddingPx));
        m_shapeLastUpdateRect = newRect;
        return;
    }

    if (m_method == SelectionMethod::Freehand && m_draggingFreehand) {
        const QPointF p = convertToImagePixelCoordFloored(event);
        const QRectF oldBounds = KisAlgebra2D::accumulateBounds(m_freehandPoints);
        m_freehandPoints.append(p);
        m_lastCursorPos = p;
        const QRectF newBounds = KisAlgebra2D::accumulateBounds(m_freehandPoints);
        updateCanvasPixelRect(paddedRect(oldBounds.united(newBounds), kOutlinePaddingPx));
        m_freehandLastUpdateRect = newBounds;
    }
}

void KisToolSelectTouch::endPrimaryAction(KoPointerEvent *event)
{
    if (isMovingSelection()) {
        KisToolSelect::endPrimaryAction(event);
        return;
    }

    if (m_method == SelectionMethod::Automatic && m_autoAdjustingThreshold) {
        m_autoAdjustingThreshold = false;
        applyAutomaticSelection(m_autoImagePos, m_threshold);
        endSelectInteraction();
        return;
    }

    if ((m_method == SelectionMethod::Rectangle || m_method == SelectionMethod::Ellipse) && m_draggingShape) {
        m_draggingShape = false;
        m_shapeCurrent = convertToImagePixelCoordFloored(event);
        const QRectF rect = QRectF(m_shapeStart, m_shapeCurrent).normalized();
        applyRectSelection(rect, m_method == SelectionMethod::Ellipse);
        endSelectInteraction();
        return;
    }

    if (m_method == SelectionMethod::Freehand && m_draggingFreehand) {
        m_draggingFreehand = false;
        if (m_freehandPoints.isEmpty()) {
            endSelectInteraction();
            return;
        }

        const QRectF bounds = KisAlgebra2D::accumulateBounds(m_freehandPoints);
        const bool isTap =
            KisAlgebra2D::maxDimension(bounds) <= kTapMaxDistancePx ||
            m_freehandPoints.size() < 3;

        if (!isTap) {
            // Treat as lasso: commit immediately.
            applyFreehandSelection(m_freehandPoints);
            m_polygonActive = false;
            m_polygonPoints.clear();
            endSelectInteraction();
            return;
        }

        // Tap-to-polygon: build polygon points; finish by tapping near the first point.
        const QPointF tapPoint = m_freehandPoints.last();
        if (!m_polygonActive) {
            m_polygonActive = true;
            m_polygonPoints.clear();
            m_polygonPoints.append(tapPoint);
            m_lastCursorPos = tapPoint;
            m_polygonLastUpdateRect = QRectF(tapPoint, tapPoint);
        } else {
            if (m_polygonPoints.size() >= 3 &&
                kisSquareDistance(tapPoint, m_polygonPoints.first()) <= kClosePolygonDistancePx * kClosePolygonDistancePx) {
                applyFreehandSelection(m_polygonPoints);
                m_polygonActive = false;
                m_polygonPoints.clear();
                endSelectInteraction();
                return;
            }

            const QRectF oldBounds = KisAlgebra2D::accumulateBounds(m_polygonPoints);
            m_polygonPoints.append(tapPoint);
            const QRectF newBounds = KisAlgebra2D::accumulateBounds(m_polygonPoints);
            updateCanvasPixelRect(paddedRect(oldBounds.united(newBounds), kOutlinePaddingPx));
            m_polygonLastUpdateRect = newBounds;
        }

        // Keep interaction open until polygon is finished.
        return;
    }

    endSelectInteraction();
}

void KisToolSelectTouch::mouseMoveEvent(KoPointerEvent *event)
{
    KisToolSelect::mouseMoveEvent(event);

    if (m_polygonActive && !m_draggingFreehand) {
        const QRectF oldBounds = m_polygonPoints.isEmpty()
            ? QRectF()
            : KisAlgebra2D::accumulateBounds(m_polygonPoints);

        m_lastCursorPos = convertToImagePixelCoordFloored(event);

        QRectF newBounds = oldBounds;
        if (!m_polygonPoints.isEmpty()) {
            newBounds = oldBounds.united(QRectF(m_polygonPoints.last(), m_lastCursorPos));
        }
        updateCanvasPixelRect(paddedRect(oldBounds.united(newBounds), kOutlinePaddingPx));
    }
}

void KisToolSelectTouch::paint(QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(converter);

    if ((m_method == SelectionMethod::Rectangle || m_method == SelectionMethod::Ellipse) && m_draggingShape) {
        const QRectF rect = QRectF(m_shapeStart, m_shapeCurrent).normalized();
        const QRectF viewRect = pixelToView(rect);
        QPainterPath path;
        if (m_method == SelectionMethod::Ellipse) {
            path.addEllipse(viewRect);
        } else {
            path.addRect(viewRect);
        }
        paintToolOutline(&painter, KisOptimizedBrushOutline(path));
        return;
    }

    if (m_method == SelectionMethod::Freehand && m_draggingFreehand && m_freehandPoints.size() >= 2) {
        QPainterPath path;
        path.moveTo(pixelToView(m_freehandPoints.first()));
        for (int i = 1; i < m_freehandPoints.size(); ++i) {
            path.lineTo(pixelToView(m_freehandPoints.at(i)));
        }
        paintToolOutline(&painter, KisOptimizedBrushOutline(path));
        return;
    }

    if (m_polygonActive && !m_polygonPoints.isEmpty()) {
        QPainterPath path;
        path.moveTo(pixelToView(m_polygonPoints.first()));
        for (int i = 1; i < m_polygonPoints.size(); ++i) {
            path.lineTo(pixelToView(m_polygonPoints.at(i)));
        }
        path.lineTo(pixelToView(m_lastCursorPos));
        paintToolOutline(&painter, KisOptimizedBrushOutline(path));
        return;
    }
}

void KisToolSelectTouch::resetCursorStyle()
{
    const SelectionAction action = selectionAction();

    auto cursorFor = [&](const char *baseName, int hotX, int hotY) -> QCursor {
        if (action == SELECTION_ADD) {
            return KisCursor::load(QString(baseName) + "_add.png", hotX, hotY);
        }
        if (action == SELECTION_SUBTRACT) {
            return KisCursor::load(QString(baseName) + "_sub.png", hotX, hotY);
        }
        if (action == SELECTION_INTERSECT) {
            return KisCursor::load(QString(baseName) + "_inter.png", hotX, hotY);
        }
        if (action == SELECTION_SYMMETRICDIFFERENCE) {
            return KisCursor::load(QString(baseName) + "_symdiff.png", hotX, hotY);
        }
        return KisCursor::load(QString(baseName) + ".png", hotX, hotY);
    };

    switch (m_method) {
    case SelectionMethod::Automatic:
        useCursor(cursorFor("tool_contiguous_selection_cursor", 6, 6));
        break;
    case SelectionMethod::Freehand:
        useCursor(cursorFor("tool_outline_selection_cursor", 5, 5));
        break;
    case SelectionMethod::Rectangle:
        useCursor(cursorFor("tool_rectangular_selection_cursor", 6, 6));
        break;
    case SelectionMethod::Ellipse:
        useCursor(cursorFor("tool_elliptical_selection_cursor", 6, 6));
        break;
    }
}

void KisToolSelectTouch::applyAutomaticSelection(const QPoint &imagePos, int threshold)
{
    KisPaintDeviceSP dev;
    if (!currentNode() || !(dev = currentNode()->projection()) || !selectionEditable()) {
        return;
    }

    KisCursorOverrideLock cursorLock(KisCursor::waitCursor());

    KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2*>(canvas());
    KIS_SAFE_ASSERT_RECOVER_RETURN(kisCanvas);

    KisSelectionToolHelper helper(kisCanvas, kundo2_i18n("Automatic Selection"));

    const QRect rc = currentImage()->bounds();

    KisPixelSelectionSP selection =
        new KisPixelSelection(new KisSelectionDefaultBounds(dev));

    const bool antiAlias = antiAliasSelection();
    const int grow = growSelection();
    const int feather = featherSelection();

    KisProcessingApplicator applicator(currentImage(),
                                       currentNode(),
                                       KisProcessingApplicator::NONE,
                                       KisImageSignalVector(),
                                       kundo2_i18n("Automatic Selection"));

    KUndo2Command *cmd = new KisCommandUtils::LambdaCommand(
        [dev, rc, threshold, antiAlias, feather, grow, selection, imagePos]() mutable -> KUndo2Command * {
            KisFillPainter fillpainter(dev);
            fillpainter.setHeight(rc.height());
            fillpainter.setWidth(rc.width());
            fillpainter.setRegionFillingMode(KisFillPainter::RegionFillingMode_FloodFill);
            fillpainter.setFillThreshold(threshold);
            fillpainter.setOpacitySpread(100);
            fillpainter.setAntiAlias(antiAlias);
            fillpainter.setFeather(feather);
            fillpainter.setSizemod(grow);
            fillpainter.setUseCompositing(true);

            fillpainter.createFloodSelection(selection,
                                             imagePos.x(),
                                             imagePos.y(),
                                             dev,
                                             nullptr);
            selection->invalidateOutlineCache();
            return nullptr;
        });

    applicator.applyCommand(cmd, KisStrokeJobData::BARRIER);
    helper.selectPixelSelection(applicator, selection, selectionAction());
    applicator.end();
}

void KisToolSelectTouch::applyFreehandSelection(const QVector<QPointF> &points)
{
    if (points.size() < 3) {
        return;
    }

    KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2*>(canvas());
    KIS_SAFE_ASSERT_RECOVER_RETURN(kisCanvas);

    KisSelectionToolHelper helper(kisCanvas, kundo2_i18n("Freehand Selection"));

    const QRectF boundingRect = KisAlgebra2D::accumulateBounds(points);
    const QRectF boundingViewRect = pixelToView(boundingRect);

    if (helper.tryDeselectCurrentSelection(boundingViewRect, selectionAction())) {
        return;
    }

    KisCursorOverrideLock cursorLock(KisCursor::waitCursor());

    KisProcessingApplicator applicator(currentImage(),
                                       currentNode(),
                                       KisProcessingApplicator::NONE,
                                       KisImageSignalVector(),
                                       kundo2_i18n("Freehand Selection"));

    KisPixelSelectionSP tmpSel =
        new KisPixelSelection(new KisDefaultBounds(currentImage()));

    const bool antiAlias = antiAliasSelection();
    const int grow = growSelection();
    const int feather = featherSelection();

    QPainterPath path;
    path.addPolygon(points);
    path.closeSubpath();

    KUndo2Command *cmd = new KisCommandUtils::LambdaCommand(
        [tmpSel, antiAlias, grow, feather, path]() mutable -> KUndo2Command * {
            KisPainter painter(tmpSel);
            painter.setPaintColor(KoColor(Qt::black, tmpSel->colorSpace()));
            painter.setAntiAliasPolygonFill(antiAlias && feather == 0);
            painter.setFillStyle(KisPainter::FillStyleForegroundColor);
            painter.setStrokeStyle(KisPainter::StrokeStyleNone);

            painter.paintPainterPath(path);

            if (grow > 0) {
                KisGrowSelectionFilter biggy(grow, grow);
                biggy.process(tmpSel,
                              tmpSel->selectedRect().adjusted(-grow, -grow, grow, grow));
            } else if (grow < 0) {
                KisShrinkSelectionFilter tiny(-grow, -grow, false);
                tiny.process(tmpSel, tmpSel->selectedRect());
            }

            if (feather > 0) {
                KisFeatherSelectionFilter feathery(feather);
                feathery.process(tmpSel,
                                 tmpSel->selectedRect().adjusted(-feather, -feather, feather, feather));
            }

            if (grow == 0 && feather == 0) {
                tmpSel->setOutlineCache(path);
            } else {
                tmpSel->invalidateOutlineCache();
            }

            return nullptr;
        });

    applicator.applyCommand(cmd, KisStrokeJobData::SEQUENTIAL);
    helper.selectPixelSelection(applicator, tmpSel, selectionAction());
    applicator.end();
}

void KisToolSelectTouch::applyRectSelection(const QRectF &rect, bool elliptical)
{
    KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2*>(canvas());
    KIS_SAFE_ASSERT_RECOVER_RETURN(kisCanvas);

    KisSelectionToolHelper helper(kisCanvas, elliptical ? kundo2_i18n("Select Ellipse") : kundo2_i18n("Select Rectangle"));

    if (helper.tryDeselectCurrentSelection(pixelToView(rect), selectionAction())) {
        return;
    }

    const QRect rc = rect.normalized().toRect();

    if (helper.canShortcutToNoop(rc, selectionAction())) {
        return;
    }

    if (!rc.isValid() || rect.isEmpty()) {
        return;
    }

    KisProcessingApplicator applicator(currentImage(),
                                       currentNode(),
                                       KisProcessingApplicator::NONE,
                                       KisImageSignalVector(),
                                       elliptical ? kundo2_i18n("Select Ellipse") : kundo2_i18n("Select Rectangle"));

    KisPixelSelectionSP tmpSel =
        new KisPixelSelection(new KisDefaultBounds(currentImage()));

    const bool antiAlias = antiAliasSelection();
    const int grow = growSelection();
    const int feather = featherSelection();

    QPainterPath path;
    if (elliptical) {
        path.addEllipse(rect);
    } else {
        path.addRect(rect);
    }

    KUndo2Command *cmd = new KisCommandUtils::LambdaCommand(
        [tmpSel, antiAlias, grow, feather, path]() mutable -> KUndo2Command * {
            KisPainter painter(tmpSel);
            painter.setPaintColor(KoColor(Qt::black, tmpSel->colorSpace()));
            painter.setAntiAliasPolygonFill(antiAlias && feather == 0);
            painter.setFillStyle(KisPainter::FillStyleForegroundColor);
            painter.setStrokeStyle(KisPainter::StrokeStyleNone);

            painter.paintPainterPath(path);

            if (grow > 0) {
                KisGrowSelectionFilter biggy(grow, grow);
                biggy.process(tmpSel,
                              tmpSel->selectedRect().adjusted(-grow, -grow, grow, grow));
            } else if (grow < 0) {
                KisShrinkSelectionFilter tiny(-grow, -grow, false);
                tiny.process(tmpSel, tmpSel->selectedRect());
            }

            if (feather > 0) {
                KisFeatherSelectionFilter feathery(feather);
                feathery.process(tmpSel,
                                 tmpSel->selectedRect().adjusted(-feather, -feather, feather, feather));
            }

            if (grow == 0 && feather == 0) {
                tmpSel->setOutlineCache(path);
            } else {
                tmpSel->invalidateOutlineCache();
            }

            return nullptr;
        });

    applicator.applyCommand(cmd, KisStrokeJobData::SEQUENTIAL);
    helper.selectPixelSelection(applicator, tmpSel, selectionAction());
    applicator.end();
}
