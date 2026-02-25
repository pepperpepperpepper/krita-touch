/*
  SPDX-FileCopyrightText: 2006 Gábor Lehel <illissius@gmail.com>

  SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "NodeView.h"
#include "NodePropertyAction_p.h"
#include "NodeDelegate.h"
#include "NodeViewVisibilityDelegate.h"
#include "kis_node_model.h"
#include "kis_signals_blocker.h"


#include <kconfig.h>
#include <kconfiggroup.h>
#include <kis_config.h>
#include <kis_icon.h>
#include <ksharedconfig.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisKineticScroller.h>
#include <kactioncollection.h>

#include <QtDebug>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QHelpEvent>
#include <QMenu>
#include <QDrag>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QApplication>
#include <QPainter>
#include <QScrollBar>
#include <QScroller>
#include <QTimer>

#include "kis_node_view_color_scheme.h"

namespace {

bool isTouchSmokeRun()
{
    return qApp && qApp->property("krita_touch_smoke").toBool();
}

} // namespace


#ifdef HAVE_X11
#define DRAG_WHILE_DRAG_WORKAROUND
#endif

#ifdef DRAG_WHILE_DRAG_WORKAROUND
#define DRAG_WHILE_DRAG_WORKAROUND_START() d->isDragging = true
#define DRAG_WHILE_DRAG_WORKAROUND_STOP() d->isDragging = false
#else
#define DRAG_WHILE_DRAG_WORKAROUND_START()
#define DRAG_WHILE_DRAG_WORKAROUND_STOP()
#endif


class Q_DECL_HIDDEN NodeView::Private
{
public:
    Private(NodeView* _q)
        : delegate(_q, _q)
#ifdef DRAG_WHILE_DRAG_WORKAROUND
        , isDragging(false)
#endif
        , touchOptionsCandidateActive(false)
        , touchSwipeCandidateActive(false)
        , touchVisibilityHoldCandidateActive(false)
        , touchVisibilityHoldTriggered(false)
        , touchVisibilityHoldTimer(nullptr)
        , touchSelectOpaqueHoldCandidateActive(false)
        , touchSelectOpaqueHoldTriggered(false)
        , touchSelectOpaqueHoldTimer(nullptr)
    {
    }
    NodeDelegate delegate;
    QPersistentModelIndex hovered;
    QPoint lastPos;

#ifdef DRAG_WHILE_DRAG_WORKAROUND
    bool isDragging;
#endif

    QPersistentModelIndex touchOptionsCandidateIndex;
    bool touchOptionsCandidateActive;
    QPersistentModelIndex touchSwipeCandidateIndex;
    QPoint touchSwipeStartPos;
    bool touchSwipeCandidateActive;

    QPersistentModelIndex touchVisibilityHoldCandidateIndex;
    QPoint touchVisibilityHoldStartPos;
    bool touchVisibilityHoldCandidateActive;
    bool touchVisibilityHoldTriggered;
    QTimer *touchVisibilityHoldTimer;

    QPersistentModelIndex touchSelectOpaqueHoldCandidateIndex;
    QPoint touchSelectOpaqueHoldStartPos;
    bool touchSelectOpaqueHoldCandidateActive;
    bool touchSelectOpaqueHoldTriggered;
    QTimer *touchSelectOpaqueHoldTimer;
};


NodeView::NodeView(QWidget *parent)
    : QTreeView(parent)
    , m_draggingFlag(false)
    , d(new Private(this))
{
    setItemDelegate(&d->delegate);

    setMouseTracking(true);
    setSelectionBehavior(SelectRows);
    setDefaultDropAction(Qt::MoveAction);
    setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setRootIsDecorated(false);

    header()->hide();
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setAcceptDrops(true);
    setDropIndicatorShown(true);

    {
        QScroller *scroller = KisKineticScroller::createPreconfiguredScroller(this);
        if (scroller) {
            connect(scroller, SIGNAL(stateChanged(QScroller::State)),
                    this, SLOT(slotScrollerStateChanged(QScroller::State)));
        }
    }

    d->touchVisibilityHoldTimer = new QTimer(this);
    d->touchVisibilityHoldTimer->setSingleShot(true);
    connect(d->touchVisibilityHoldTimer, &QTimer::timeout, this, &NodeView::slotTouchVisibilityHoldTimeout);

    d->touchSelectOpaqueHoldTimer = new QTimer(this);
    d->touchSelectOpaqueHoldTimer->setSingleShot(true);
    connect(d->touchSelectOpaqueHoldTimer, &QTimer::timeout, this, &NodeView::slotTouchSelectOpaqueHoldTimeout);
}

NodeView::~NodeView()
{
    delete d;
}

void NodeView::setModel(QAbstractItemModel *model)
{
    QTreeView::setModel(model);

    if (!this->model()->inherits("KisNodeModel") && !this->model()->inherits("KisNodeFilterProxyModel")) {
        qWarning() << "NodeView may not work with" << model->metaObject()->className();
    }
    if (this->model()->columnCount() != 3) {
        qWarning() << "NodeView: expected 2 model columns, got " << this->model()->columnCount();
    }

    if (header()->sectionPosition(VISIBILITY_COL) != 0 || header()->sectionPosition(SELECTED_COL) != 1) {
        header()->moveSection(VISIBILITY_COL, 0);
        header()->moveSection(SELECTED_COL, 1);
    }

    KisConfig cfg(true);
    if (!cfg.useLayerSelectionCheckbox()) {
        header()->hideSection(SELECTED_COL);
    }

    // the default may be too large for our visibility icon
    header()->setMinimumSectionSize(KisNodeViewColorScheme::instance()->visibilityColumnWidth());
}

void NodeView::addPropertyActions(QMenu *menu, const QModelIndex &index)
{
    KisBaseNode::PropertyList list = index.data(KisNodeModel::PropertiesRole).value<KisBaseNode::PropertyList>();
    for (int i = 0, n = list.count(); i < n; ++i) {
        if (list.at(i).isMutable) {
            PropertyAction *a = new PropertyAction(i, list.at(i), index, menu);
            connect(a, SIGNAL(toggled(bool,QPersistentModelIndex,int)),
                    this, SLOT(slotActionToggled(bool,QPersistentModelIndex,int)));
            menu->addAction(a);
        }
    }
}

void NodeView::updateNode(const QModelIndex &index)
{
    dataChanged(index, index);
}

void NodeView::toggleSolo(const QModelIndex &index) {
    d->delegate.toggleSolo(index);
}

QItemSelectionModel::SelectionFlags NodeView::selectionCommand(const QModelIndex &index, const QEvent *event) const
{
    //Block selection on press if ctrl is held
    //This is to allow for a more consistent behaviour with ctrl+dnd
    if (event &&
        event->type() == QEvent::MouseButtonPress ) {
        const QMouseEvent *mevent = static_cast<const QMouseEvent*>(event);
        if (mevent->modifiers() & Qt::ControlModifier)
            return QItemSelectionModel::NoUpdate;
    }

    return QTreeView::selectionCommand(index, event);
}

QModelIndex NodeView::indexAt(const QPoint &point) const
{
    KisNodeViewColorScheme scm;

    QModelIndex index = QTreeView::indexAt(point);
    if (!index.isValid()) {
        // Middle is a good position for both LTR and RTL layouts
        // First reset x, then get the x in the middle
        index = QTreeView::indexAt(point - QPoint(point.x(), 0) + QPoint(width() / 2, 0));
    }

    return index;
}

bool NodeView::viewportEvent(QEvent *e)
{
    if (model()) {
        switch(e->type()) {
        case QEvent::MouseButtonPress: {
            DRAG_WHILE_DRAG_WORKAROUND_STOP();
            d->touchOptionsCandidateActive = false;
            d->touchOptionsCandidateIndex = QModelIndex();
            d->touchSwipeCandidateActive = false;
            d->touchSwipeCandidateIndex = QModelIndex();
            d->touchVisibilityHoldCandidateActive = false;
            d->touchVisibilityHoldTriggered = false;
            d->touchVisibilityHoldCandidateIndex = QModelIndex();
            d->touchVisibilityHoldStartPos = QPoint();
            if (d->touchVisibilityHoldTimer) {
                d->touchVisibilityHoldTimer->stop();
            }
            d->touchSelectOpaqueHoldCandidateActive = false;
            d->touchSelectOpaqueHoldTriggered = false;
            d->touchSelectOpaqueHoldCandidateIndex = QModelIndex();
            d->touchSelectOpaqueHoldStartPos = QPoint();
            if (d->touchSelectOpaqueHoldTimer) {
                d->touchSelectOpaqueHoldTimer->stop();
            }

            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(e);
            const QPoint pos = mouseEvent->pos();
            d->lastPos = pos;

            if (!indexAt(pos).isValid()) {
                return QTreeView::viewportEvent(e);
            }

            const QModelIndex rawIndex = QTreeView::indexAt(pos);
            const QModelIndex rawBuddyIndex = rawIndex.isValid() ? model()->buddy(rawIndex) : QModelIndex();
            const bool pressedOnSelectedRow =
                rawIndex.isValid() &&
                rawIndex.column() == DEFAULT_COL &&
                selectionModel() &&
                selectionModel()->isSelected(rawBuddyIndex);

            const KisConfig cfg(true);
            const bool treatAsTouchMouse =
                mouseEvent->source() != Qt::MouseEventNotSynthesized ||
                isTouchSmokeRun();
            const bool touchModeMouse =
                cfg.touchModeEnabled() &&
                treatAsTouchMouse &&
                mouseEvent->button() == Qt::LeftButton;

            if (isTouchSmokeRun()) {
                qInfo().noquote()
                    << QStringLiteral("KRITA_TOUCH_SMOKE_NODEVIEW press pos=%1,%2 rawValid=%3 rawCol=%4 button=%5 buttons=%6 source=%7 touchMode=%8")
                           .arg(pos.x())
                           .arg(pos.y())
                           .arg(rawIndex.isValid())
                           .arg(rawIndex.column())
                           .arg(int(mouseEvent->button()))
                           .arg(int(mouseEvent->buttons()))
                           .arg(int(mouseEvent->source()))
                           .arg(touchModeMouse);
            }

            // Procreate-like layer "solo": press-and-hold the visibility icon.
            // We delay the normal visibility toggle so short tap still toggles.
            if (touchModeMouse &&
                rawIndex.isValid() &&
                rawIndex.column() == VISIBILITY_COL &&
                d->touchVisibilityHoldTimer) {
                d->touchVisibilityHoldCandidateActive = true;
                d->touchVisibilityHoldTriggered = false;
                d->touchVisibilityHoldCandidateIndex = rawIndex;
                d->touchVisibilityHoldStartPos = pos;

                constexpr int kHoldMs = 450;
                d->touchVisibilityHoldTimer->start(kHoldMs);

                e->accept();
                return true;
            }

            QModelIndex index = model()->buddy(indexAt(pos));
            if (d->delegate.editorEvent(e, model(), optionForIndex(index), index)) {
                if (isTouchSmokeRun()) {
                    qInfo().noquote() << QStringLiteral("KRITA_TOUCH_SMOKE_NODEVIEW press delegate_consumed=true");
                }
                return true;
            }

            if (touchModeMouse) {
                if (rawIndex.isValid() && rawIndex.column() == DEFAULT_COL && rawBuddyIndex.isValid()) {
                    d->touchSwipeCandidateActive = true;
                    d->touchSwipeCandidateIndex = rawBuddyIndex;
                    d->touchSwipeStartPos = pos;

                    if (d->touchSelectOpaqueHoldTimer) {
                        d->touchSelectOpaqueHoldCandidateActive = true;
                        d->touchSelectOpaqueHoldTriggered = false;
                        d->touchSelectOpaqueHoldCandidateIndex = rawBuddyIndex;
                        d->touchSelectOpaqueHoldStartPos = pos;

                        constexpr int kHoldMs = 450;
                        d->touchSelectOpaqueHoldTimer->start(kHoldMs);
                    }

                    if (pressedOnSelectedRow) {
                        d->touchOptionsCandidateActive = true;
                        d->touchOptionsCandidateIndex = rawBuddyIndex;
                    }

                    if (isTouchSmokeRun()) {
                        qInfo().noquote()
                            << QStringLiteral("KRITA_TOUCH_SMOKE_NODEVIEW press swipe_candidate=1 row=%1 col=%2")
                                   .arg(d->touchSwipeCandidateIndex.row())
                                   .arg(d->touchSwipeCandidateIndex.column());
                    }
                }
            }
        } break;
        case QEvent::MouseButtonRelease: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(e);
            const QPoint pos = mouseEvent->pos();

            if (d->touchVisibilityHoldCandidateActive && mouseEvent->button() == Qt::LeftButton) {
                if (d->touchVisibilityHoldTimer) {
                    d->touchVisibilityHoldTimer->stop();
                }

                const int dragDistance = (pos - d->touchVisibilityHoldStartPos).manhattanLength();
                if (!d->touchVisibilityHoldTriggered &&
                    d->touchVisibilityHoldCandidateIndex.isValid() &&
                    dragDistance <= qApp->startDragDistance()) {
                    QMouseEvent pressEvent(
                        QEvent::MouseButtonPress,
                        d->touchVisibilityHoldStartPos,
                        Qt::LeftButton,
                        Qt::LeftButton,
                        mouseEvent->modifiers());
                    d->delegate.editorEvent(&pressEvent,
                                            model(),
                                            optionForIndex(d->touchVisibilityHoldCandidateIndex),
                                            d->touchVisibilityHoldCandidateIndex);
                }

                d->touchVisibilityHoldCandidateActive = false;
                d->touchVisibilityHoldTriggered = false;
                d->touchVisibilityHoldCandidateIndex = QModelIndex();
                d->touchVisibilityHoldStartPos = QPoint();

                e->accept();
                return true;
            }

            if (d->touchSelectOpaqueHoldCandidateActive && mouseEvent->button() == Qt::LeftButton) {
                if (d->touchSelectOpaqueHoldTimer) {
                    d->touchSelectOpaqueHoldTimer->stop();
                }

                const bool holdTriggered = d->touchSelectOpaqueHoldTriggered;
                d->touchSelectOpaqueHoldCandidateActive = false;
                d->touchSelectOpaqueHoldTriggered = false;
                d->touchSelectOpaqueHoldCandidateIndex = QModelIndex();
                d->touchSelectOpaqueHoldStartPos = QPoint();

                if (holdTriggered) {
                    d->touchSwipeCandidateActive = false;
                    d->touchSwipeCandidateIndex = QModelIndex();
                    d->touchOptionsCandidateActive = false;
                    d->touchOptionsCandidateIndex = QModelIndex();

                    e->accept();
                    return true;
                }
            }

            d->touchSwipeCandidateActive = false;
            d->touchSwipeCandidateIndex = QModelIndex();

            if (!d->touchOptionsCandidateActive || mouseEvent->button() != Qt::LeftButton) {
                d->touchOptionsCandidateActive = false;
                d->touchOptionsCandidateIndex = QModelIndex();
                break;
            }

            const int dragDistance = (pos - d->lastPos).manhattanLength();
            if (dragDistance > qApp->startDragDistance()) {
                d->touchOptionsCandidateActive = false;
                d->touchOptionsCandidateIndex = QModelIndex();
                break;
            }

            const QModelIndex rawIndex = QTreeView::indexAt(pos);
            if (!rawIndex.isValid() || rawIndex.column() != DEFAULT_COL) {
                d->touchOptionsCandidateActive = false;
                d->touchOptionsCandidateIndex = QModelIndex();
                break;
            }

            const QModelIndex buddyIndex = model()->buddy(rawIndex);
            if (!buddyIndex.isValid() || buddyIndex != d->touchOptionsCandidateIndex) {
                d->touchOptionsCandidateActive = false;
                d->touchOptionsCandidateIndex = QModelIndex();
                break;
            }

            KisMainWindow *mainWindow = KisPart::instance()->currentMainwindow();
            KisKActionCollection *actionCollection = mainWindow ? mainWindow->actionCollection() : nullptr;
            QAction *action =
                actionCollection ? actionCollection->action(QStringLiteral("touch_layer_options_sheet")) : nullptr;
            if (!action && actionCollection) {
                action = actionCollection->action(QStringLiteral("layer_properties"));
            }

            d->touchOptionsCandidateActive = false;
            d->touchOptionsCandidateIndex = QModelIndex();

            if (action) {
                action->trigger();
                e->accept();
                return true;
            }
        } break;
        case QEvent::Leave: {
            QEvent e(QEvent::Leave);
            d->delegate.editorEvent(&e, model(), optionForIndex(d->hovered), d->hovered);
            d->hovered = QModelIndex();
        } break;
        case QEvent::MouseMove: {
#ifdef DRAG_WHILE_DRAG_WORKAROUND
            if (d->isDragging) {
                return false;
            }
#endif

            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(e);
            const QPoint pos = mouseEvent->pos();

            if (d->touchVisibilityHoldCandidateActive &&
                (mouseEvent->buttons() & Qt::LeftButton) &&
                d->touchVisibilityHoldCandidateIndex.isValid()) {
                const int dragDistance = (pos - d->touchVisibilityHoldStartPos).manhattanLength();
                if (dragDistance > qApp->startDragDistance()) {
                    if (d->touchVisibilityHoldTimer) {
                        d->touchVisibilityHoldTimer->stop();
                    }
                    d->touchVisibilityHoldCandidateActive = false;
                    d->touchVisibilityHoldTriggered = false;
                    d->touchVisibilityHoldCandidateIndex = QModelIndex();
                    d->touchVisibilityHoldStartPos = QPoint();
                }
            }

            if (d->touchSelectOpaqueHoldCandidateActive &&
                (mouseEvent->buttons() & Qt::LeftButton) &&
                d->touchSelectOpaqueHoldCandidateIndex.isValid()) {
                const int dragDistance = (pos - d->touchSelectOpaqueHoldStartPos).manhattanLength();
                if (dragDistance > qApp->startDragDistance()) {
                    if (d->touchSelectOpaqueHoldTimer) {
                        d->touchSelectOpaqueHoldTimer->stop();
                    }
                    d->touchSelectOpaqueHoldCandidateActive = false;
                    d->touchSelectOpaqueHoldTriggered = false;
                    d->touchSelectOpaqueHoldCandidateIndex = QModelIndex();
                    d->touchSelectOpaqueHoldStartPos = QPoint();
                }
            }

            if (d->touchSwipeCandidateActive &&
                (mouseEvent->source() != Qt::MouseEventNotSynthesized || isTouchSmokeRun()) &&
                (mouseEvent->buttons() & Qt::LeftButton) &&
                d->touchSwipeCandidateIndex.isValid()) {

                const QPoint delta = pos - d->touchSwipeStartPos;
                const int dx = delta.x();
                const int dy = delta.y();
                const int absDx = qAbs(dx);
                const int absDy = qAbs(dy);

                const int minSwipePx = qMax(qApp->startDragDistance() * 2, 36);
                if (isTouchSmokeRun()) {
                    qInfo().noquote()
                        << QStringLiteral(
                               "KRITA_TOUCH_SMOKE_NODEVIEW move pos=%1,%2 dx=%3 dy=%4 absDx=%5 absDy=%6 minSwipe=%7")
                               .arg(pos.x())
                               .arg(pos.y())
                               .arg(dx)
                               .arg(dy)
                               .arg(absDx)
                               .arg(absDy)
                               .arg(minSwipePx);
                }
                if (absDy >= minSwipePx && absDy > absDx) {
                    // Treat as a scroll/drag; don't keep a swipe candidate alive.
                    d->touchSwipeCandidateActive = false;
                    d->touchSwipeCandidateIndex = QModelIndex();
                } else if (absDx >= minSwipePx && absDx >= absDy * 2) {
                    // Swipe left: show layer options (Lock/Duplicate/Delete).
                    // Swipe right: multi-select toggle (Procreate-style).
                    if (dx < 0) {
                        KisMainWindow *mainWindow = KisPart::instance()->currentMainwindow();
                        KisKActionCollection *actionCollection = mainWindow ? mainWindow->actionCollection() : nullptr;
                        QAction *action =
                            actionCollection ? actionCollection->action(QStringLiteral("touch_layer_options_sheet")) : nullptr;
                        if (!action && actionCollection) {
                            action = actionCollection->action(QStringLiteral("layer_properties"));
                        }

                        if (action) {
                            action->setData(mouseEvent->globalPos());
                            action->trigger();
                        }
                    } else {
                        if (selectionModel()) {
                            const bool wasSelected = selectionModel()->isSelected(d->touchSwipeCandidateIndex);
                            selectionModel()->select(d->touchSwipeCandidateIndex,
                                                     QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                            selectionModel()->setCurrentIndex(d->touchSwipeCandidateIndex,
                                                             QItemSelectionModel::NoUpdate);
                            if (isTouchSmokeRun()) {
                                const bool isSelectedNow = selectionModel()->isSelected(d->touchSwipeCandidateIndex);
                                qInfo().noquote()
                                    << QStringLiteral("KRITA_TOUCH_SMOKE_NODEVIEW swipe_right selection %1->%2")
                                           .arg(wasSelected ? 1 : 0)
                                           .arg(isSelectedNow ? 1 : 0);
                            }
                        }
                    }

                    if (isTouchSmokeRun()) {
                        qInfo().noquote()
                            << QStringLiteral("KRITA_TOUCH_SMOKE_NODEVIEW swipe recognized dx=%1").arg(dx);
                    }

                    d->touchSwipeCandidateActive = false;
                    d->touchSwipeCandidateIndex = QModelIndex();
                    d->touchOptionsCandidateActive = false;
                    d->touchOptionsCandidateIndex = QModelIndex();
                    if (d->touchSelectOpaqueHoldTimer) {
                        d->touchSelectOpaqueHoldTimer->stop();
                    }
                    d->touchSelectOpaqueHoldCandidateActive = false;
                    d->touchSelectOpaqueHoldTriggered = false;
                    d->touchSelectOpaqueHoldCandidateIndex = QModelIndex();
                    d->touchSelectOpaqueHoldStartPos = QPoint();

                    e->accept();
                    return true;
                }
            }

            QModelIndex hovered = indexAt(pos);
            if (hovered != d->hovered) {
                if (d->hovered.isValid()) {
                    QEvent e(QEvent::Leave);
                    d->delegate.editorEvent(&e, model(), optionForIndex(d->hovered), d->hovered);
                }
                if (hovered.isValid()) {
                    QEvent e(QEvent::Enter);
                    d->delegate.editorEvent(&e, model(), optionForIndex(hovered), hovered);
                }
                d->hovered = hovered;
            }
            /* This is a workaround for a bug in QTreeView that immediately begins a dragging action
            when the mouse lands on the decoration/icon of a different index and moves 1 pixel or more */
            Qt::MouseButtons buttons = static_cast<QMouseEvent*>(e)->buttons();
            if ((Qt::LeftButton | Qt::MiddleButton) & buttons) {
                if ((pos - d->lastPos).manhattanLength() > qApp->startDragDistance()) {
                    return QTreeView::viewportEvent(e);
                }
                return true;
            }
        } break;
        case QEvent::ToolTip: {
            const QPoint pos = static_cast<QHelpEvent*>(e)->pos();
            if (!indexAt(pos).isValid()) {
                return QTreeView::viewportEvent(e);
            }
            QModelIndex index = model()->buddy(indexAt(pos));
            return d->delegate.editorEvent(e, model(), optionForIndex(index), index);
        } break;
        case QEvent::Resize: {
            scheduleDelayedItemsLayout();
            break;
        }
        default: break;
        }
    }
    return QTreeView::viewportEvent(e);
}

void NodeView::contextMenuEvent(QContextMenuEvent *e)
{
    QTreeView::contextMenuEvent(e);
    QModelIndex i = indexAt(e->pos());
    if (model())
        i = model()->buddy(i);
    showContextMenu(e->globalPos(), i);
}

void NodeView::showContextMenu(const QPoint &globalPos, const QModelIndex &index)
{
    Q_EMIT contextMenuRequested(globalPos, index);
}

void NodeView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    QTreeView::currentChanged(current, previous);
    if (current != previous) {
        Q_ASSERT(!current.isValid() || current.model() == model());
        KisSignalsBlocker blocker(this);
        model()->setData(current, true, KisNodeModel::ActiveRole);
    }
}

void NodeView::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &/*roles*/)
{
    QTreeView::dataChanged(topLeft, bottomRight);

    for (int x = topLeft.row(); x <= bottomRight.row(); ++x) {
        for (int y = topLeft.column(); y <= bottomRight.column(); ++y) {
            QModelIndex index = topLeft.sibling(x, y);
            if (index.data(KisNodeModel::ActiveRole).toBool()) {
                if (currentIndex() != index) {
                    setCurrentIndex(index);
                }

                return;
            }
        }
    }
}

void NodeView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    QTreeView::selectionChanged(selected, deselected);
    // XXX: selectedIndexes() does not include hidden (collapsed) items, is this really intended?
    Q_EMIT selectionChanged(selectedIndexes());
}

void NodeView::slotActionToggled(bool on, const QPersistentModelIndex &index, int num)
{
    KisBaseNode::PropertyList list = index.data(KisNodeModel::PropertiesRole).value<KisBaseNode::PropertyList>();
    list[num].state = on;
    const_cast<QAbstractItemModel*>(index.model())->setData(index, QVariant::fromValue(list), KisNodeModel::PropertiesRole);
}

QStyleOptionViewItem NodeView::optionForIndex(const QModelIndex &index) const
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QStyleOptionViewItem option = viewOptions();
#else
    QStyleOptionViewItem option;
    initViewItemOption(&option);
#endif
    option.rect = visualRect(index);
    if (index == currentIndex())
        option.state |= QStyle::State_HasFocus;
    return option;
}

void NodeView::startDrag(Qt::DropActions supportedActions)
{
    DRAG_WHILE_DRAG_WORKAROUND_START();
    QTreeView::startDrag(supportedActions);
}

QPixmap NodeView::createDragPixmap() const
{
    const QModelIndexList selectedIndexes = selectionModel()->selectedIndexes();
    Q_ASSERT(!selectedIndexes.isEmpty());

    const int itemCount = selectedIndexes.count();

    // If more than one item is dragged, align the items inside a
    // rectangular grid. The maximum grid size is limited to 4 x 4 items.
    int xCount = 2;
    int size = 96;
    if (itemCount > 9) {
        xCount = 4;
        size = KisIconUtils::SizeLarge;
    }
    else if (itemCount > 4) {
        xCount = 3;
        size = KisIconUtils::SizeHuge;
    }
    else if (itemCount < xCount) {
        xCount = itemCount;
    }

    int yCount = itemCount / xCount;
    if (itemCount % xCount != 0) {
        ++yCount;
    }

    if (yCount > xCount) {
        yCount = xCount;
    }

    // Draw the selected items into the grid cells
    QPixmap dragPixmap(xCount * size + xCount - 1, yCount * size + yCount - 1);
    dragPixmap.fill(Qt::transparent);

    QPainter painter(&dragPixmap);
    int x = 0;
    int y = 0;
    Q_FOREACH (const QModelIndex &selectedIndex, selectedIndexes) {
        const QImage img = selectedIndex.data(int(KisNodeModel::BeginThumbnailRole) + size).value<QImage>();
        painter.drawPixmap(x, y, QPixmap::fromImage(img.scaled(QSize(size, size), Qt::KeepAspectRatio, Qt::SmoothTransformation)));

        x += size + 1;
        if (x >= dragPixmap.width()) {
            x = 0;
            y += size + 1;
        }
        if (y >= dragPixmap.height()) {
            break;
        }
    }

    return dragPixmap;
}

void NodeView::resizeEvent(QResizeEvent * event)
{
    KisNodeViewColorScheme scm;
    header()->setStretchLastSection(false);

    int otherColumnsWidth = scm.visibilityColumnWidth();

    // if layer box is enabled subtract its width from the "Default col".
    if (KisConfig(false).useLayerSelectionCheckbox()) {
        otherColumnsWidth += scm.selectedButtonColumnWidth();
    }
    header()->resizeSection(DEFAULT_COL, event->size().width() - otherColumnsWidth);
    header()->resizeSection(SELECTED_COL, scm.selectedButtonColumnWidth());
    header()->resizeSection(VISIBILITY_COL, scm.visibilityColumnWidth());

    setIndentation(scm.indentation());
    QTreeView::resizeEvent(event);
}

void NodeView::paintEvent(QPaintEvent *event)
{
    event->accept();
    QTreeView::paintEvent(event);
}

void NodeView::drawBranches(QPainter *painter, const QRect &rect,
                               const QModelIndex &index) const
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QStyleOptionViewItem option = viewOptions();
#else
    QStyleOptionViewItem option;
    initViewItemOption(&option);
#endif
    option.rect = rect;
    // This is not really a job for an item delegate, but the logic was already there
    d->delegate.drawBranches(painter, option, index);
}

void NodeView::dropEvent(QDropEvent *ev)
{
    QTreeView::dropEvent(ev);
    DRAG_WHILE_DRAG_WORKAROUND_STOP();
}

int NodeView::cursorPageIndex() const
{
    QSize size(visualRect(model()->index(0, 0, QModelIndex())).width(), visualRect(model()->index(0, 0, QModelIndex())).height());
    int scrollBarValue = verticalScrollBar()->value();

    QPoint cursorPosition = QWidget::mapFromGlobal(QCursor::pos());

    int numberRow = (cursorPosition.y() + scrollBarValue) / size.height();

    //If cursor is at the half button of the page then the move action is performed after the slide, otherwise it is
    //performed before the page
    if (abs((cursorPosition.y() + scrollBarValue) - size.height()*numberRow) > (size.height()/2)) {
        numberRow++;
    }

    if (numberRow > model()->rowCount(QModelIndex())) {
        numberRow = model()->rowCount(QModelIndex());
    }

    return numberRow;
}

void NodeView::dragEnterEvent(QDragEnterEvent *ev)
{
    DRAG_WHILE_DRAG_WORKAROUND_START();

    QVariant data = QVariant::fromValue(
        static_cast<void*>(const_cast<QMimeData*>(ev->mimeData())));
    model()->setData(QModelIndex(), data, KisNodeModel::DropEnabled);

    QTreeView::dragEnterEvent(ev);
}

void NodeView::dragMoveEvent(QDragMoveEvent *ev)
{
    DRAG_WHILE_DRAG_WORKAROUND_START();
    QTreeView::dragMoveEvent(ev);
}

void NodeView::dragLeaveEvent(QDragLeaveEvent *e)
{
    QTreeView::dragLeaveEvent(e);
    DRAG_WHILE_DRAG_WORKAROUND_STOP();
}

bool NodeView::isDragging() const
{
    return m_draggingFlag;
}

void NodeView::setDraggingFlag(bool flag)
{
    m_draggingFlag = flag;
}

void NodeView::slotUpdateIcons()
{
    d->delegate.slotUpdateIcon();
}

void NodeView::slotTouchVisibilityHoldTimeout()
{
    if (!d->touchVisibilityHoldCandidateActive ||
        d->touchVisibilityHoldTriggered ||
        !d->touchVisibilityHoldCandidateIndex.isValid()) {
        return;
    }

    d->touchVisibilityHoldTriggered = true;
    toggleSolo(d->touchVisibilityHoldCandidateIndex);
}

void NodeView::slotTouchSelectOpaqueHoldTimeout()
{
    if (!d->touchSelectOpaqueHoldCandidateActive ||
        d->touchSelectOpaqueHoldTriggered ||
        !d->touchSelectOpaqueHoldCandidateIndex.isValid()) {
        return;
    }

    d->touchSelectOpaqueHoldTriggered = true;
    d->touchSwipeCandidateActive = false;
    d->touchSwipeCandidateIndex = QModelIndex();
    d->touchOptionsCandidateActive = false;
    d->touchOptionsCandidateIndex = QModelIndex();

    if (selectionModel()) {
        selectionModel()->setCurrentIndex(d->touchSelectOpaqueHoldCandidateIndex, QItemSelectionModel::NoUpdate);
    }

    KisMainWindow *mainWindow = KisPart::instance()->currentMainwindow();
    KisKActionCollection *actionCollection = mainWindow ? mainWindow->actionCollection() : nullptr;
    QAction *action = actionCollection ? actionCollection->action(QStringLiteral("selectopaque")) : nullptr;
    if (action) {
        action->trigger();
    }
}

void NodeView::slotScrollerStateChanged(QScroller::State state){
    if (state != QScroller::Inactive && d->touchVisibilityHoldCandidateActive) {
        if (d->touchVisibilityHoldTimer) {
            d->touchVisibilityHoldTimer->stop();
        }
        d->touchVisibilityHoldCandidateActive = false;
        d->touchVisibilityHoldTriggered = false;
        d->touchVisibilityHoldCandidateIndex = QModelIndex();
        d->touchVisibilityHoldStartPos = QPoint();
    }
    if (state != QScroller::Inactive && d->touchSelectOpaqueHoldCandidateActive) {
        if (d->touchSelectOpaqueHoldTimer) {
            d->touchSelectOpaqueHoldTimer->stop();
        }
        d->touchSelectOpaqueHoldCandidateActive = false;
        d->touchSelectOpaqueHoldTriggered = false;
        d->touchSelectOpaqueHoldCandidateIndex = QModelIndex();
        d->touchSelectOpaqueHoldStartPos = QPoint();
    }
    KisKineticScroller::updateCursor(this, state);
}

void NodeView::slotConfigurationChanged()
{
    setIndentation(KisNodeViewColorScheme::instance()->indentation());
    updateSelectedCheckboxColumn();
    d->delegate.slotConfigChanged();
}

void NodeView::updateSelectedCheckboxColumn()
{
    KisConfig cfg(false);
    if (cfg.useLayerSelectionCheckbox() == !header()->isSectionHidden(SELECTED_COL)) {
        return;
    }
    header()->setSectionHidden(SELECTED_COL, !cfg.useLayerSelectionCheckbox());
    // add/subtract width based on SELECTED_COL section's visibility
    header()->resizeSection(DEFAULT_COL,
                            size().width()
                                + (cfg.useLayerSelectionCheckbox() ? header()->sectionSize(SELECTED_COL)
                                                                   : -header()->sectionSize(SELECTED_COL)));
}
