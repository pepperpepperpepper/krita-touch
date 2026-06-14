/*
 *  SPDX-FileCopyrightText: 2007 Sven Langkamp <sven.langkamp@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KIS_SELECTION_TOOL_HELPER_H
#define KIS_SELECTION_TOOL_HELPER_H

#include <kritaui_export.h>
#include <QMenu>
#include <QPointer>

#include "kundo2magicstring.h"
#include "kis_layer.h"
#include "kis_selection.h"
#include "kis_canvas2.h"
#include "kis_processing_applicator.h"

class KoShape;
class QPainterPath;

/**
 * XXX: Doc!
 */
class KRITAUI_EXPORT KisSelectionToolHelper
{
public:

    KisSelectionToolHelper(KisCanvas2* canvas, const KUndo2MagicString& name);
    virtual ~KisSelectionToolHelper();

    void selectPixelSelection(KisProcessingApplicator& applicator, KisPixelSelectionSP selection, SelectionAction action);
    void selectPixelSelection(KisPixelSelectionSP selection, SelectionAction action);

    /**
     * Commits @p path as a pixel selection. Allocates a temporary
     * KisPixelSelection bounded by @p image, builds a DEFERRED stroke command
     * that rasterizes @p path with the given antiAlias/grow/feather settings
     * (KisPainter::paintPainterPath + grow/shrink/feather filters + outline
     * cache), enqueues it on a fresh KisProcessingApplicator rooted at @p node
     * and named with this helper's undo name, then calls selectPixelSelection()
     * and ends the applicator.
     *
     * The rasterization runs deferred inside the stroke job, not at call time;
     * @p path and the scalars are captured by value. Pass currentNode() for
     * @p node (it is the processing root, NOT a layer) and currentImage() for
     * @p image (it bounds the temporary selection). Callers retain the concerns
     * that differ per shape: early-out guards, cursor overrides, the undo-name
     * string (given to this helper's constructor), and path construction.
     */
    void applyShapePath(const QPainterPath &path,
                        SelectionAction action,
                        KisNodeSP node,
                        KisImageSP image,
                        bool antiAlias,
                        int grow,
                        int feather);

    void addSelectionShape(KoShape* shape, SelectionAction action = SELECTION_DEFAULT);
    void addSelectionShapes(QList<KoShape*> shapes, SelectionAction action = SELECTION_DEFAULT);

    bool canShortcutToDeselect(const QRect &rect, SelectionAction action);
    bool canShortcutToNoop(const QRect &rect, SelectionAction action);

    bool tryDeselectCurrentSelection(const QRectF selectionViewRect, SelectionAction action);
    static QMenu* getSelectionContextMenu(KisCanvas2* canvas);

    SelectionMode tryOverrideSelectionMode(KisSelectionSP activeSelection, SelectionMode currentMode, SelectionAction currentAction) const;


private:
    QPointer<KisCanvas2> m_canvas;
    KisImageSP m_image;
    KisLayerSP m_layer;
    KUndo2MagicString m_name;
};


#endif
