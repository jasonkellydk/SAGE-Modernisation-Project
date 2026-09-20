module;
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>

export module engine.navigation.diagnostics.frame_capture;

export namespace navigation::diagnostics {
// Opt-in rendered-game measurements. Rows stay in memory during the measured
// interval: CSV formatting and disk writes cannot cause measured frame spikes.
class FrameCapture {
    using Clock=std::chrono::steady_clock;
    struct Row {
        unsigned before,after,objects,width,height;
        bool gameplay,paused,windowed;
        double frame,client,logic;
        unsigned presents=0,presentIntervals=0,latePresentIntervals=0;
        double maximumPresentInterval=0;
    };
    std::string output_;
    std::vector<Row> rows_;
    struct Query {
        unsigned frame,unit,work;
        float x,y,toX,toY;
        int diameter;
        bool ranked,predicate,found;
        double milliseconds;
        std::string unitType;
        unsigned surfaces;
        int radius;
        bool centered;
        std::uint64_t hpaSearches,hpaRefinements;
    };
    std::vector<Query> queries_;
    struct Stage {
        const char* name;
        unsigned frame,unit,module;
        double milliseconds;
    };
    std::vector<Stage> stages_;
    std::map<const char*,double> accumulatedStages_;
    struct Counter { const char* name; std::size_t renderFrame; unsigned logicFrame; std::uint64_t value; };
    std::vector<Counter> counters_;
    struct ResourceCounter { std::string resource; const char* phase; std::size_t renderFrame; unsigned logicFrame; std::uint64_t value; };
    std::vector<ResourceCounter> resources_;
    bool captureStages_=false;
    using ModuleNameResolver=std::string(*)(unsigned);
    ModuleNameResolver moduleName_=nullptr;
    Clock::time_point start_,frameStart_,phaseStart_;
    Clock::time_point lastPresent_;
    bool presented_=false;
    Row current_{};
    double duration_=0;
    bool started_=false,finished_=false;
    static double milliseconds(Clock::duration value) {
        return std::chrono::duration<double,std::milli>(value).count();
    }
public:
    class StageScope {
        FrameCapture* owner_;
        const char* name_;
        unsigned frame_,unit_,module_;
        Clock::time_point start_;
        bool accumulate_;
    public:
        StageScope(FrameCapture* owner,const char* name,unsigned frame,unsigned unit,unsigned module,bool accumulate=false)
            :owner_(owner),name_(name),frame_(frame),unit_(unit),module_(module),accumulate_(accumulate) {
            if (owner_) start_=Clock::now();
        }
        StageScope(const StageScope&)=delete;
        ~StageScope() {
            if (!owner_) return;
            const auto elapsed=milliseconds(Clock::now()-start_);
            if (accumulate_) owner_->accumulatedStages_[name_]+=elapsed;
            else if (elapsed>=0.1) owner_->stages_.push_back({name_,frame_,unit_,module_,elapsed});
        }
    };
    FrameCapture() {
        if (const char* path=std::getenv("SAGE_NAVIGATION_FRAME_CSV")) output_=path;
        if (output_.empty()) return;
        if (const char* duration=std::getenv("SAGE_NAVIGATION_CAPTURE_SECONDS")) duration_=std::atof(duration);
        rows_.reserve(2'000'000);
        queries_.reserve(100'000);
        if (const char* stages=std::getenv("SAGE_NAVIGATION_CAPTURE_STAGES")) captureStages_=std::atoi(stages)!=0;
        // The replay emits over a million nested scope rows. Reserve before
        // map loading so vector growth is not itself a measured gameplay dip.
        if (captureStages_) { stages_.reserve(4'000'000); counters_.reserve(1'000'000); }
    }
    ~FrameCapture() { finish(); }
    bool enabled() const { return !output_.empty() && !finished_; }
    bool detailsEnabled() const { return enabled() && captureStages_; }
    void counter(const char* name,unsigned logicFrame,std::uint64_t value) {
        if (detailsEnabled()) counters_.push_back({name,rows_.size(),logicFrame,value});
    }
    void resourceCounter(const char* resource,const char* phase,unsigned logicFrame,std::uint64_t value) {
        if (detailsEnabled() && value) resources_.push_back({resource?resource:"",phase,rows_.size(),logicFrame,value});
    }
    void setModuleNameResolver(ModuleNameResolver resolver) { moduleName_=resolver; }
    // Names must be static strings. Detailed scopes are opt-in diagnostics;
    // their timing overhead is included in the enclosing rendered frame.
    StageScope measure(const char* name,unsigned frame,unsigned unit=0,unsigned module=0) {
        return {enabled() && captureStages_?this:nullptr,name,frame,unit,module};
    }
    // Sum frequent short operations per rendered frame without storing one
    // diagnostic row per mesh. These totals include clock/observer overhead.
    StageScope accumulate(const char* name) {
        return {detailsEnabled()?this:nullptr,name,0,0,0,true};
    }
    void query(unsigned frame,unsigned unit,unsigned work,float x,float y,float toX,float toY,
        int diameter,bool ranked,bool predicate,bool found,double milliseconds,
        const char* unitType,unsigned surfaces,int radius,bool centered,
        std::uint64_t hpaSearches,std::uint64_t hpaRefinements) {
        if (enabled() && milliseconds>=0.1)
            queries_.push_back({frame,unit,work,x,y,toX,toY,diameter,ranked,predicate,found,milliseconds,
                unitType,surfaces,radius,centered,hpaSearches,hpaRefinements});
    }
    void begin(unsigned frame,bool gameplay,bool paused,unsigned width,unsigned height,bool windowed) {
        if (!enabled()) return;
        frameStart_=Clock::now();
        if (!started_) { start_=frameStart_;started_=true; }
        current_={frame,frame,0,width,height,gameplay,paused,windowed,0,0,0};
    }
    void beginPhase() { if (enabled()) phaseStart_=Clock::now(); }
    // Successful CPU-side present submissions, separate from render-loop rows.
    // This measures the complete interval between submissions (including game
    // logic outside draw), not GPU completion or monitor scanout timing.
    void presented() {
        if (!enabled() || !started_) return;
        const auto now=Clock::now();
        ++current_.presents;
        if (presented_) {
            const auto interval=milliseconds(now-lastPresent_);
            ++current_.presentIntervals;
            if (interval>1000.0/500.0) ++current_.latePresentIntervals;
            if (interval>current_.maximumPresentInterval) current_.maximumPresentInterval=interval;
        }
        lastPresent_=now;presented_=true;
    }
    void endClient() { if (enabled()) current_.client+=milliseconds(Clock::now()-phaseStart_); }
    void endLogic() { if (enabled()) current_.logic+=milliseconds(Clock::now()-phaseStart_); }
    bool end(unsigned frame,unsigned objects) {
        if (!enabled()) return false;
        const auto now=Clock::now();
        current_.after=frame;current_.objects=objects;
        current_.frame=milliseconds(now-frameStart_);
        for (auto& [name,elapsed]:accumulatedStages_) {
            if (elapsed) stages_.push_back({name,current_.before,0,0,elapsed});
            elapsed=0;
        }
        rows_.push_back(current_);
        const bool complete=duration_>0 && milliseconds(now-start_)>=duration_*1000;
        if (complete) finish();
        return complete;
    }
    void finish() {
        if (!enabled()) return;
        finished_=true;
        auto* file=std::fopen(output_.c_str(),"wb");
        if (!file) { std::fprintf(stderr,"Cannot write navigation frame capture: %s\n",output_.c_str());return; }
        std::fprintf(file,"render_frame,logic_before,logic_after,objects,width,height,windowed,gameplay,paused,frame_ms,client_ms,logic_ms,present_count,present_intervals,present_late_500_intervals,present_interval_max_ms\n");
        for (std::size_t i=0;i<rows_.size();++i) {
            const auto& r=rows_[i];
            std::fprintf(file,"%zu,%u,%u,%u,%u,%u,%u,%u,%u,%.6f,%.6f,%.6f,%u,%u,%u,%.6f\n",
                i,r.before,r.after,r.objects,r.width,r.height,unsigned(r.windowed),unsigned(r.gameplay),
                unsigned(r.paused),r.frame,r.client,r.logic,r.presents,r.presentIntervals,
                r.latePresentIntervals,r.maximumPresentInterval);
        }
        std::fclose(file);
        file=std::fopen((output_+".queries.csv").c_str(),"wb");
        if (!file) return;
        std::fprintf(file,"logic_frame,unit,work,x,y,to_x,to_y,diameter,ranked,predicate,found,milliseconds,unit_type,surfaces,radius,centered,hpa_searches,hpa_refinements\n");
        for (const auto& q:queries_)
            std::fprintf(file,"%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%d,%u,%u,%u,%.6f,%s,%u,%d,%u,%llu,%llu\n",
                q.frame,q.unit,q.work,q.x,q.y,q.toX,q.toY,q.diameter,unsigned(q.ranked),
                unsigned(q.predicate),unsigned(q.found),q.milliseconds,q.unitType.c_str(),q.surfaces,q.radius,unsigned(q.centered),
                static_cast<unsigned long long>(q.hpaSearches),static_cast<unsigned long long>(q.hpaRefinements));
        std::fclose(file);
        if (captureStages_) {
            file=std::fopen((output_+".stages.csv").c_str(),"wb");
            if (!file) return;
            std::map<unsigned,std::string> names;
            std::fprintf(file,"stage,logic_frame,unit,module,milliseconds,module_name\n");
            for (const auto& s:stages_) {
                auto [entry,inserted]=names.try_emplace(s.module);
                if (inserted && s.module && moduleName_) entry->second=moduleName_(s.module);
                std::fprintf(file,"%s,%u,%u,%u,%.6f,%s\n",s.name,s.frame,s.unit,s.module,s.milliseconds,entry->second.c_str());
            }
            std::fclose(file);
            file=std::fopen((output_+".counters.csv").c_str(),"wb");
            if (!file) return;
            std::fprintf(file,"counter,render_frame,logic_frame,value\n");
            for (const auto& c:counters_)
                std::fprintf(file,"%s,%zu,%u,%llu\n",c.name,c.renderFrame,c.logicFrame,
                    static_cast<unsigned long long>(c.value));
            std::fclose(file);
            file=std::fopen((output_+".resources.csv").c_str(),"wb");
            if (!file) return;
            std::fprintf(file,"resource,phase,render_frame,logic_frame,bytes\n");
            for (const auto& r:resources_)
                std::fprintf(file,"%s,%s,%zu,%u,%llu\n",r.resource.c_str(),r.phase,r.renderFrame,r.logicFrame,
                    static_cast<unsigned long long>(r.value));
            std::fclose(file);
        }
    }
};
FrameCapture& frameCapture() { static FrameCapture capture;return capture; }
}
