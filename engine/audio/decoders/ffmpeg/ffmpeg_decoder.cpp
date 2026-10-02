module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

module engine.audio.decoders.ffmpeg.ffmpeg_decoder;

namespace engine::audio
{
struct AudioDecoder::State
{
	std::vector<std::byte> file;
	std::size_t offset{0};
	AVFormatContext *format{nullptr};
	AVIOContext *io{nullptr};
	AVCodecContext *codec{nullptr};
	SwrContext *resampler{nullptr};
	AVPacket *packet{nullptr};
	AVFrame *frame{nullptr};
	int stream{-1};
	std::uint32_t outputRate{0};
	// Mono converts as mono and is duplicated to both sides at full level
	// (the resampler's upmix would lower it by 3 dB).
	int channels{2};
	std::vector<float> converted;
	// Converted frames not yet handed out.
	std::vector<float> ready;
	std::size_t readyOffset{0};
	bool drained{false}; // the codec has given everything
	bool ended{false};

	~State()
	{
		swr_free(&resampler);
		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&codec);
		if (format != nullptr)
			avformat_close_input(&format);
		if (io != nullptr)
		{
			av_freep(&io->buffer);
			avio_context_free(&io);
		}
	}

	static int ReadPacket(void *opaque, std::uint8_t *buffer, int size)
	{
		State &state = *static_cast<State *>(opaque);
		const std::size_t left = state.file.size() - state.offset;
		if (left == 0)
			return AVERROR_EOF;
		const std::size_t count = std::min(left, static_cast<std::size_t>(size));
		std::memcpy(buffer, state.file.data() + state.offset, count);
		state.offset += count;
		return static_cast<int>(count);
	}

	static std::int64_t Seek(void *opaque, std::int64_t offset, int whence)
	{
		State &state = *static_cast<State *>(opaque);
		const auto size = static_cast<std::int64_t>(state.file.size());
		if ((whence & AVSEEK_SIZE) != 0)
			return size;
		std::int64_t target = offset;
		switch (whence & ~AVSEEK_FORCE)
		{
		case SEEK_CUR: target += static_cast<std::int64_t>(state.offset); break;
		case SEEK_END: target += size; break;
		default: break;
		}
		if (target < 0 || target > size)
			return -1;
		state.offset = static_cast<std::size_t>(target);
		return target;
	}

	bool Open()
	{
		constexpr int bufferSize = 0x8000;
		auto *buffer = static_cast<unsigned char *>(av_malloc(bufferSize));
		if (buffer == nullptr)
			return false;
		io = avio_alloc_context(buffer, bufferSize, 0, this, &ReadPacket, nullptr, &Seek);
		if (io == nullptr)
		{
			av_free(buffer);
			return false;
		}
		format = avformat_alloc_context();
		if (format == nullptr)
			return false;
		format->pb = io;
		format->flags |= AVFMT_FLAG_CUSTOM_IO;
		if (avformat_open_input(&format, nullptr, nullptr, nullptr) < 0)
		{
			format = nullptr; // freed by the failed open
			return false;
		}
		if (avformat_find_stream_info(format, nullptr) < 0)
			return false;
		stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
		if (stream < 0)
			return false;
		const AVCodecParameters *parameters = format->streams[stream]->codecpar;
		const AVCodec *decoder = avcodec_find_decoder(parameters->codec_id);
		if (decoder == nullptr)
			return false;
		codec = avcodec_alloc_context3(decoder);
		if (codec == nullptr || avcodec_parameters_to_context(codec, parameters) < 0 || avcodec_open2(codec, decoder, nullptr) < 0)
			return false;
		packet = av_packet_alloc();
		frame = av_frame_alloc();
		return packet != nullptr && frame != nullptr;
	}

	bool Convert()
	{
		if (resampler == nullptr)
		{
			AVChannelLayout input = frame->ch_layout;
			if (input.nb_channels <= 0)
				av_channel_layout_default(&input, codec->ch_layout.nb_channels > 0 ? codec->ch_layout.nb_channels : 1);
			channels = input.nb_channels == 1 ? 1 : 2;
			AVChannelLayout output{};
			av_channel_layout_default(&output, channels);
			if (swr_alloc_set_opts2(&resampler, &output, AV_SAMPLE_FMT_FLT, static_cast<int>(outputRate), &input,
					static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr) < 0 ||
				swr_init(resampler) < 0)
				return false;
		}
		const int capacity = swr_get_out_samples(resampler, frame->nb_samples);
		if (capacity <= 0)
			return true;
		return Append(capacity, const_cast<const std::uint8_t **>(frame->extended_data), frame->nb_samples) >= 0;
	}

	// Converts (or, with no input, flushes) into `ready` as stereo; the frames converted.
	int Append(int capacity, const std::uint8_t **input, int inputFrames)
	{
		converted.resize(static_cast<std::size_t>(capacity) * static_cast<std::size_t>(channels));
		auto *out = reinterpret_cast<std::uint8_t *>(converted.data());
		const int frames = swr_convert(resampler, &out, capacity, input, inputFrames);
		for (int frame = 0; frame < frames; ++frame)
		{
			const float left = converted[static_cast<std::size_t>(frame * channels)];
			ready.push_back(left);
			ready.push_back(channels == 1 ? left : converted[static_cast<std::size_t>(frame * channels + 1)]);
		}
		return frames;
	}

	void Flush()
	{
		if (resampler == nullptr)
			return;
		while (Append(4096, nullptr, 0) > 0)
		{
		}
	}

	// Decodes until some frames are ready or the file ends.
	void Decode()
	{
		while (ready.size() - readyOffset * 2 == 0 && !drained)
		{
			const int received = avcodec_receive_frame(codec, frame);
			if (received == 0)
			{
				if (!Convert())
					drained = true;
				av_frame_unref(frame);
				continue;
			}
			if (received == AVERROR_EOF)
			{
				Flush();
				drained = true;
				break;
			}
			if (received != AVERROR(EAGAIN))
			{
				drained = true;
				break;
			}
			const int read = av_read_frame(format, packet);
			if (read < 0)
			{
				avcodec_send_packet(codec, nullptr); // drain the codec
				continue;
			}
			if (packet->stream_index == stream)
				avcodec_send_packet(codec, packet);
			av_packet_unref(packet);
		}
	}
};

AudioDecoder::AudioDecoder(std::unique_ptr<State> state) : m_state(std::move(state)) {}
AudioDecoder::~AudioDecoder() = default;

std::unique_ptr<AudioDecoder> AudioDecoder::Open(std::vector<std::byte> file, std::uint32_t outputRate)
{
	// Only real problems: a shipped MP3 without a length header is not one.
	static const bool quiet = (av_log_set_level(AV_LOG_ERROR), true);
	(void)quiet;
	auto state = std::make_unique<State>();
	state->file = std::move(file);
	state->outputRate = outputRate;
	if (state->file.empty() || !state->Open())
		return nullptr;
	return std::unique_ptr<AudioDecoder>(new AudioDecoder(std::move(state)));
}

std::size_t AudioDecoder::Read(std::span<float> out)
{
	State &state = *m_state;
	std::size_t written = 0;
	const std::size_t wanted = out.size() / 2;
	while (written < wanted && !state.ended)
	{
		state.Decode();
		const std::size_t available = state.ready.size() / 2 - state.readyOffset;
		if (available == 0)
		{
			state.ended = state.drained;
			if (state.ended)
				break;
			continue;
		}
		const std::size_t count = std::min(available, wanted - written);
		std::copy_n(state.ready.begin() + static_cast<std::ptrdiff_t>(state.readyOffset * 2), count * 2, out.begin() + static_cast<std::ptrdiff_t>(written * 2));
		written += count;
		state.readyOffset += count;
		if (state.readyOffset * 2 == state.ready.size())
		{
			state.ready.clear();
			state.readyOffset = 0;
		}
	}
	return written;
}

bool AudioDecoder::Ended() const noexcept { return m_state->ended; }

std::optional<PcmBuffer> DecodeAll(std::vector<std::byte> file, std::uint32_t outputRate)
{
	auto decoder = AudioDecoder::Open(std::move(file), outputRate);
	if (decoder == nullptr)
		return std::nullopt;
	PcmBuffer buffer;
	buffer.sampleRate = outputRate;
	std::vector<float> block(8192);
	while (const std::size_t frames = decoder->Read(block))
		buffer.samples.insert(buffer.samples.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(frames * 2));
	return buffer;
}

DecoderFeed::DecoderFeed(std::unique_ptr<AudioDecoder> decoder, std::uint32_t sampleRate, float seconds) :
	m_decoder(std::move(decoder)), m_output(std::make_shared<PcmStream>(static_cast<std::size_t>(static_cast<float>(sampleRate) * seconds))),
	m_scratch(4096 * 2)
{
}

void DecoderFeed::Pump()
{
	for (;;)
	{
		if (m_pending == 0)
		{
			if (m_decoder->Ended())
			{
				m_output->Finish();
				return;
			}
			m_pending = m_decoder->Read(m_scratch);
			m_offset = 0;
			if (m_pending == 0)
				continue;
		}
		const std::size_t taken = m_output->Write(std::span<const float>(m_scratch).subspan(m_offset * 2, m_pending * 2));
		m_offset += taken;
		m_pending -= taken;
		if (m_pending != 0)
			return; // full
	}
}
}
