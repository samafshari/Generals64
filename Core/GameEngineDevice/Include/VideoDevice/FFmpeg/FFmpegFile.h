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

//////// FFmpegFile.h ///////////////////////////
// Stephan Vedder, April 2025
/////////////////////////////////////////////////

#pragma once

#include <Lib/BaseType.h>

#include <functional>
#include <vector>

struct AVFormatContext;
struct AVIOContext;
struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct File;

using FFmpegFrameCallback = std::function<void(AVFrame *, int, int, void *)>;

class FFmpegFile
{
public:
	FFmpegFile();
	// The constructur takes ownership of the file
	explicit FFmpegFile(File *file);
	~FFmpegFile();

	Bool open(File *file);
	void close();
	void setFrameCallback(FFmpegFrameCallback callback) { m_frameCallback = callback; }
	void setUserData(void *user_data) { m_userData = user_data; }
	// Read & decode a packet from the container. Note that we could/should split this step
	Bool decodePacket();
	void seekFrame(int frame_idx);
	Bool hasAudio() const;

	// Audio specific
	Int getSizeForSamples(Int numSamples) const;
	Int getNumChannels() const;
	Int getSampleRate() const;
	Int getBytesPerSample() const;
	Int getAudioSampleFormat() const;

	// Video specific
	Int getWidth() const;
	Int getHeight() const;
	Int getNumFrames() const;
	Int getCurrentFrame() const;
	Int getPixelFormat() const;
	UnsignedInt getFrameTime() const;

private:
	struct FFmpegStream
	{
		AVCodecContext *codec_ctx = nullptr;
		const AVCodec *codec = nullptr;
		Int stream_idx = -1;
		Int stream_type = -1;
		AVFrame *frame = nullptr;
	};

	static Int readPacket(void *opaque, UnsignedByte *buf, Int buf_size);
	static Int64 seekPacket(void *opaque, Int64 offset, Int whence);
	const FFmpegStream *findMatch(int type) const;

	FFmpegFrameCallback 		m_frameCallback = nullptr; ///< Callback for frame processing
	AVFormatContext 			*m_fmtCtx = nullptr; ///< Format context for AVFormat
	AVIOContext 				*m_avioCtx = nullptr; ///< IO context for AVFormat
	AVPacket 					*m_packet = nullptr; ///< Current packet
	std::vector<FFmpegStream> 	m_streams; ///< List of streams in the file
	File 						*m_file = nullptr;	///< File handle for the file
	void 						*m_userData = nullptr; ///< User data for the callback
};
