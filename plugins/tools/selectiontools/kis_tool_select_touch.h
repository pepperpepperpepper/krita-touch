/*
 *  SPDX-FileCopyrightText: 2026
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOOL_SELECT_TOUCH_H_
#define KIS_TOOL_SELECT_TOUCH_H_

#include "KisSelectionToolFactoryBase.h"

#include <kis_icon.h>
#include <kis_tool_select_base.h>
#include <kis_types.h>

#include <kconfiggroup.h>

class KisOptionButtonStrip;
class KisOptionCollectionWidgetWithHeader;
class KisSliderSpinBox;
class KoGroupButton;
class QToolButton;

/**
 * A touch-first selection tool inspired by Procreate’s unified Selection tool.
 *
 * It provides a single tool with multiple selection methods:
 * - Automatic (magic wand / contiguous)
 * - Freehand (lasso + tap-to-polygon)
 * - Rectangle
 * - Ellipse
 *
 * The tool is intentionally pixel-selection-only to match the touch UX.
 */
class KisToolSelectTouch : public KisToolSelect
{
    Q_OBJECT

public:
    enum class SelectionMethod {
        Automatic = 0,
        Freehand = 1,
        Rectangle = 2,
        Ellipse = 3,
    };

    explicit KisToolSelectTouch(KoCanvasBase *canvas);
    ~KisToolSelectTouch() override;

    QWidget *createOptionWidget() override;
    void paint(QPainter &painter, const KoViewConverter &converter) override;

    void beginPrimaryAction(KoPointerEvent *event) override;
    void continuePrimaryAction(KoPointerEvent *event) override;
    void endPrimaryAction(KoPointerEvent *event) override;

    bool primaryActionSupportsHiResEvents() const override;

    void mouseMoveEvent(KoPointerEvent *event) override;

    void resetCursorStyle() override;

public Q_SLOTS:
    void activate(const QSet<KoShape*> &shapes) override;
    void deactivate() override;

private Q_SLOTS:
    void slot_methodButtonToggled(KoGroupButton *button, bool checked);
    void slot_actionButtonToggled(KoGroupButton *button, bool checked);
    void slot_toggleFeatherPanel(bool checked);
    void slot_toggleSaveLoadPanel(bool checked);
    void slot_thresholdChanged(int value);
    void slot_featherChanged(int value);
    void slot_saveSelectionClicked();
    void slot_loadSelectionClicked();

private:
    bool isPixelOnly() const override { return true; }

    void setMethod(SelectionMethod method, bool persist);
    void loadSettings();
    void cancelInFlightInteraction();

    void updateMethodUi();
    void updateMethodDependentUi();
    void updateSelectionActionUi();
    void updateFeatherUi();
    void updateSavedSelectionUi();

    void applyAutomaticSelection(const QPoint &imagePos, int threshold);
    void applyFreehandSelection(const QVector<QPointF> &points);
    void applyRectSelection(const QRectF &rect, bool elliptical);

    SelectionMethod m_method {SelectionMethod::Freehand};

    KisOptionButtonStrip *m_methodButtons {nullptr};
    KoGroupButton *m_buttonAutomatic {nullptr};
    KoGroupButton *m_buttonFreehand {nullptr};
    KoGroupButton *m_buttonRectangle {nullptr};
    KoGroupButton *m_buttonEllipse {nullptr};

    KisOptionCollectionWidgetWithHeader *m_sectionSelectionAction {nullptr};
    KisOptionButtonStrip *m_actionButtons {nullptr};
    KoGroupButton *m_buttonActionReplace {nullptr};
    KoGroupButton *m_buttonActionAdd {nullptr};
    KoGroupButton *m_buttonActionSubtract {nullptr};
    KoGroupButton *m_buttonActionIntersect {nullptr};
    KoGroupButton *m_buttonActionSymmetricDifference {nullptr};

    KoGroupButton *m_buttonToggleFeather {nullptr};
    KoGroupButton *m_buttonToggleSaveLoad {nullptr};

    KisOptionCollectionWidgetWithHeader *m_sectionFeather {nullptr};
    KisSliderSpinBox *m_sliderFeather {nullptr};

    KisOptionCollectionWidgetWithHeader *m_sectionAutomatic {nullptr};
    KisSliderSpinBox *m_sliderThreshold {nullptr};
    int m_threshold {8};

    KisOptionCollectionWidgetWithHeader *m_sectionSavedSelection {nullptr};
    QToolButton *m_buttonSaveSelection {nullptr};
    QToolButton *m_buttonLoadSelection {nullptr};
    KisPixelSelectionSP m_savedSelection;

    bool m_draggingShape {false};
    QPointF m_shapeStart;
    QPointF m_shapeCurrent;
    QRectF m_shapeLastUpdateRect;

    bool m_draggingFreehand {false};
    QVector<QPointF> m_freehandPoints;
    QPointF m_lastCursorPos;
    QRectF m_freehandLastUpdateRect;

    bool m_polygonActive {false};
    QVector<QPointF> m_polygonPoints;
    QRectF m_polygonLastUpdateRect;

    bool m_autoAdjustingThreshold {false};
    QPointF m_autoDragStart;
    QPoint m_autoImagePos;
    int m_autoStartThreshold {8};

    KConfigGroup m_configGroup;
};

class KisToolSelectTouchFactory : public KisSelectionToolFactoryBase
{
public:
    KisToolSelectTouchFactory()
        : KisSelectionToolFactoryBase("KisToolSelectTouch")
    {
        // Procreate-style unified selection tool (Automatic/Freehand/Rect/Ellipse).
        // Keep the tooltip/name generic so it reads naturally in the toolbox.
        setToolTip(i18n("Selection Tool"));
        setSection(ToolBoxSection::Select);
        setActivationShapeId(KRITA_TOOL_ACTIVATION_ID);
        // Prefer a selection-specific (but method-agnostic) icon so this tool
        // stands out from the legacy lasso/rect/ellipse tools in the Select section.
        setIconName(koIconNameCStr("tool_touch_selection"));
        // Show early in the Select section so it is easy to find in touch-first
        // workflows (and can act as the default selection entry point).
        setPriority(-1);
    }

    ~KisToolSelectTouchFactory() override {}

    KoToolBase *createTool(KoCanvasBase *canvas) override
    {
        return new KisToolSelectTouch(canvas);
    }
};

#endif // KIS_TOOL_SELECT_TOUCH_H_
