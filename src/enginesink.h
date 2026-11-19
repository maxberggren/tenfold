#pragma once

#include <cmath>

#include "engine.h"
#include "gesture.h"

namespace tf {

// Hands the gesture layer's commands straight to the engine's queue.
class EngineSink : public CommandSink {
public:
    EngineSink(Engine *engine, float aspect) : m_engine(engine), m_aspect(aspect) {}
    void command(const Command &c) override { m_engine->send(c); }
    int loopNear(float x, float y, float radius) const override {
        const LoopRegistry &r = m_engine->registry();
        int best = -1;
        float bestDist = radius;
        for (int i = 0; i < kMaxLoops; ++i) {
            const int id = r.id[i].load(std::memory_order_relaxed);
            if (id < 0)
                continue;
            const float dx = (r.x[i].load(std::memory_order_relaxed) - x) * m_aspect;
            const float dy = r.y[i].load(std::memory_order_relaxed) - y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < bestDist) {
                bestDist = d;
                best = id;
            }
        }
        return best;
    }

    int layerNear(float x, float y, float radius) const override {
        const LoopRegistry &r = m_engine->registry();
        int best = -1;
        float bestDist = radius;
        for (int i = 0; i < kMaxLayers; ++i) {
            const int id = r.layer[i].load(std::memory_order_relaxed);
            if (id < 0)
                continue;
            const float dx = (r.layerX[i].load(std::memory_order_relaxed) - x) * m_aspect;
            const float dy = r.layerY[i].load(std::memory_order_relaxed) - y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < bestDist) {
                bestDist = d;
                best = id;
            }
        }
        return best;
    }

private:
    Engine *m_engine;
    float m_aspect;
};

}
