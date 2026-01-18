/*
 *  SPDX-FileCopyrightText: 2024 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "TouchDockerWidget.h"
#include "ui_TouchDockerWidget.h"

#include <kis_canvas2.h>
#include <KisViewManager.h>
#include <kis_action_manager.h>
#include <kactioncollection.h>
#include <kis_action.h>
#include <kis_canvas_controller.h>
#include <kis_canvas_resource_provider.h>
#include <kis_image_config.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoToolManager.h>

#include <klocalizedstring.h>

#include <QSignalBlocker>
#include <QVariant>

#include <KisMainWindow.h>
#include <kis_config.h>
#include <QDockWidget>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>


TouchDockerWidget::TouchDockerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TouchDockerWidget)
{
    ui->setupUi(this);

    ui->sliderSize->setEnabled(false);
    ui->sliderOpacity->setEnabled(false);
    ui->btnColor->setEnabled(false);
    ui->lblSizeValue->setText(i18n("—"));
    ui->lblOpacityValue->setText(i18n("—"));

    ui->btnUndo->setAutoRepeat(true);
    ui->btnUndo->setAutoRepeatDelay(350);
    ui->btnUndo->setAutoRepeatInterval(60);
    ui->btnRedo->setAutoRepeat(true);
    ui->btnRedo->setAutoRepeatDelay(350);
    ui->btnRedo->setAutoRepeatInterval(60);

    ui->sliderSize->installEventFilter(this);
    ui->sliderOpacity->installEventFilter(this);

    connect(ui->sliderSize, &QSlider::valueChanged, this, &TouchDockerWidget::slotSizeSliderChanged);
    connect(ui->sliderOpacity, &QSlider::valueChanged, this, &TouchDockerWidget::slotOpacitySliderChanged);
    connect(ui->btnColor, &QAbstractButton::clicked, this, &TouchDockerWidget::slotColorButtonClicked);
    connect(ui->btnModify, &QAbstractButton::pressed, this, &TouchDockerWidget::slotModifyPressed);
    connect(ui->btnModify, &QAbstractButton::released, this, &TouchDockerWidget::slotModifyReleased);

    // In Touch Mode, we open the color panel docker instead of the desktop color dialog.
    disconnect(ui->btnColor, SIGNAL(clicked()), ui->btnColor, SLOT(_k_chooseColor()));
}

TouchDockerWidget::~TouchDockerWidget()
{
    unsetCanvas();
    delete ui;
}

namespace {

int touchFineControlSliderValueAt(const QSlider *slider, int mouseY)
{
    QStyleOptionSlider opt;
    opt.initFrom(slider);
    opt.subControls = QStyle::SC_SliderGroove | QStyle::SC_SliderHandle;
    opt.orientation = slider->orientation();
    opt.minimum = slider->minimum();
    opt.maximum = slider->maximum();
    opt.sliderPosition = slider->sliderPosition();
    opt.sliderValue = slider->value();
    opt.singleStep = slider->singleStep();
    opt.pageStep = slider->pageStep();
    opt.upsideDown = slider->orientation() == Qt::Horizontal ? slider->invertedAppearance()
                                                             : !slider->invertedAppearance();

    const QRect groove =
        slider->style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, slider);
    const QRect handle =
        slider->style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, slider);

    if (!groove.isValid() || handle.height() <= 0) {
        return slider->value();
    }

    const int span = qMax(1, groove.height() - handle.height());
    const int centeredY = mouseY - groove.y() - handle.height() / 2;
    const int pos = qBound(0, centeredY, span);

    return QStyle::sliderValueFromPosition(slider->minimum(),
                                           slider->maximum(),
                                           pos,
                                           span,
                                           opt.upsideDown);
}

} // namespace

bool TouchDockerWidget::eventFilter(QObject *watched, QEvent *event)
{
    QSlider *slider = qobject_cast<QSlider *>(watched);
    if (!slider || (slider != ui->sliderSize && slider != ui->sliderOpacity)) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && mouseEvent->source() != Qt::MouseEventNotSynthesized) {
            m_touchFineControlSlider = slider;
            m_touchFineControlStartPos = mouseEvent->pos();
        }
        break;
    }
    case QEvent::MouseMove: {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!m_touchFineControlSlider || m_touchFineControlSlider != slider) {
            break;
        }
        if (mouseEvent->source() == Qt::MouseEventNotSynthesized) {
            break;
        }
        if (!(mouseEvent->buttons() & Qt::LeftButton)) {
            break;
        }

        const QPoint delta = mouseEvent->pos() - m_touchFineControlStartPos;

        const qreal scale = qBound<qreal>(0.15, 1.0 / (1.0 + qAbs(delta.x()) / 80.0), 1.0);
        const int effectiveY = m_touchFineControlStartPos.y() + qRound(delta.y() * scale);

        const int value = touchFineControlSliderValueAt(slider, effectiveY);
        slider->setSliderPosition(value);
        slider->setValue(value);

        mouseEvent->accept();
        return true;
    }
    case QEvent::MouseButtonRelease: {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_touchFineControlSlider == slider) {
            m_touchFineControlSlider = nullptr;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void TouchDockerWidget::setSizeValueLabel(int value)
{
    ui->lblSizeValue->setText(i18n("%1 px", value));
}

void TouchDockerWidget::setOpacityValueLabel(int value)
{
    ui->lblOpacityValue->setText(i18n("%1%", value));
}

void TouchDockerWidget::setCanvas(KisCanvas2 *canvas)
{
    if (!canvas) {
        unsetCanvas();
        return;
    }

    if (m_canvas == canvas) {
        return;
    }

    unsetCanvas();

    m_canvas = canvas;

    auto action = [canvas] (const QString &id) {
        QAction *action = canvas->viewManager()->actionManager()->actionByName(id);
        if (!action) {
            return canvas->canvasController()->actionCollection()->action(id);
        }

        return action;
    };

    ui->btnUndo->setAssociatedAction(action("edit_undo"));
    ui->btnRedo->setAssociatedAction(action("edit_redo"));

    // Procreate's "Modify" button is most often used as a temporary eyedropper.
    // For v1, map it to Krita's Color Sampler tool and make it "temporary"
    // (press = switch; release = return to prior tool).
    m_modifyAction = action("KritaSelected/KisToolColorSampler");
    ui->btnModify->setAssociatedAction(m_modifyAction);
    if (m_modifyAction) {
        disconnect(ui->btnModify, &QToolButton::clicked, m_modifyAction, &QAction::trigger);
    }
    QAction *actionsSheet = action("touch_actions_sheet");
    if (!actionsSheet) {
        actionsSheet = action("command_bar_open"); // fallback
    }
    ui->btnActions->setAssociatedAction(actionsSheet);

    KisCanvasResourceProvider *provider = canvas->viewManager()->canvasResourceProvider();
    if (!provider || !provider->resourceManager()) {
        return;
    }

    {
        const int maxBrushSize = KisImageConfig(true).maxBrushSize();
        const int currentSize = qBound(1, qRound(provider->size()), maxBrushSize);
        QSignalBlocker b(ui->sliderSize);
        ui->sliderSize->setRange(1, maxBrushSize);
        ui->sliderSize->setValue(currentSize);
        ui->sliderSize->setEnabled(true);
    }
    setSizeValueLabel(ui->sliderSize->value());

    {
        const int currentOpacity = qBound(0, qRound(provider->opacity() * 100.0), 100);
        QSignalBlocker b(ui->sliderOpacity);
        ui->sliderOpacity->setRange(0, 100);
        ui->sliderOpacity->setValue(currentOpacity);
        ui->sliderOpacity->setEnabled(true);
    }
    setOpacityValueLabel(ui->sliderOpacity->value());

    m_resourceManager = provider->resourceManager();

    {
        const KoColor fg = m_resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>();
        ui->btnColor->setColor(fg);
        ui->btnColor->setEnabled(true);
        ui->btnColor->setToolTip(i18n("Color"));
    }

    m_resourceChangedConnection =
        connect(m_resourceManager, &KoCanvasResourceProvider::canvasResourceChanged,
                this, &TouchDockerWidget::slotCanvasResourceChanged);
}

void TouchDockerWidget::slotSizeSliderChanged(int value)
{
    setSizeValueLabel(value);

    if (!m_canvas) {
        return;
    }

    KisCanvasResourceProvider *provider = m_canvas->viewManager()->canvasResourceProvider();
    if (!provider) {
        return;
    }

    provider->setSize(value);
}

void TouchDockerWidget::slotOpacitySliderChanged(int value)
{
    setOpacityValueLabel(value);

    if (!m_canvas) {
        return;
    }

    KisCanvasResourceProvider *provider = m_canvas->viewManager()->canvasResourceProvider();
    if (!provider) {
        return;
    }

    provider->setOpacity(qBound<qreal>(0.0, value / 100.0, 1.0));
}

void TouchDockerWidget::unsetCanvas()
{
    if (m_modifyTouchPaintingForced && m_modifyTouchPaintingBefore >= 0) {
        KisConfig writeCfg(false);
        writeCfg.setTouchPainting(KisConfig::TouchPainting(m_modifyTouchPaintingBefore));
    }
    m_modifyTouchPaintingBefore = -1;
    m_modifyTouchPaintingForced = false;

    ui->btnUndo->setAssociatedAction(nullptr);
    ui->btnRedo->setAssociatedAction(nullptr);
    ui->btnColor->setEnabled(false);
    ui->btnModify->setAssociatedAction(nullptr);
    ui->btnActions->setAssociatedAction(nullptr);

    m_modifyAction = nullptr;
    m_modifyReturnToolId.clear();
    m_modifyHeld = false;

    QObject::disconnect(m_resourceChangedConnection);
    m_resourceChangedConnection = QMetaObject::Connection();
    m_resourceManager = nullptr;
    m_canvas = nullptr;

    {
        QSignalBlocker b(ui->sliderSize);
        ui->sliderSize->setEnabled(false);
        ui->sliderSize->setRange(1, 1000);
        ui->sliderSize->setValue(1);
    }
    ui->lblSizeValue->setText(i18n("—"));

    {
        QSignalBlocker b(ui->sliderOpacity);
        ui->sliderOpacity->setEnabled(false);
        ui->sliderOpacity->setRange(0, 100);
        ui->sliderOpacity->setValue(0);
    }
    ui->lblOpacityValue->setText(i18n("—"));
}

void TouchDockerWidget::slotCanvasResourceChanged(int key, const QVariant &value)
{
    if (!m_canvas) {
        return;
    }

    if (key == KoCanvasResource::Opacity) {
        const int opacity = qBound(0, qRound(value.toReal() * 100.0), 100);
        if (ui->sliderOpacity->value() != opacity) {
            QSignalBlocker b(ui->sliderOpacity);
            ui->sliderOpacity->setValue(opacity);
        }
        setOpacityValueLabel(opacity);
    } else if (key == KoCanvasResource::Size) {
        const int size = qRound(value.toReal());
        const int clamped = qBound(ui->sliderSize->minimum(), size, ui->sliderSize->maximum());
        if (ui->sliderSize->value() != clamped) {
            QSignalBlocker b(ui->sliderSize);
            ui->sliderSize->setValue(clamped);
        }
        setSizeValueLabel(clamped);
    } else if (key == KoCanvasResource::ForegroundColor) {
        if (ui->btnColor) {
            ui->btnColor->setColor(value.value<KoColor>());
        }
    }

}

void TouchDockerWidget::slotColorButtonClicked()
{
    if (!m_canvas) {
        return;
    }

    KisViewManager *viewManager = m_canvas->viewManager();
    KisMainWindow *mainWindow = viewManager ? viewManager->mainWindow() : nullptr;
    if (!mainWindow) {
        return;
    }

    if (QDockWidget *dock = mainWindow->dockWidget(QStringLiteral("ColorSelectorNg"))) {
        dock->show();
        dock->raise();
        return;
    }
}

void TouchDockerWidget::slotModifyPressed()
{
    if (!m_canvas || !m_modifyAction || m_modifyHeld) {
        return;
    }

    KoToolManager *toolManager = KoToolManager::instance();
    if (!toolManager) {
        return;
    }

    const QString activeToolId = toolManager->activeToolId();
    if (activeToolId.isEmpty() || activeToolId == QStringLiteral("KritaSelected/KisToolColorSampler")) {
        return;
    }

    // Procreate-style behavior: while Modify (eyedropper) is held, we want
    // a single finger to sample colors (not pan the canvas). Krita's touch
    // shortcut system prioritizes one-finger gestures when touch painting is
    // disabled, so temporarily enable touch painting while the Color Sampler
    // tool is active, and restore the user's setting on release.
    if (m_modifyTouchPaintingBefore < 0) {
        KisConfig cfg(true);
        const KisConfig::TouchPainting before = cfg.touchPainting();
        m_modifyTouchPaintingBefore = int(before);
        if (before != KisConfig::TOUCH_PAINTING_ENABLED) {
            KisConfig writeCfg(false);
            writeCfg.setTouchPainting(KisConfig::TOUCH_PAINTING_ENABLED);
            m_modifyTouchPaintingForced = true;
        }
    }

    m_modifyReturnToolId = activeToolId;
    m_modifyHeld = true;
    m_modifyAction->trigger();
}

void TouchDockerWidget::slotModifyReleased()
{
    if (!m_modifyHeld) {
        return;
    }
    m_modifyHeld = false;

    if (m_modifyReturnToolId.isEmpty()) {
        return;
    }

    if (KoToolManager *toolManager = KoToolManager::instance()) {
        toolManager->switchToolRequested(m_modifyReturnToolId);
    }

    m_modifyReturnToolId.clear();

    if (m_modifyTouchPaintingForced && m_modifyTouchPaintingBefore >= 0) {
        KisConfig writeCfg(false);
        writeCfg.setTouchPainting(KisConfig::TouchPainting(m_modifyTouchPaintingBefore));
    }

    m_modifyTouchPaintingBefore = -1;
    m_modifyTouchPaintingForced = false;
}
