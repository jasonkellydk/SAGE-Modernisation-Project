// Offline Tracy analysis: emit CPU totals for matching shellmap logic frames.
// Build against the same TracyServer library as the capture tool.
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include "server/TracyFileRead.hpp"
#include "server/TracyWorker.hpp"

static int64_t Child_Time(const tracy::Worker& worker, const tracy::ZoneEvent& zone)
{
    if (!zone.HasChildren()) return 0;
    const auto& children = worker.GetZoneChildren(zone.Child());
    int64_t total = 0;
    if (children.is_magic()) {
        const auto& values = reinterpret_cast<const tracy::Vector<tracy::ZoneEvent>&>(children);
        for (const auto& child : values) total += child.End() - child.Start();
    } else {
        for (const auto& child : children) total += child->End() - child->Start();
    }
    return total;
}

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::fprintf(stderr,"Usage: TracyRange capture.tracy first_logic_frame last_logic_frame\n");
        return 1;
    }
    const int first = std::atoi(argv[2]), last = std::atoi(argv[3]);
    auto file = std::unique_ptr<tracy::FileRead>(tracy::FileRead::Open(argv[1]));
    if (!file || first >= last) return 1;
    tracy::Worker worker(*file);
    while (!worker.AreSourceLocationZonesReady())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    int64_t begin = -1, end = -1;
    for (const auto& plot : worker.GetPlots()) {
        if (std::strcmp(worker.GetString(plot->name),"Graphics.LogicFrame") != 0) continue;
        for (const auto& point : plot->data) {
            if (begin < 0 && point.val >= first) begin = point.time.Val();
            if (begin >= 0 && point.val >= last) { end = point.time.Val(); break; }
        }
    }
    if (begin < 0 || end <= begin) return 2;
    std::fprintf(stderr,"Range: %lld..%lld ns\n",static_cast<long long>(begin),static_cast<long long>(end));
    std::puts("name|calls|total_ns|self_ns");
    for (const auto& entry : worker.GetSourceLocationZones()) {
        int64_t total = 0, self = 0;
        uint64_t count = 0;
        for (const auto& item : entry.second.zones) {
            const auto& zone = *item.Zone();
            if (zone.Start() < begin || zone.Start() >= end || zone.End() < zone.Start()) continue;
            const auto duration = zone.End() - zone.Start();
            total += duration;
            self += duration - Child_Time(worker,zone);
            ++count;
        }
        if (!count) continue;
        const auto& source = worker.GetSourceLocation(entry.first);
        const auto* name = worker.GetString(source.name.active ? source.name : source.function);
        std::printf("%s|%llu|%lld|%lld\n",name,static_cast<unsigned long long>(count),
            static_cast<long long>(total),static_cast<long long>(self));
    }
}
