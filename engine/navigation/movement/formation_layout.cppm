module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

export module engine.navigation.movement.formation_layout;

export namespace navigation {
struct FormationUnit {
    std::uint32_t id = 0;
    float x = 0, y = 0, radius = 0, speed = 0;
    std::uint32_t type = 0, strength = 0;
};
struct FormationBounds { float left, top, right, bottom; };
struct FormationDestination { std::uint32_t id; float x, y, speed; float z=0; };
struct FormationPlan {
    unsigned columns = 0, rows = 0;
    float spacing = 0;
    std::vector<FormationDestination> destinations;
};

// Slot assignment is spatial, with ObjectID as the final tie breaker. Sorting
// before summation also keeps the result independent of selection list order.
FormationPlan planFormation(std::span<const FormationUnit> input, float targetX,
    float targetY, FormationBounds bounds, float minimumSpacing = 22.0f,
    float facing = std::numeric_limits<float>::quiet_NaN(), unsigned requestedColumns = 0)
{
    FormationPlan result;
    if (input.empty() || !std::isfinite(targetX) || !std::isfinite(targetY) ||
        !std::isfinite(bounds.left) || !std::isfinite(bounds.right) ||
        !std::isfinite(bounds.top) || !std::isfinite(bounds.bottom) ||
        !(bounds.left < bounds.right && bounds.top < bounds.bottom) ||
        !(minimumSpacing > 0) || !std::isfinite(minimumSpacing)) return result;
    auto units = std::vector<FormationUnit>(input.begin(), input.end());
    std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    double sumX = 0, sumY = 0;
    float radius = 0;
    for (std::size_t i=0; i<units.size(); ++i) {
        const auto& unit = units[i];
        if ((i && units[i-1].id == unit.id) || !std::isfinite(unit.x) ||
            !std::isfinite(unit.y) || !std::isfinite(unit.radius) ||
            !std::isfinite(unit.speed) || unit.radius < 0 || unit.speed <= 0) return result;
        sumX += unit.x; sumY += unit.y; radius = std::max(radius, unit.radius);
    }
    std::sort(units.begin(),units.end(),[](const auto& a,const auto& b) {
        if (a.strength!=b.strength) return a.strength<b.strength;
        if (a.type!=b.type) return a.type<b.type;
        return a.id<b.id;
    });
    std::vector<std::pair<std::size_t,std::size_t>> groups;
    for (std::size_t begin=0;begin<units.size();) {
        std::size_t end=begin+1;
        while (end<units.size() && units[end].type==units[begin].type &&
               units[end].strength==units[begin].strength) ++end;
        groups.emplace_back(begin,end);
        begin=end;
    }
    const float centerX = static_cast<float>(sumX / units.size());
    const float centerY = static_cast<float>(sumY / units.size());
    float forwardX = targetX-centerX, forwardY = targetY-centerY;
    const float length = std::hypot(forwardX, forwardY);
    if (length > 0.001f) { forwardX /= length; forwardY /= length; }
    else { forwardX = 0; forwardY = 1; }
    if (std::isfinite(facing)) { forwardX = std::cos(facing); forwardY = std::sin(facing); }
    const float sideX = -forwardY, sideY = forwardX;
    const float spacing = std::max(minimumSpacing, 2*radius+2);
    // Prefer a broad compact frontage. Adapt the column count to map bounds
    // before placing slots, rather than clamping individual units together.
    double best = std::numeric_limits<double>::infinity();
    unsigned columns = 0, rows = 0;
    for (unsigned c=1; c<=units.size(); ++c) {
        unsigned r=0;
        for (const auto& [begin,end] : groups) r+=static_cast<unsigned>((end-begin+c-1)/c);
        const float halfWidth = (c-1)*spacing*0.5f;
        const float halfDepth = (r-1)*spacing*0.5f;
        const float extentX = std::abs(sideX)*halfWidth + std::abs(forwardX)*halfDepth + radius;
        const float extentY = std::abs(sideY)*halfWidth + std::abs(forwardY)*halfDepth + radius;
        if (2*extentX > bounds.right-bounds.left || 2*extentY > bounds.bottom-bounds.top) continue;
        const double score = requestedColumns ? std::abs(double(c)-requestedColumns) : std::abs(double(c)/r-1.5);
        if (score < best) { best = score; columns = c; rows = r; }
    }
    if (!columns) return result;
    const auto longitudinal = [&](const auto& unit) { return (unit.x-centerX)*forwardX+(unit.y-centerY)*forwardY; };
    const auto lateral = [&](const auto& unit) { return (unit.x-centerX)*sideX+(unit.y-centerY)*sideY; };
    // Explicit facing uses fixed ID slots: moving units and rotating previews
    // must not reshuffle membership between rows and columns.
    for (const auto& [begin,end] : groups)
    if (!std::isfinite(facing)) std::sort(units.begin()+begin, units.begin()+end, [&](const auto& a, const auto& b) {
        const float first = longitudinal(a), second = longitudinal(b);
        return first != second ? first > second : a.id < b.id;
    });
    std::vector<FormationDestination> slots;
    slots.reserve(units.size());
    double offsetX = 0, offsetY = 0;
    unsigned row=0;
    for (const auto& [groupBegin,groupEnd] : groups) {
    for (std::size_t begin=groupBegin; begin<groupEnd; begin+=columns,++row) {
        const std::size_t end = std::min(begin+columns, groupEnd);
        if (!std::isfinite(facing)) std::sort(units.begin()+begin, units.begin()+end, [&](const auto& a, const auto& b) {
            const float first = lateral(a), second = lateral(b);
            return first != second ? first > second : a.id < b.id;
        });
        for (std::size_t i=begin; i<end; ++i) {
            const float side = (float(end-begin-1)*0.5f-float(i-begin))*spacing;
            const float forward = -float(row)*spacing;
            slots.push_back({units[i].id, side*sideX+forward*forwardX, side*sideY+forward*forwardY, 0});
            offsetX += slots.back().x; offsetY += slots.back().y;
        }
    }
    }
    const float averageX = static_cast<float>(offsetX/slots.size());
    const float averageY = static_cast<float>(offsetY/slots.size());
    float lowX=0, highX=0, lowY=0, highY=0;
    for (auto& slot : slots) {
        slot.x -= averageX; slot.y -= averageY;
        lowX=std::min(lowX,slot.x); highX=std::max(highX,slot.x);
        lowY=std::min(lowY,slot.y); highY=std::max(highY,slot.y);
    }
    const float minimumX=bounds.left+radius-lowX, maximumX=bounds.right-radius-highX;
    const float minimumY=bounds.top+radius-lowY, maximumY=bounds.bottom-radius-highY;
    if (minimumX > maximumX || minimumY > maximumY) return result;
    targetX=std::clamp(targetX,minimumX,maximumX);
    targetY=std::clamp(targetY,minimumY,maximumY);
    for (std::size_t i=0; i<slots.size(); ++i) {
        slots[i].x += targetX; slots[i].y += targetY;
        slots[i].speed=units[i].speed;
    }
    result.columns=columns; result.rows=rows; result.spacing=spacing;
    result.destinations=std::move(slots);
    return result;
}

// Remove the whole cohort's old reservations before resolving any new slot.
// The adapter adjusts each slot against terrain and reserves it immediately,
// so later units cannot claim the same destination. Formation layout never
// slows a unit down to synchronize its arrival with other members.
template<class RemoveGoal, class ResolveGoal>
FormationPlan prepareFormationMove(std::span<const FormationUnit> units,
    float targetX, float targetY, FormationBounds bounds,
    RemoveGoal removeGoal, ResolveGoal resolveGoal, float spacing = 22.0f,
    float facing = std::numeric_limits<float>::quiet_NaN(), unsigned requestedColumns = 0)
{
    auto plan = planFormation(units, targetX, targetY, bounds, spacing, facing, requestedColumns);
    for (const auto& slot : plan.destinations) removeGoal(slot.id);
    for (auto& slot : plan.destinations) resolveGoal(slot);
    return plan;
}
}
