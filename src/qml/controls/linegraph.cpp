// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/controls/linegraph.h>

#include <QBrush>
#include <QColor>
#include <QLinearGradient>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QQueue>
#include <QQuickPaintedItem>

LineGraph::LineGraph(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFillColor(m_background_color);
}

void LineGraph::setBackgroundColor(QColor color)
{
    m_background_color = color;
    setFillColor(color);
}

void LineGraph::setBorderColor(QColor color)
{
    m_border_color = color;
    update();
}

void LineGraph::setFillColor(QColor color)
{
    m_fill_color = color;
    update();
}

void LineGraph::setLineColor(QColor color)
{
    m_line_color = color;
    update();
}

void LineGraph::setMarkerLineColor(QColor color)
{
    m_marker_line_color = color;
    update();
}

void LineGraph::setMaxSamples(int max_samples)
{
    m_max_samples = max_samples;
    update();
}

void LineGraph::setMaxValue(float max_value)
{
    m_max_value = max_value;
}

void LineGraph::setValueList(QQueue<float> value_list)
{
    m_value_list = value_list;
    update();
}

void LineGraph::paintGraphBorder(QPainter * painter)
{
    painter->setPen(QPen(m_border_color, 2));
    QRectF rect(0, 0, width(), height());
    painter->drawRect(rect);
}

void LineGraph::paintMarkerLines(QPainter * painter)
{
    painter->setPen(QPen(m_marker_line_color, 1));
    qreal line_spacing = height() / 8;

    for (int i = 0; i < 9; i++) {
        qreal y = i * line_spacing;

        if (i == 4) {
            painter->setPen(QPen(m_border_color, 1));
            painter->drawLine(0, y, width(), y);
            painter->setPen(QPen(m_marker_line_color, 1));
        } else {
             painter->drawLine(0, y, width(), y);
        }
    }
}

void LineGraph::paintPath(QPainterPath * painter_path)
{
    int item_count = std::min(m_max_samples, m_value_list.size());
    qreal h = height();
    qreal w = width();

    if(item_count > 0) {
        int y_pos = h;
        int x_pos = w;
        painter_path->moveTo(x_pos, y_pos);

        for(int i = 0; i < item_count; ++i) {
            x_pos = w - w * i / m_max_samples;
            y_pos = h - ((h - 10) * m_value_list.at(i) / m_max_value);
            painter_path->lineTo(x_pos, y_pos);
        }

        painter_path->lineTo(x_pos - 2, h);
        painter_path->lineTo(0, h);
    }
}

void LineGraph::paintTraffic(QPainter * painter)
{
    painter->setRenderHint(QPainter::Antialiasing);

    if (m_max_value == 0.0f) {
        return;
    }

    if(!m_value_list.empty()) {
        QPainterPath p;
        paintPath(&p);
        setupGradient(&p);
        painter->setPen(QPen(m_line_color, 1));
        painter->drawPath(p);
        painter->fillPath(p, QBrush(m_fill_gradient));
    }

    update();
}

void LineGraph::setupGradient(QPainterPath * painter_path)
{
    QLinearGradient gradient(painter_path->boundingRect().topLeft(), painter_path->boundingRect().bottomLeft());
    gradient.setColorAt(0, QColor(m_fill_color.red(), m_fill_color.green(), m_fill_color.blue(), 191));
    gradient.setColorAt(1, QColor("transparent"));
    m_fill_gradient = gradient;
}

void LineGraph::paint(QPainter *painter)
{
    paintMarkerLines(painter);
    paintTraffic(painter);
    paintGraphBorder(painter);
}
