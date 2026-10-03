export module games.renegade.content.campaign.conversations;
import std;
export import games.renegade.content.levels.persist_records;

export namespace renegade::content {
struct ConversationRemark {std::uint32_t orator{},text{};std::string animation;};
struct ConversationDefinition {
    std::uint32_t id{},category{},orators{},ai_state{},look_at{},priority{30};
    std::string name;
    Engine::Math::Fixed probability{Engine::Math::Fixed::One()},maximum_distance{};
    bool innate{true},key{},interruptible{true};
    std::vector<ConversationRemark> remarks;
};
struct ConversationCatalog {
    std::vector<ConversationDefinition> entries;
    const ConversationDefinition* Find(std::string_view name) const noexcept {
        const auto equal=[](std::string_view a,std::string_view b) {
            if(a.size()!=b.size()) return false;
            for(std::size_t i=0;i<a.size();++i) {const auto fold=[](char c) {return c>='A' && c<='Z' ? char(c+32) : c;};if(fold(a[i])!=fold(b[i])) return false;}
            return true;
        };
        for(const auto& item:entries) if(equal(item.name,name)) return &item;
        return nullptr;
    }
};
inline std::expected<ConversationCatalog,std::string> ReadConversationManager(persist::Bytes bytes) {
    using namespace persist;ConversationCatalog result;
    const auto manager=Children(bytes);if(!manager) return std::unexpected(manager.error());
    std::set<std::uint32_t> ids;
    for(const auto& category:*manager) {
        if(category.id!=0x08090318) continue;
        if(category.payload.size()<4) return std::unexpected("conversation category lacks its raw ID prefix");
        const auto category_id=U32(category.payload.first(4));if(!category_id || *category_id>1) return std::unexpected("invalid conversation category");
        const auto records=Children(category.payload.subspan(4));if(!records) return std::unexpected(records.error());
        for(const auto& record:*records) {
            if(record.id!=0x08090319) return std::unexpected("invalid conversation category record");
            const auto variables=One(record.payload,0x08090316);if(!variables) return std::unexpected(variables.error());
            const auto fields=Micros(variables->payload);if(!fields) return std::unexpected(fields.error());
            ConversationDefinition conversation;conversation.category=*category_id;std::set<unsigned> seen;
            for(const auto& field:*fields) {
                if(!seen.insert(field.id).second) return std::unexpected("duplicate conversation variable");
                if(field.id==0) {const auto name=Text(field.payload);if(!name) return std::unexpected(name.error());conversation.name=*name;}
                if(field.id==1 || field.id==7 || field.id==8 || field.id==9 || field.id==10) {
                    const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());
                    (field.id==1 ? conversation.id : field.id==7 ? conversation.ai_state : field.id==8 ? conversation.category : field.id==9 ? conversation.look_at : conversation.priority)=*value;
                }
                if(field.id==5 || field.id==12 || field.id==13) {
                    const auto value=Flag(field.payload);if(!value) return std::unexpected(value.error());
                    (field.id==5 ? conversation.innate : field.id==12 ? conversation.interruptible : conversation.key)=*value;
                }
                if(field.id==6 || field.id==11) {const auto value=Scalar(field.payload);if(!value) return std::unexpected(value.error());(field.id==6 ? conversation.probability : conversation.maximum_distance)=*value;}
            }
            const auto children=Children(record.payload);if(!children) return std::unexpected(children.error());
            for(const auto& child:*children) {
                if(child.id==0x08090317) ++conversation.orators;
                if(child.id!=0x08090318) continue;
                const auto remark_variables=One(child.payload,0x01250307);if(!remark_variables) return std::unexpected(remark_variables.error());
                const auto values=Micros(remark_variables->payload);if(!values) return std::unexpected(values.error());
                ConversationRemark remark;std::set<unsigned> remark_seen;
                for(const auto& field:*values) {
                    if(!remark_seen.insert(field.id).second) return std::unexpected("duplicate conversation remark variable");
                    if(field.id==0 || field.id==1) {const auto value=U32(field.payload);if(!value) return std::unexpected(value.error());(field.id==0 ? remark.orator : remark.text)=*value;}
                    if(field.id==2) {const auto value=Text(field.payload);if(!value) return std::unexpected(value.error());remark.animation=*value;}
                }
                if(!remark_seen.contains(0) || !remark_seen.contains(1)) return std::unexpected("conversation remark lacks speaker or text");
                conversation.remarks.push_back(std::move(remark));
            }
            if(!conversation.id || conversation.name.empty() || conversation.category!=*category_id || !ids.insert(conversation.id).second)
                return std::unexpected("invalid or duplicate conversation identity");
            if(conversation.probability<Engine::Math::Fixed{} || conversation.maximum_distance<Engine::Math::Fixed{}) return std::unexpected("invalid conversation scalar");
            for(const auto& remark:conversation.remarks) if(remark.orator>=conversation.orators) return std::unexpected("conversation references missing orator");
            result.entries.push_back(std::move(conversation));
        }
    }
    return result;
}
inline std::expected<ConversationCatalog,std::string> ReadLevelConversations(persist::Bytes dynamic) {
    using namespace persist;const auto root=One(dynamic,0x3c51c461);if(!root) return std::unexpected(root.error());
    // SaveGameManager writes the conversation manager beside CombatScene,
    // rather than inside the game-object subsystem's payload.
    const auto manager=One(root->payload,0x40700);if(!manager) return std::unexpected(manager.error());
    return ReadConversationManager(manager->payload);
}
}
