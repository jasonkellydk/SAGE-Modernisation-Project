export module engine.audio.scene.emitter_player;
import std;
export import engine.audio.playback.sound_player;
import engine.gameplay.common.audio.systems.emitter_snapshot_system;
import Engine.Core.Math.FixedPresentation;
export namespace engine::audio {
// Prepared PCM only: scene publication never starts synchronous file I/O.
class PreparedSoundLibrary final:public SoundLibrary {
public:
    std::map<std::string,std::shared_ptr<const PcmBuffer>,std::less<>> buffers;
    std::shared_ptr<const PcmBuffer> Sound(std::string_view name) override {const auto found=buffers.find(name);return found==buffers.end() ? nullptr : found->second;}
    std::shared_ptr<StreamFeed> Stream(std::string_view,Bus) override {return {};}
};
class EmitterPlayer {
public:
    EmitterPlayer(Mixer& mixer,SoundLibrary& library):m_mixer(mixer),m_player(mixer,library) {}
    ~EmitterPlayer() {m_player.StopAllExcept({});}
    EmitterPlayer(const EmitterPlayer&)=delete;EmitterPlayer& operator=(const EmitterPlayer&)=delete;
    void Update(const gameplay::SoundEmitterSnapshots& snapshots,std::span<const SoundEventDefinition> catalog,const Listener& listener) {
        m_mixer.SetListener(listener);m_player.SetListenerPosition(listener.position);m_player.Update();
        for(auto& [key,record]:m_records) record.seen=false;
        snapshots.ForEach([&](const gameplay::SoundEmitterSnapshot& snapshot) {
            const auto key=(std::uint64_t(snapshot.entity.generation)<<32)|snapshot.entity.index;auto& record=m_records[key];record.seen=true;
            if(record.clip!=snapshot.emitter.clip || !snapshot.emitter.enabled) {m_player.Stop(record.handle,false);record.handle=0;record.started=false;record.clip=snapshot.emitter.clip;}
            if(!snapshot.emitter.enabled || !record.clip || record.clip>catalog.size()) return;
            const auto& sound=catalog[record.clip-1];
            const Vec3 position{Engine::Math::ToFloat(snapshot.position.x),Engine::Math::ToFloat(snapshot.position.y),Engine::Math::ToFloat(snapshot.position.z)};
            if(!record.started) {record.handle=m_player.Play(sound,sound.Positional() ? std::optional(position) : std::nullopt);record.started=record.handle!=0;}
            else if(sound.Positional()) m_player.Move(record.handle,position);
        });
        std::erase_if(m_records,[&](const auto& value) {if(value.second.seen) return false;m_player.Stop(value.second.handle,false);return true;});
    }
    std::size_t Playing() const noexcept {return m_player.PlayingCount();}
    void SetPaused(bool paused) {m_player.PauseAll(paused);}
private:
    struct Record {SoundHandle handle{};std::uint32_t clip{};bool started{},seen{};};
    Mixer& m_mixer;SoundPlayer m_player;std::map<std::uint64_t,Record> m_records;
};
}
