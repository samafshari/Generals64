// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//////// FFmpegFile.cpp ///////////////////////////
// Stephan Vedder, April 2025
/////////////////////////////////////////////////

#include "VideoDevice/FFmpeg/FFmpegFile.h"
#include "Common/File.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {
double GetStreamFrameRate(const AVStream *stream)
{
	if (stream == nullptr) {
		return 0.0;
	}

	double frame_rate = av_q2d(stream->avg_frame_rate);
	if (frame_rate <= 0.0) {
		frame_rate = av_q2d(stream->r_frame_rate);
	}

	if (frame_rate <= 0.0 && stream->time_base.num > 0 && stream->time_base.den > 0) {
		frame_rate = 1.0 / av_q2d(stream->time_base);
	}

	return frame_rate;
}
}


FFmpegFile::FFmpegFile() {}

FFmpegFile::FFmpegFile(File *file)
{
	open(file);
}

FFmpegFile::~FFmpegFile()
{
	close();
}

Bool FFmpegFile::open(File *file)
{
	DEBUG_ASSERTCRASH(m_file == nullptr, ("already open"));
	DEBUG_ASSERTCRASH(file != nullptr, ("null file pointer"));
	// Quiet ffmpeg: the default AV_LOG_INFO pipes "[mp3 @ 0x...] Estimating
	// duration..." lines to stderr for every clip, drowning out real
	// diagnostics during gameplay. AV_LOG_ERROR still reports real failures.
	av_log_set_level(AV_LOG_ERROR);

// This is required for FFmpeg older than 4.0 -> deprecated afterwards though
#if LIBAVFORMAT_VERSION_MAJOR < 58
	av_register_all();
#endif

	m_file = file;

	// FFmpeg setup
	m_fmtCtx = avformat_alloc_context();
	if (!m_fmtCtx) {
		DEBUG_LOG(("Failed to alloc AVFormatContext"));
		close();
		return false;
	}

	constexpr size_t avio_ctx_buffer_size = 0x10000;
	uint8_t *buffer = static_cast<uint8_t *>(av_malloc(avio_ctx_buffer_size));
	if (buffer == nullptr) {
		DEBUG_LOG(("Failed to alloc AVIOContextBuffer"));
		close();
		return false;
	}

	m_avioCtx = avio_alloc_context(buffer, avio_ctx_buffer_size, 0, file, &readPacket, nullptr, &seekPacket);
	if (m_avioCtx == nullptr) {
		DEBUG_LOG(("Failed to alloc AVIOContext"));
		close();
		return false;
	}

	m_fmtCtx->pb = m_avioCtx;
	m_fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

	int result = avformat_open_input(&m_fmtCtx, nullptr, nullptr, nullptr);
	if (result < 0) {
		char error_buffer[1024];
		av_strerror(result, error_buffer, sizeof(error_buffer));
		DEBUG_LOG(("Failed 'avformat_open_input': %s", error_buffer));
		close();
		return false;
	}

	result = avformat_find_stream_info(m_fmtCtx, nullptr);
	if (result < 0) {
		char error_buffer[1024];
		av_strerror(result, error_buffer, sizeof(error_buffer));
		DEBUG_LOG(("Failed 'avformat_find_stream_info': %s", error_buffer));
		close();
		return false;
	}

	m_streams.resize(m_fmtCtx->nb_streams);
	Bool found_video_stream = false;
	for (unsigned int stream_idx = 0; stream_idx < m_fmtCtx->nb_streams; stream_idx++) {
		AVStream *av_stream = m_fmtCtx->streams[stream_idx];
		const Bool require_stream = av_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
		const AVCodec *input_codec = avcodec_find_decoder(av_stream->codecpar->codec_id);
		if (input_codec == nullptr) {
			DEBUG_LOG(("Codec not supported: '%s'", avcodec_get_name(av_stream->codecpar->codec_id)));
			if (require_stream) {
				close();
				return false;
			}
			continue;
		}

		AVCodecContext *codec_ctx = avcodec_alloc_context3(input_codec);
		if (codec_ctx == nullptr) {
			DEBUG_LOG(("Could not allocate codec context"));
			if (require_stream) {
				close();
				return false;
			}
			continue;
		}

		result = avcodec_parameters_to_context(codec_ctx, av_stream->codecpar);
		if (result < 0) {
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'avcodec_parameters_to_context': %s", error_buffer));
			avcodec_free_context(&codec_ctx);
			if (require_stream) {
				close();
				return false;
			}
			continue;
		}

		// Use all CPU cores for H.264 decoding (critical for AI-upscaled 3200x2400 videos)
		codec_ctx->thread_count = 0; // 0 = auto-detect optimal thread count
		codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

		result = avcodec_open2(codec_ctx, input_codec, nullptr);
		if (result < 0) {
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'avcodec_open2': %s", error_buffer));
			avcodec_free_context(&codec_ctx);
			if (require_stream) {
				close();
				return false;
			}
			continue;
		}

		FFmpegStream &output_stream = m_streams[stream_idx];
		output_stream.codec_ctx = codec_ctx;
		output_stream.codec = input_codec;
		output_stream.stream_type = input_codec->type;
		output_stream.stream_idx = stream_idx;
		output_stream.frame = av_frame_alloc();
		if (output_stream.frame == nullptr) {
			DEBUG_LOG(("Could not allocate frame"));
			avcodec_free_context(&output_stream.codec_ctx);
			output_stream.codec = nullptr;
			output_stream.stream_type = -1;
			output_stream.stream_idx = -1;
			if (require_stream) {
				close();
				return false;
			}
			continue;
		}

		if (output_stream.stream_type == AVMEDIA_TYPE_VIDEO) {
			found_video_stream = true;
		}
	}

	if (!found_video_stream) {
		DEBUG_LOG(("No decodable video stream found"));
		close();
		return false;
	}

	m_packet = av_packet_alloc();

	return true;
}

/**
 * Read an FFmpeg packet from file
 */
int FFmpegFile::readPacket(void *opaque, uint8_t *buf, int buf_size)
{
	File *file = static_cast<File *>(opaque);
	const int read = file->read(buf, buf_size);

	// Streaming protocol requires us to return real errors - when we read less equal 0 we're at EOF
	if (read <= 0)
		return AVERROR_EOF;

	return read;
}

/**
 * Seek within the file for FFmpeg's AVIO layer.
 * Supports SEEK_SET, SEEK_CUR, SEEK_END, and AVSEEK_SIZE.
 */
Int64 FFmpegFile::seekPacket(void *opaque, Int64 offset, Int whence)
{
	File *file = static_cast<File *>(opaque);

	if (whence == AVSEEK_SIZE) {
		return static_cast<Int64>(file->size());
	}

	File::seekMode mode;
	switch (whence & ~AVSEEK_FORCE) {
		case SEEK_SET: mode = File::START; break;
		case SEEK_CUR: mode = File::CURRENT; break;
		case SEEK_END: mode = File::END; break;
		default: return AVERROR(EINVAL);
	}

	Int result = file->seek(static_cast<Int>(offset), mode);
	return (result < 0) ? AVERROR(EIO) : static_cast<Int64>(result);
}

/**
 * close all the open FFmpeg handles for an open file.
 */
void FFmpegFile::close()
{
	if (m_fmtCtx != nullptr) {
		avformat_close_input(&m_fmtCtx);
	}

	for (auto &stream : m_streams) {
		if (stream.codec_ctx != nullptr) {
			avcodec_free_context(&stream.codec_ctx);
			av_frame_free(&stream.frame);
		}
	}
	m_streams.clear();

	if (m_avioCtx != nullptr) {
		av_freep(&m_avioCtx->buffer);
		avio_context_free(&m_avioCtx);
	}

	if (m_packet != nullptr) {
		av_packet_free(&m_packet);
	}

	if (m_file != nullptr) {
		m_file->close();
		m_file = nullptr;
	}
}

Bool FFmpegFile::decodePacket()
{
	DEBUG_ASSERTCRASH(m_fmtCtx != nullptr, ("null format context"));
	DEBUG_ASSERTCRASH(m_packet != nullptr, ("null packet pointer"));

	int result = av_read_frame(m_fmtCtx, m_packet);
	if (result == AVERROR_EOF)
		return false;
	if (result < 0) {
		char error_buffer[1024];
		av_strerror(result, error_buffer, sizeof(error_buffer));
		DEBUG_LOG(("Failed 'av_read_frame': %s", error_buffer));
		return false;
	}

	const int stream_idx = m_packet->stream_index;
	DEBUG_ASSERTCRASH(m_streams.size() > stream_idx, ("stream index out of bounds"));

	auto &stream = m_streams[stream_idx];
	if (stream.codec_ctx == nullptr) {
		av_packet_unref(m_packet);
		return true;
	}

	AVCodecContext *codec_ctx = stream.codec_ctx;
	result = avcodec_send_packet(codec_ctx, m_packet);
	// Check if we need more data
	if (result == AVERROR(EAGAIN)) {
		av_packet_unref(m_packet);
		return true;
	}

	// Handle any other errors
	if (result < 0) {
		char error_buffer[1024];
		av_strerror(result, error_buffer, sizeof(error_buffer));
		DEBUG_LOG(("Failed 'avcodec_send_packet': %s", error_buffer));
		return false;
	}
	av_packet_unref(m_packet);

	// Get all frames in this packet
	while (result >= 0) {
		result = avcodec_receive_frame(codec_ctx, stream.frame);

		// Check if we need more data
		if (result == AVERROR(EAGAIN))
			return true;

		// Handle any other errors
		if (result < 0) {
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'avcodec_receive_frame': %s", error_buffer));
			return false;
		}

		if (m_frameCallback != nullptr) {
			m_frameCallback(stream.frame, stream_idx, stream.stream_type, m_userData);
		}
	}

	return true;
}

void FFmpegFile::seekFrame(int frame_idx)
{
	// Note: not tested, since not used ingame
	for (const auto &stream : m_streams) {
		if (stream.codec_ctx == nullptr || stream.stream_idx < 0) {
			continue;
		}

		Int64 timestamp = av_q2d(m_fmtCtx->streams[stream.stream_idx]->time_base) * frame_idx
			* av_q2d(m_fmtCtx->streams[stream.stream_idx]->avg_frame_rate);
		int result = av_seek_frame(m_fmtCtx, stream.stream_idx, timestamp, AVSEEK_FLAG_ANY);
		if (result < 0) {
			char error_buffer[1024];
			av_strerror(result, error_buffer, sizeof(error_buffer));
			DEBUG_LOG(("Failed 'av_seek_frame': %s", error_buffer));
		}
	}
}

Bool FFmpegFile::hasAudio() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	return stream != nullptr;
}

const FFmpegFile::FFmpegStream *FFmpegFile::findMatch(Int type) const
{
	for (auto &stream : m_streams) {
		if (stream.stream_type == type)
			return &stream;
	}

	return nullptr;
}

Int FFmpegFile::getNumChannels() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->ch_layout.nb_channels;
}

Int FFmpegFile::getSampleRate() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->sample_rate;
}

Int FFmpegFile::getBytesPerSample() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return av_get_bytes_per_sample(stream->codec_ctx->sample_fmt);
}

Int FFmpegFile::getAudioSampleFormat() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return -1;

	return stream->codec_ctx->sample_fmt;
}

Int FFmpegFile::getSizeForSamples(Int numSamples) const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_AUDIO);
	if (stream == nullptr)
		return 0;

	return av_samples_get_buffer_size(nullptr, stream->codec_ctx->ch_layout.nb_channels, numSamples, stream->codec_ctx->sample_fmt, 1);
}

Int FFmpegFile::getHeight() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->height;
}

Int FFmpegFile::getWidth() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return 0;

	return stream->codec_ctx->width;
}

Int FFmpegFile::getNumFrames() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (m_fmtCtx == nullptr || stream == nullptr || m_fmtCtx->streams[stream->stream_idx] == nullptr)
		return 0;

	AVStream *av_stream = m_fmtCtx->streams[stream->stream_idx];
	if (av_stream->nb_frames > 0) {
		return static_cast<Int>(av_stream->nb_frames);
	}

	const double frame_rate = GetStreamFrameRate(av_stream);
	if (frame_rate > 0.0) {
		if (av_stream->duration > 0) {
			return static_cast<Int>(av_stream->duration * av_q2d(av_stream->time_base) * frame_rate + 0.5);
		}

		if (m_fmtCtx->duration > 0) {
			return static_cast<Int>((m_fmtCtx->duration / static_cast<double>(AV_TIME_BASE)) * frame_rate + 0.5);
		}
	}

	return 0;
}

Int FFmpegFile::getCurrentFrame() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return 0;
	return stream->codec_ctx->frame_num;
}

Int FFmpegFile::getPixelFormat() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr)
		return AV_PIX_FMT_NONE;

	return stream->codec_ctx->pix_fmt;
}

UnsignedInt FFmpegFile::getFrameTime() const
{
	const FFmpegStream *stream = findMatch(AVMEDIA_TYPE_VIDEO);
	if (stream == nullptr || m_fmtCtx == nullptr || m_fmtCtx->streams[stream->stream_idx] == nullptr)
		return 33u;

	const double frame_rate = GetStreamFrameRate(m_fmtCtx->streams[stream->stream_idx]);
	if (frame_rate <= 0.0) {
		return 33u;
	}

	const double frame_time = 1000.0 / frame_rate;
	if (frame_time < 1.0) {
		return 1u;
	}

	return static_cast<UnsignedInt>(frame_time + 0.5);
}
