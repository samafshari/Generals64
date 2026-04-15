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

//////// FFmpegVideoPlayer.cpp ///////////////////////////
// Stephan Vedder, April 2025
/////////////////////////////////////////////////

//----------------------------------------------------------------------------
//         Includes
//----------------------------------------------------------------------------

#include "Lib/BaseType.h"
#include "VideoDevice/FFmpeg/FFmpegVideoPlayer.h"
#include "Common/AudioAffect.h"
#include "Common/GameAudio.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/Registry.h"
#include "Common/FileSystem.h"

#include "VideoDevice/FFmpeg/FFmpegFile.h"

extern "C" {
	#include <libavcodec/avcodec.h>
	#include <libswscale/swscale.h>
	#include <libswresample/swresample.h>
	#include <libavutil/opt.h>
	#include <libavutil/channel_layout.h>
}

#ifdef _WIN32
#include <xaudio2.h>
#include <windows.h>
#endif
#include <chrono>
#include <limits>

//----------------------------------------------------------------------------
//         Defines
//----------------------------------------------------------------------------
#define VIDEO_LANG_PATH_FORMAT "Data/%s/Movies/%s.%s"
#define VIDEO_PATH	"Data/Movies"
#define VIDEO_EXT		"bik"
#define VIDEO_ENHANCED_PATH	"Data/Movies_Enhanced"
#define VIDEO_ENHANCED_EXT	"mp4"

// Audio output format
static const int AUDIO_OUT_SAMPLE_RATE = 44100;
static const int AUDIO_OUT_CHANNELS = 2;
static const int AUDIO_OUT_BYTES_PER_SAMPLE = 2; // s16

//----------------------------------------------------------------------------
//         XAudio2 access (Windows only)
//----------------------------------------------------------------------------
#ifdef _WIN32
extern IXAudio2* GetXAudio2Device();

// VoiceCallback that frees the submitted buffer when XAudio2 is done with it
struct VideoVoiceCallback : IXAudio2VoiceCallback {
	void STDMETHODCALLTYPE OnBufferEnd(void* pCtx) override {
		av_free(pCtx);
	}
	void STDMETHODCALLTYPE OnStreamEnd() override {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
	void STDMETHODCALLTYPE OnBufferStart(void*) override {}
	void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
	void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

static VideoVoiceCallback g_videoVoiceCallback;
#endif // _WIN32

//----------------------------------------------------------------------------
//         FFmpegVideoPlayer
//----------------------------------------------------------------------------

FFmpegVideoPlayer::FFmpegVideoPlayer()
{
}

FFmpegVideoPlayer::~FFmpegVideoPlayer()
{
	deinit();
}

void FFmpegVideoPlayer::init()
{
	VideoPlayer::init();
}

void FFmpegVideoPlayer::deinit()
{
	VideoPlayer::deinit();
}

void FFmpegVideoPlayer::reset()
{
	VideoPlayer::reset();
}

void FFmpegVideoPlayer::update()
{
	VideoPlayer::update();
}

void FFmpegVideoPlayer::loseFocus()
{
	VideoPlayer::loseFocus();

	for (VideoStream* s = m_firstStream; s != nullptr; s = static_cast<VideoStream*>(s->next()))
	{
		static_cast<FFmpegVideoStream*>(s)->pauseAudio();
	}
}

void FFmpegVideoPlayer::regainFocus()
{
	VideoPlayer::regainFocus();

	for (VideoStream* s = m_firstStream; s != nullptr; s = static_cast<VideoStream*>(s->next()))
	{
		static_cast<FFmpegVideoStream*>(s)->resumeAudio();
	}
}

VideoStreamInterface* FFmpegVideoPlayer::createStream( File* file )
{
	if ( file == nullptr )
	{
		return nullptr;
	}

	FFmpegFile* ffmpegHandle = NEW FFmpegFile();
	if(!ffmpegHandle->open(file))
	{
		OutputDebugStringA("FFmpeg: open() FAILED\n");
		delete ffmpegHandle;
		return nullptr;
	}
	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "FFmpeg: open() OK %dx%d %d frames\n",
			ffmpegHandle->getWidth(), ffmpegHandle->getHeight(), ffmpegHandle->getNumFrames());
		OutputDebugStringA(dbg);
	}

	FFmpegVideoStream *stream = NEW FFmpegVideoStream(ffmpegHandle);
	if (stream == nullptr)
	{
		delete ffmpegHandle;
		return nullptr;
	}

	if (stream->m_frame == nullptr)
	{
		char dbg[256];
		snprintf(dbg, sizeof(dbg), "FFmpeg: first frame FAILED (good=%d gotFrame=%d)\n", stream->m_good, stream->m_gotFrame);
		OutputDebugStringA(dbg);
		stream->close();
		return nullptr;
	}

	if ( stream )
	{
		stream->m_next = m_firstStream;
		stream->m_player = this;
		m_firstStream = stream;
	}

	return stream;
}

VideoStreamInterface* FFmpegVideoPlayer::open( AsciiString movieTitle )
{
	VideoStreamInterface* stream = nullptr;

	const Video* pVideo = getVideo(movieTitle);
	if (pVideo == nullptr) {
		OutputDebugStringA("FFmpeg: video not found in catalog: ");
		OutputDebugStringA(movieTitle.str());
		OutputDebugStringA("\n");
		return nullptr;
	}

	DEBUG_LOG(("FFmpegVideoPlayer::open() - Opening video '%s'", pVideo->m_filename.str()));

	// Try AI-enhanced version first (Real-ESRGAN upscaled)
	File* file = nullptr;
	{
		char enhancedPath[ _MAX_PATH ];
		snprintf( enhancedPath, ARRAY_SIZE(enhancedPath), "%s\\%s.%s", VIDEO_ENHANCED_PATH, pVideo->m_filename.str(), VIDEO_ENHANCED_EXT );
		file = TheFileSystem->openFile(enhancedPath);
		if (file) {
			DEBUG_LOG(("FFmpegVideoPlayer::open() - Opened AI-enhanced file %s", enhancedPath));
		}
	}

	// Fall back to localized original
	if (!file)
	{
		char localizedFilePath[ _MAX_PATH ];
		snprintf( localizedFilePath, ARRAY_SIZE(localizedFilePath), VIDEO_LANG_PATH_FORMAT, GetRegistryLanguage().str(), pVideo->m_filename.str(), VIDEO_EXT );
		file = TheFileSystem->openFile(localizedFilePath);
		if (file) {
			DEBUG_LOG(("FFmpegVideoPlayer::open() - Opened localized file %s", localizedFilePath));
		}
	}

	// Fall back to default path
	if (!file)
	{
		char filePath[ _MAX_PATH ];
		snprintf( filePath, ARRAY_SIZE(filePath), "%s\\%s.%s", VIDEO_PATH, pVideo->m_filename.str(), VIDEO_EXT );
		file = TheFileSystem->openFile(filePath);
		if (file) {
			DEBUG_LOG(("FFmpegVideoPlayer::open() - Opened file %s", filePath));
		} else {
			OutputDebugStringA("FFmpeg: failed to open video file for '");
			OutputDebugStringA(pVideo->m_filename.str());
			OutputDebugStringA("'\n");
		}
	}

	stream = createStream( file );
	return stream;
}

VideoStreamInterface* FFmpegVideoPlayer::load( AsciiString movieTitle )
{
	return open(movieTitle);
}

void FFmpegVideoPlayer::notifyVideoPlayerOfNewProvider( Bool nowHasValid )
{
}

void FFmpegVideoPlayer::initializeBinkWithMiles()
{
}

//----------------------------------------------------------------------------
//         FFmpegVideoStream
//----------------------------------------------------------------------------

FFmpegVideoStream::FFmpegVideoStream(FFmpegFile* file)
: m_ffmpegFile(file)
{
	m_ffmpegFile->setFrameCallback(onFrame);
	m_ffmpegFile->setUserData(this);

	// Set up audio voice for video playback (XAudio2 on Windows)
#ifdef _WIN32
	if (m_ffmpegFile->hasAudio())
	{
		IXAudio2* xa2 = GetXAudio2Device();
		if (xa2)
		{
			m_audioSampleRate = m_ffmpegFile->getSampleRate();
			m_audioChannels = m_ffmpegFile->getNumChannels();
			if (m_audioSampleRate <= 0) m_audioSampleRate = AUDIO_OUT_SAMPLE_RATE;
			if (m_audioChannels <= 0) m_audioChannels = AUDIO_OUT_CHANNELS;

			// Set up swresample to convert whatever FFmpeg gives us to s16 interleaved stereo
			m_swrContext = swr_alloc();
			if (m_swrContext)
			{
				AVChannelLayout outLayout = {};
				AVChannelLayout inLayout = {};
				av_channel_layout_default(&outLayout, AUDIO_OUT_CHANNELS);
				av_channel_layout_default(&inLayout, m_audioChannels);

				swr_alloc_set_opts2(&m_swrContext,
					&outLayout, AV_SAMPLE_FMT_S16, AUDIO_OUT_SAMPLE_RATE,
					&inLayout, (AVSampleFormat)m_ffmpegFile->getAudioSampleFormat(), m_audioSampleRate,
					0, nullptr);
				if (swr_init(m_swrContext) < 0)
				{
					OutputDebugStringA("FFmpeg: swr_init failed\n");
					swr_free(&m_swrContext);
					m_swrContext = nullptr;
				}
			}

			// Create XAudio2 source voice for s16 stereo PCM
			WAVEFORMATEX wfx = {};
			wfx.wFormatTag = WAVE_FORMAT_PCM;
			wfx.nChannels = AUDIO_OUT_CHANNELS;
			wfx.nSamplesPerSec = AUDIO_OUT_SAMPLE_RATE;
			wfx.wBitsPerSample = AUDIO_OUT_BYTES_PER_SAMPLE * 8;
			wfx.nBlockAlign = AUDIO_OUT_CHANNELS * AUDIO_OUT_BYTES_PER_SAMPLE;
			wfx.nAvgBytesPerSec = AUDIO_OUT_SAMPLE_RATE * wfx.nBlockAlign;

			HRESULT hr = xa2->CreateSourceVoice(&m_audioVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &g_videoVoiceCallback);
			if (FAILED(hr))
			{
				OutputDebugStringA("FFmpeg: CreateSourceVoice failed\n");
				m_audioVoice = nullptr;
			}
			else
			{
				// Set volume based on speech volume
				if (TheAudio)
				{
					float vol = TheAudio->getVolume(AudioAffect_Speech) * 0.8f;
					m_audioVoice->SetVolume(vol);
				}
				m_audioVoice->Start();
				OutputDebugStringA("FFmpeg: audio voice created OK\n");
			}
		}
	}
#endif // _WIN32 (XAudio2 audio setup)

	// Decode until we have our first video frame
	while (m_good && m_gotFrame == false)
		m_good = m_ffmpegFile->decodePacket();

	if (m_gotFrame) {
		m_frameCount = m_ffmpegFile->getNumFrames();
		if (m_frameCount <= 0) {
			m_frameCount = std::numeric_limits<Int>::max();
		}

		m_frameTime = m_ffmpegFile->getFrameTime();
		if (m_frameTime == 0u) {
			m_frameTime = 33u;
		}
	}

	m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

FFmpegVideoStream::~FFmpegVideoStream()
{
#ifdef _WIN32
	if (m_audioVoice)
	{
		m_audioVoice->Stop();
		m_audioVoice->FlushSourceBuffers();
		m_audioVoice->DestroyVoice();
		m_audioVoice = nullptr;
	}
#endif
	if (m_swrContext)
	{
		swr_free(&m_swrContext);
	}
	av_frame_free(&m_frame);
	sws_freeContext(m_swsContext);
	delete m_ffmpegFile;
}

void FFmpegVideoStream::onFrame(AVFrame *frame, int stream_idx, int stream_type, void *user_data)
{
	FFmpegVideoStream *videoStream = static_cast<FFmpegVideoStream *>(user_data);

	if (stream_type == AVMEDIA_TYPE_VIDEO) {
		av_frame_free(&videoStream->m_frame);
		videoStream->m_frame = av_frame_clone(frame);
		videoStream->m_gotFrame = true;
	}
#ifdef _WIN32
	else if (stream_type == AVMEDIA_TYPE_AUDIO && videoStream->m_audioVoice && videoStream->m_swrContext) {
		// Resample to s16 interleaved stereo and submit to XAudio2
		int outSamples = swr_get_out_samples(videoStream->m_swrContext, frame->nb_samples);
		if (outSamples <= 0) return;

		int bufSize = outSamples * AUDIO_OUT_CHANNELS * AUDIO_OUT_BYTES_PER_SAMPLE;
		uint8_t* outBuf = (uint8_t*)av_malloc(bufSize);
		if (!outBuf) return;

		uint8_t* outPtrs[1] = { outBuf };
		int converted = swr_convert(videoStream->m_swrContext,
			outPtrs, outSamples,
			(const uint8_t**)frame->data, frame->nb_samples);

		if (converted <= 0) {
			av_free(outBuf);
			return;
		}

		int actualSize = converted * AUDIO_OUT_CHANNELS * AUDIO_OUT_BYTES_PER_SAMPLE;

		XAUDIO2_BUFFER xbuf = {};
		xbuf.AudioBytes = actualSize;
		xbuf.pAudioData = outBuf;
		xbuf.pContext = outBuf; // passed to OnBufferEnd for freeing

		HRESULT hr = videoStream->m_audioVoice->SubmitSourceBuffer(&xbuf);
		if (FAILED(hr)) {
			av_free(outBuf);
		}
	}
#endif
}

void FFmpegVideoStream::pauseAudio()
{
	if (m_audioVoice)
	{
		m_audioVoice->Stop();
		m_audioVoice->FlushSourceBuffers();
	}
}

void FFmpegVideoStream::resumeAudio()
{
	if (m_audioVoice)
	{
		if (TheAudio)
		{
			float vol = TheAudio->getVolume(AudioAffect_Speech) * 0.8f;
			m_audioVoice->SetVolume(vol);
		}
		m_audioVoice->Start();
	}

	// Reset the playback clock so the video doesn't try to catch up for lost time
	m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
		- static_cast<uint64_t>(m_frameTime) * static_cast<uint64_t>(m_currentFrame);
}

void FFmpegVideoStream::update()
{
}

Bool FFmpegVideoStream::isFrameReady()
{
	if (m_frame == nullptr) {
		return false;
	}

	uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	// First frame is always ready; initialize the clock
	if (m_currentFrame <= 0) {
		m_startTime = now;
		return true;
	}

	// Use absolute timing: frame N should display at startTime + N * frameTime.
	uint64_t targetTime = m_startTime + static_cast<uint64_t>(m_frameTime) * static_cast<uint64_t>(m_currentFrame);
	return now >= targetTime;
}

void FFmpegVideoStream::frameDecompress()
{
}

void FFmpegVideoStream::frameRender( VideoBuffer *buffer )
{
	if (buffer == nullptr || m_frame == nullptr || m_frame->data == nullptr) {
		return;
	}

	// Use actual decoded frame dimensions, which may differ from codec_ctx
	// dimensions for codecs like H.264 that pad to macroblock boundaries.
	const int srcW = m_frame->width;
	const int srcH = m_frame->height;

	// Output directly as BGRA (X8R8G8B8) — this matches the D3D11VideoBuffer format
	// and lets drawVideoBuffer skip its per-pixel conversion loop entirely.
	// Use SWS_BILINEAR since FSR handles sharpening at display time.
	m_swsContext = sws_getCachedContext(m_swsContext,
		srcW,
		srcH,
		static_cast<AVPixelFormat>(m_frame->format),
		buffer->width(),
		buffer->height(),
		AV_PIX_FMT_BGR0,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr);

	uint8_t *buffer_data = static_cast<uint8_t *>(buffer->lock());
	if (buffer_data == nullptr) {
		DEBUG_LOG(("Failed to lock videobuffer"));
		return;
	}

	int dst_strides[] = { (int)buffer->pitch() };
	uint8_t *dst_data[] = { buffer_data };
	sws_scale(m_swsContext, m_frame->data, m_frame->linesize, 0, srcH, dst_data, dst_strides);
	buffer->unlock();
}

void FFmpegVideoStream::frameNext()
{
	m_gotFrame = false;
	while (m_good && m_gotFrame == false)
		m_good = m_ffmpegFile->decodePacket();

	if (m_gotFrame) {
		++m_currentFrame;
		if (m_currentFrame >= m_frameCount) {
			m_frameCount = m_currentFrame + 1;
		}
	} else {
		m_frameCount = m_currentFrame + 1;
	}
}

Int FFmpegVideoStream::frameIndex()
{
	return m_currentFrame;
}

Int FFmpegVideoStream::frameCount()
{
	return m_frameCount;
}

void FFmpegVideoStream::frameGoto( Int index )
{
	m_ffmpegFile->seekFrame(index);
}

Int FFmpegVideoStream::height()
{
	return m_ffmpegFile->getHeight();
}

Int FFmpegVideoStream::width()
{
	return m_ffmpegFile->getWidth();
}
