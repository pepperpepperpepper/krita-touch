/*
 *  SPDX-FileCopyrightText: 2024 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TouchDockerWIDGET_H
#define TouchDockerWIDGET_H

#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QWidget>

class QAction;
class KisCanvas2;
class KoCanvasResourceProvider;
class QSlider;

namespace Ui {
class TouchDockerWidget;
}

class TouchDockerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TouchDockerWidget(QWidget *parent = nullptr);
    ~TouchDockerWidget();

    void setCanvas(KisCanvas2 *canvas);
    void unsetCanvas();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setSizeValueLabel(int value);
    void setOpacityValueLabel(int value);

private Q_SLOTS:
    void slotSizeSliderChanged(int value);
    void slotOpacitySliderChanged(int value);
    void slotCanvasResourceChanged(int key, const QVariant &value);
    void slotColorButtonClicked();
    void slotModifyPressed();
    void slotModifyReleased();

private:
    Ui::TouchDockerWidget *ui;
    QPointer<KisCanvas2> m_canvas;
    QPointer<KoCanvasResourceProvider> m_resourceManager;
    QMetaObject::Connection m_resourceChangedConnection;

    QPointer<QAction> m_modifyAction;
    QString m_modifyReturnToolId;
    bool m_modifyHeld {false};
    int m_modifyTouchPaintingBefore {-1};
    bool m_modifyTouchPaintingForced {false};

    QPointer<QSlider> m_touchFineControlSlider;
    QPoint m_touchFineControlStartPos;
};

#endif // TouchDockerWIDGET_H
