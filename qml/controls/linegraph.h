// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_CONTROLS_LINEGRAPH_H
#define BITCOIN_QML_CONTROLS_LINEGRAPH_H

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QQueue>
#include <QQuickPaintedItem>

class LineGraph : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor)
    Q_PROPERTY(QColor markerLineColor READ markerLineColor WRITE setMarkerLineColor)
    Q_PROPERTY(int maxSamples READ maxSamples WRITE setMaxSamples)
    Q_PROPERTY(float maxValue READ maxValue WRITE setMaxValue)
    Q_PROPERTY(QQueue<float> valueList READ valueList WRITE setValueList)

    public:
        explicit LineGraph(QQuickItem * parent = nullptr);
        void paint(QPainter * painter) override;

        QColor backgroundColor() const { return m_background_color; };
        QColor borderColor() const { return m_border_color; };
        QColor fillColor() const { return m_fill_color; };
        QColor lineColor() const { return m_line_color; };
        QColor markerLineColor() const { return m_marker_line_color; };
        int maxSamples() const { return m_max_samples; };
        float maxValue() const { return m_max_value; };
        QQueue<float> valueList() const { return m_value_list; };

    public Q_SLOTS:
        void setBackgroundColor(QColor color);
        void setBorderColor(QColor color);
        void setFillColor(QColor color);
        void setLineColor(QColor color);
        void setMarkerLineColor(QColor color);
        void setMaxSamples(int max_samples);
        void setMaxValue(float max_value);
        void setValueList(QQueue<float> value_list);

    private:
        void paintGraphBorder(QPainter * painter);
        void paintMarkerLines(QPainter * painter);
        void paintPath(QPainterPath * painter_path);
        void paintTraffic(QPainter * painter);
        void setupGradient(QPainterPath * painter);

        QColor m_background_color{"#2D2D2D"};
        QColor m_border_color{"#000000"};
        QColor m_fill_color{"#000000"};
        QLinearGradient m_fill_gradient{0, 0, 0, 0};
        QColor m_line_color{"#000000"};
        QColor m_marker_line_color{"#000000"};
        int m_max_samples{0};
        float m_max_value{0};
        QQueue<float> m_value_list;
};

#endif // BITCOIN_QML_CONTROLS_LINEGRAPH_H
