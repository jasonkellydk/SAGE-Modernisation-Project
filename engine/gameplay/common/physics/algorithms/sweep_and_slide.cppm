export module engine.gameplay.common.physics.algorithms.sweep_and_slide;
import std;
export import engine.gameplay.common.physics.components.swept_hull;
export import engine.gameplay.common.physics.components.sweep_contacts;
export import engine.level.collision.collision_scene;

export namespace engine::gameplay {
inline Engine::Math::FixedVector3 SweepAndSlide(const engine::level::CollisionScene3& scene,
    Engine::Math::FixedVector3 position,Engine::Math::FixedVector3 requested,const SweptHull& hull,SweepContacts& contacts) {
    using namespace Engine::Math;
    if(hull.clip_bias<Fixed::One() || !hull.maximum_contacts || hull.maximum_contacts>SweepContacts::Capacity ||
        hull.support_clearance<Fixed{} || hull.blocking_clearance<Fixed{})
        throw std::invalid_argument("invalid swept hull contact response");
    contacts={};const auto start=position;auto remaining=requested;
    for(unsigned iteration=0;iteration<hull.maximum_contacts && remaining!=FixedVector3{};++iteration) {
        const auto hit=scene.Cast({position+hull.offset,hull.extent},remaining,hull.categories,
            hull.ignored_subject ? std::optional(hull.ignored_subject) : std::nullopt);
        if(!hit) {position=position+remaining;break;}
        contacts.normals[contacts.count]=hit->normal;contacts.subjects[contacts.count]=hit->subject;
        contacts.surfaces[contacts.count++]=hit->surface;
        if(hit->starts_overlapping) {contacts.overlapping=1;break;}
        // SAT divides to the nearest fixed fraction. Moving to that rounded
        // fraction can cross the contact by a few spatial quanta. Retire one
        // fraction quantum conservatively so a second sweep never begins
        // inside the plane just contacted. This is numerical clearance, not
        // the game's wall/ground spacing policy.
        const auto fraction=Fixed::FromRaw(std::max<std::int64_t>(0,hit->fraction.Raw()-1));
        const auto clearance=hit->normal.z>=hull.support_normal_z ? hull.support_clearance : hull.blocking_clearance;
        const auto length=Length(remaining);
        const auto advance=length>Fixed{} ? std::max(Fixed{},fraction-clearance/length) : Fixed{};
        position=position+remaining*advance;remaining=remaining*(Fixed::One()-fraction);
        // Clip against accumulated planes. Bias is supplied by game policy;
        // the mechanism itself assumes neither soldier rules nor a tick rate.
        for(unsigned i=0;i<contacts.count;++i) {
            if(const auto dot=Dot(remaining,contacts.normals[i]);dot<Fixed{})
                remaining=remaining-contacts.normals[i]*(dot*hull.clip_bias);
            bool satisfied=true;
            for(unsigned j=0;j<contacts.count;++j) if(Dot(remaining,contacts.normals[j])<Fixed{}) {satisfied=false;break;}
            if(satisfied) break;
        }
    }
    return position-start;
}
}
