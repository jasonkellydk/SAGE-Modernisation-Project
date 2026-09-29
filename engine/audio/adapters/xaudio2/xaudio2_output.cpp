module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>

module engine.audio.adapters.xaudio2.xaudio2_output;

// One float source voice on the mastering voice, kept a few short blocks
// ahead: whenever XAudio2 finishes a block (on its own thread) the mixer
// fills that block again and it is queued behind the others.
namespace engine::audio
{
namespace
{
constexpr std::size_t BlockFrames = 480; // 10 ms at 48 kHz
constexpr std::size_t BlockCount = 3;

class XAudio2Output final : public AudioOutput, private IXAudio2VoiceCallback
{
public:
	explicit XAudio2Output(Mixer &mixer) : m_mixer(mixer)
	{
		for (auto &block : m_blocks)
			block.resize(BlockFrames * 2);
	}

	~XAudio2Output() override
	{
		if (m_source != nullptr)
		{
			m_source->Stop();
			m_source->DestroyVoice();
		}
		if (m_master != nullptr)
			m_master->DestroyVoice();
		if (m_engine != nullptr)
			m_engine->Release();
		if (m_comInitialized)
			CoUninitialize();
	}

	bool Open()
	{
		m_comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
		if (FAILED(XAudio2Create(&m_engine, 0, XAUDIO2_DEFAULT_PROCESSOR)))
			return false;
		if (FAILED(m_engine->CreateMasteringVoice(&m_master, 2, SampleRate)))
			return false;
		WAVEFORMATEX format{};
		format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		format.nChannels = 2;
		format.nSamplesPerSec = SampleRate;
		format.wBitsPerSample = 32;
		format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		if (FAILED(m_engine->CreateSourceVoice(&m_source, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this)))
			return false;
		for (std::size_t block = 0; block < BlockCount; ++block)
			Submit(block);
		return SUCCEEDED(m_source->Start(0));
	}

	std::string_view Name() const noexcept override { return "XAudio2"; }

private:
	void Submit(std::size_t index)
	{
		std::vector<float> &block = m_blocks[index];
		m_mixer.Mix(block);
		XAUDIO2_BUFFER buffer{};
		buffer.AudioBytes = static_cast<UINT32>(block.size() * sizeof(float));
		buffer.pAudioData = reinterpret_cast<const BYTE *>(block.data());
		buffer.pContext = reinterpret_cast<void *>(index);
		m_source->SubmitSourceBuffer(&buffer);
	}

	// IXAudio2VoiceCallback (XAudio2's thread).
	void STDMETHODCALLTYPE OnBufferEnd(void *context) noexcept override { Submit(reinterpret_cast<std::size_t>(context)); }
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
	void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
	void STDMETHODCALLTYPE OnBufferStart(void *) noexcept override {}
	void STDMETHODCALLTYPE OnLoopEnd(void *) noexcept override {}
	void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) noexcept override {}

	Mixer &m_mixer;
	IXAudio2 *m_engine{nullptr};
	IXAudio2MasteringVoice *m_master{nullptr};
	IXAudio2SourceVoice *m_source{nullptr};
	std::array<std::vector<float>, BlockCount> m_blocks;
	bool m_comInitialized{false};
};
}

std::unique_ptr<AudioOutput> OpenXAudio2Output(Mixer &mixer)
{
	auto output = std::make_unique<XAudio2Output>(mixer);
	if (!output->Open())
		return nullptr;
	return output;
}
}
