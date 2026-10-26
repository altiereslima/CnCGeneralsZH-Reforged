// Backend-neutral persistent planar smoke transport for hazard clouds.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloudFlow.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace HazardCloudFlow {
namespace {

constexpr unsigned GridWidth = 192;
constexpr unsigned GridHeight = 96;
constexpr unsigned CellCount = GridWidth * GridHeight;
constexpr unsigned MaxBodies = 32;
constexpr float FixedStep = 1.0f / 60.0f;
constexpr float MaxDisplacement = 18.0f;
constexpr float MaxVelocity = 60.0f;
constexpr float MaxInfantrySpeed = 120.0f;
// Keep transported billows visible at the preview's normal tactical zoom.
constexpr float BodyFlowStrength = 2.2f;

struct Cell {
    float vx, vy;
    float dx, dy;
};

float clamp(float value, float lo, float hi)
{
    return (std::max)(lo, (std::min)(hi, value));
}

void limitVector(float &x, float &y, float limit)
{
    float squared = x * x + y * y;
    if (squared > limit * limit) {
        float scale = limit / std::sqrt(squared);
        x *= scale;
        y *= scale;
    }
}

Cell mix(const Cell &a, const Cell &b, float weight)
{
    return {
        a.vx + (b.vx - a.vx) * weight,
        a.vy + (b.vy - a.vy) * weight,
        a.dx + (b.dx - a.dx) * weight,
        a.dy + (b.dy - a.dy) * weight
    };
}

} // namespace

struct Flow::Impl {
    std::array<Cell, CellCount> cells = {};
    std::array<Cell, CellCount> transported = {};
    std::array<float, CellCount * 4> pixels = {};
    float minX = 0, minY = 0, maxX = 1, maxY = 1;
    float cellWidth = 1.0f / GridWidth, cellHeight = 1.0f / GridHeight;
    double remainder = 0;
    bool configured = false;
    bool healthy = true;
    float peak = 0;
    unsigned bodyCount = 0;

    Cell sample(float x, float y)
    {
        float gx = clamp((x - minX) / cellWidth - 0.5f, 0.0f, float(GridWidth - 1));
        float gy = clamp((y - minY) / cellHeight - 0.5f, 0.0f, float(GridHeight - 1));
        unsigned ix = static_cast<unsigned>(gx), iy = static_cast<unsigned>(gy);
        unsigned nx = (std::min)(ix + 1, GridWidth - 1);
        unsigned ny = (std::min)(iy + 1, GridHeight - 1);
        return mix(mix(cells[iy * GridWidth + ix], cells[iy * GridWidth + nx], gx - ix),
                   mix(cells[ny * GridWidth + ix], cells[ny * GridWidth + nx], gx - ix), gy - iy);
    }

    void transport()
    {
        const float velocityDecay = std::exp(-1.25f * FixedStep);
        const float displacementDecay = std::exp(-1.05f * FixedStep);
        // Conservative explicit diffusion after semi-Lagrangian backtracing. The
        // coefficient is bounded even for accidentally tiny preview domains.
        const float diffuseX = (std::min)(0.12f, 4.0f * FixedStep / (cellWidth * cellWidth));
        const float diffuseY = (std::min)(0.12f, 4.0f * FixedStep / (cellHeight * cellHeight));
        for (unsigned y = 0; y < GridHeight; ++y) {
            for (unsigned x = 0; x < GridWidth; ++x) {
                unsigned index = y * GridWidth + x;
                const Cell &old = cells[index];
                Cell next = sample(minX + (x + 0.5f) * cellWidth - old.vx * FixedStep,
                                   minY + (y + 0.5f) * cellHeight - old.vy * FixedStep);
                const Cell &left = cells[y * GridWidth + (x ? x - 1 : x)];
                const Cell &right = cells[y * GridWidth + (std::min)(x + 1, GridWidth - 1)];
                const Cell &down = cells[(y ? y - 1 : y) * GridWidth + x];
                const Cell &up = cells[(std::min)(y + 1, GridHeight - 1) * GridWidth + x];
                next.vx += (left.vx + right.vx - 2 * old.vx) * diffuseX
                         + (down.vx + up.vx - 2 * old.vx) * diffuseY;
                next.vy += (left.vy + right.vy - 2 * old.vy) * diffuseX
                         + (down.vy + up.vy - 2 * old.vy) * diffuseY;
                next.vx *= velocityDecay;
                next.vy *= velocityDecay;
                next.dx = next.dx * displacementDecay + next.vx * FixedStep;
                next.dy = next.dy * displacementDecay + next.vy * FixedStep;
                transported[index] = next;
            }
        }
    }

    void injectBody(const Body &body)
    {
        float speed = std::sqrt(body.vx * body.vx + body.vy * body.vy);
        // Only motion injects flow. Standing infantry leave the smoke unchanged;
        // this simulation never subtracts density around a body or its wake.
        if (speed <= 0.02f) return;
        float directionX = body.vx / speed;
        float directionY = body.vy / speed;
        float sideX = -directionY, sideY = directionX;
        float radius = body.radius;
        float extent = radius * 5.0f;
        int firstX = (std::max)(0, int(std::floor((body.x - extent - minX) / cellWidth)));
        int lastX = (std::min)(int(GridWidth) - 1, int(std::ceil((body.x + extent - minX) / cellWidth)));
        int firstY = (std::max)(0, int(std::floor((body.y - extent - minY) / cellHeight)));
        int lastY = (std::min)(int(GridHeight) - 1, int(std::ceil((body.y + extent - minY) / cellHeight)));
        for (int y = firstY; y <= lastY; ++y) {
            for (int x = firstX; x <= lastX; ++x) {
                float offsetX = minX + (x + 0.5f) * cellWidth - body.x;
                float offsetY = minY + (y + 0.5f) * cellHeight - body.y;
                float along = (offsetX * directionX + offsetY * directionY) / radius;
                float side = (offsetX * sideX + offsetY * sideY) / radius;
                Cell &cell = transported[y * GridWidth + x];

                float shoulder = std::exp(-along * along / 2.25f - side * side / 2.89f);
                float shoulderSide = side / std::sqrt(side * side + 0.20f);
                float targetAlong = speed * 0.20f * shoulder;
                float targetSide = speed * 0.58f * shoulderSide * shoulder;
                float weight = shoulder;
                // A mirrored pair of vortices rolls the displaced smoke into the
                // wake. The transported field persists after the infantry pass.
                for (int sign = -1; sign <= 1; sign += 2) {
                    float vortexAlong = along + 1.8f;
                    float vortexSide = side - sign * 0.95f;
                    float vortex = std::exp(-(vortexAlong * vortexAlong + vortexSide * vortexSide) / 1.82f);
                    float circulation = -sign * speed * 0.36f;
                    targetAlong += -vortexSide * circulation * vortex;
                    targetSide += vortexAlong * circulation * vortex;
                    weight = (std::max)(weight, vortex);
                }
                targetAlong *= BodyFlowStrength;
                targetSide *= BodyFlowStrength;
                float response = 1.0f - std::exp(-10.0f * weight * FixedStep);
                float desiredX = directionX * targetAlong + sideX * targetSide;
                float desiredY = directionY * targetAlong + sideY * targetSide;
                cell.vx += (desiredX - cell.vx) * response;
                cell.vy += (desiredY - cell.vy) * response;
            }
        }
    }

    void finishStep()
    {
        for (unsigned index = 0; index < CellCount; ++index) {
            Cell &cell = transported[index];
            limitVector(cell.vx, cell.vy, MaxVelocity);
            limitVector(cell.dx, cell.dy, MaxDisplacement);
            // Outflow vanishes smoothly at the domain edge, preventing a clamped
            // sampler from carrying disturbance indefinitely beyond the field.
            unsigned x = index % GridWidth, y = index / GridWidth;
            unsigned edge = (std::min)((std::min)(x, GridWidth - 1 - x), (std::min)(y, GridHeight - 1 - y));
            if (edge < 3) {
                float fade = float(edge) / 3.0f;
                cell.vx *= fade; cell.vy *= fade;
                cell.dx *= fade; cell.dy *= fade;
            }
        }
        cells.swap(transported);
    }

    void publish()
    {
        peak = 0;
        for (unsigned index = 0; index < CellCount; ++index) {
            Cell &cell = cells[index];
            if (!std::isfinite(cell.vx) || !std::isfinite(cell.vy)
                || !std::isfinite(cell.dx) || !std::isfinite(cell.dy)) {
                healthy = false;
                cell = {};
            }
            pixels[index * 4 + 0] = cell.dx;
            pixels[index * 4 + 1] = cell.dy;
            pixels[index * 4 + 2] = 0;
            pixels[index * 4 + 3] = 0;
            peak = (std::max)(peak, std::sqrt(cell.dx * cell.dx + cell.dy * cell.dy) / MaxDisplacement);
        }
    }

    void setDomain(float newMinX, float newMinY, float newMaxX, float newMaxY)
    {
        bool usable = std::isfinite(newMinX) && std::isfinite(newMinY)
            && std::isfinite(newMaxX) && std::isfinite(newMaxY)
            && newMaxX - newMinX >= 2.0f && newMaxY - newMinY >= 2.0f
            && newMaxX - newMinX < 100000.0f && newMaxY - newMinY < 100000.0f;
        if (!usable) {
            configured = false;
            reset();
            healthy = false;
            return;
        }
        minX = newMinX; minY = newMinY; maxX = newMaxX; maxY = newMaxY;
        cellWidth = (maxX - minX) / GridWidth;
        cellHeight = (maxY - minY) / GridHeight;
        configured = true;
        reset();
    }

    void reset()
    {
        cells.fill({});
        transported.fill({});
        pixels.fill(0.0f);
        remainder = 0;
        healthy = true;
        peak = 0;
        bodyCount = 0;
    }

    void step(float deltaSeconds, const Body *bodies, unsigned count)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0 || (count && !bodies)) {
            healthy = false;
            return;
        }
        if (!configured || deltaSeconds == 0) return;
        std::array<Body, MaxBodies> usable = {};
        bodyCount = 0;
        for (unsigned index = 0; index < (std::min)(count, MaxBodies); ++index) {
            Body body = bodies[index];
            if (!std::isfinite(body.x) || !std::isfinite(body.y) || !std::isfinite(body.z)
                || !std::isfinite(body.vx) || !std::isfinite(body.vy)
                || !std::isfinite(body.radius) || !std::isfinite(body.height)
                || body.radius <= 0 || body.height <= 0) {
                healthy = false;
                continue;
            }
            body.radius = clamp(body.radius, 1.0f, 24.0f);
            // Repositioning an NPC must not launch a supersonic wake or leave a
            // swept trail across the entire map. Begin locally at its new position.
            if (std::abs(body.vx) > MaxInfantrySpeed || std::abs(body.vy) > MaxInfantrySpeed
                || body.vx * body.vx + body.vy * body.vy > MaxInfantrySpeed * MaxInfantrySpeed)
                body.vx = body.vy = 0;
            float extent = body.radius * 5.0f;
            if (body.x + extent < minX || body.x - extent > maxX
                || body.y + extent < minY || body.y - extent > maxY) continue;
            usable[bodyCount++] = body;
        }
        // A stalled frame is capped instead of doing an unbounded catch-up burst.
        // Normal 30/60/120 Hz callers execute the same fixed integration steps.
        remainder += (std::min)(deltaSeconds, 0.25f);
        bool advanced = false;
        while (remainder + 1.0e-9 >= FixedStep) {
            transport();
            for (unsigned index = 0; index < bodyCount; ++index) injectBody(usable[index]);
            finishStep();
            remainder -= FixedStep;
            advanced = true;
        }
        if (advanced) publish();
    }

};

Flow::Flow() : impl(new Impl) {}
Flow::~Flow() { delete impl; }

void Flow::setDomain(float minX, float minY, float maxX, float maxY)
{
    impl->setDomain(minX, minY, maxX, maxY);
}
void Flow::reset() { impl->reset(); }
void Flow::step(float deltaSeconds, const Body *bodies, unsigned count)
{
    impl->step(deltaSeconds, bodies, count);
}

unsigned Flow::width() const { return GridWidth; }
unsigned Flow::height() const { return GridHeight; }
float Flow::domainMinX() const { return impl->minX; }
float Flow::domainMinY() const { return impl->minY; }
float Flow::domainMaxX() const { return impl->maxX; }
float Flow::domainMaxY() const { return impl->maxY; }
const float *Flow::data() const { return impl->pixels.data(); }
bool Flow::valid() const { return impl->configured && impl->healthy; }
float Flow::maxDisturbance() const { return impl->peak; }
unsigned Flow::activeBodies() const { return impl->bodyCount; }

} // namespace HazardCloudFlow
