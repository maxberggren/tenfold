#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>
#include <complex>
#include <vector>

#include "backend.h"

// The pad as the player sees it: the trackpad's own shape, its notes and
// materials, every finger with the knob it is turning, loops as rings that
// spin where they were left, and behind it all the sound itself, as a
// landscape of spectra lined up under the notes that made them.
class Surface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *app READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)

public:
    explicit Surface(QQuickItem *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *b);
    QColor accent() const { return m_accent; }
    void setAccent(const QColor &c);

    void paint(QPainter *p) override;

    static QColor roleColor(int role, const QColor &accent);
    static QColor materialColor(float material);

signals:
    void backendChanged();
    void accentChanged();

private:
    void onFrame();
    void updateSpectrum();
    QRectF padRect() const;

    QPointer<Backend> m_backend;
    QColor m_accent{"#FFD60A"};
    QElapsedTimer m_clock;

    struct Ripple {
        float x, y, vel, material;
        qint64 born;
    };
    std::vector<Ripple> m_ripples;
    uint32_t m_seenStrikes = 0;

    // Spectrum landscape: a short history of spectra, each resampled onto the
    // pad's own pitch axis.
    static constexpr int kFft = 4096;
    static constexpr int kColumns = 192;
    static constexpr int kHistory = 44;
    std::vector<float> m_window;
    std::vector<std::complex<float>> m_twiddle;
    std::vector<std::vector<float>> m_history;
    int m_historyHead = 0;
    int m_frameCount = 0;
    std::vector<float> m_columnFreq;
    int m_mappedRoot = -1, m_mappedFamily = -1, m_mappedBright = -1;
    bool m_mappedJust = false;
};
