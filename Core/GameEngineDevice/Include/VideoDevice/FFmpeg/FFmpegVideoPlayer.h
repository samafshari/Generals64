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

//////// FFmpegVideoPlayer.h ///////////////////////////
// Stephan Vedder, April 2025
/////////////////////////////////////////////////

#pragma once

//----------------------------------------------------------------------------
//           Includes
//----------------------------------------------------------------------------

#include "GameClient/VideoPlayer.h"

//----------------------------------------------------------------------------
//           Forward References
//----------------------------------------------------------------------------

class FFmpegFile;
struct AVFrame;
struct SwsContext;
struct SwrContext;
#ifdef _WIN32
struct IXAudio2SourceVoice;
#endif

//----------------------------------------------------------------------------
//           Type Defines
//----------------------------------------------------------------------------

//===============================
// FFmpegVideoStream
//===============================

class FFmpegVideoStream : public VideoStream
{
	friend class FFmpegVideoPlayer;

	protected:
		Bool 			m_good = true;			///< Is the stream valid
		Bool 			m_gotFrame = false;		///< Is the frame ready to be displayed
		AVFrame 		*m_frame = nullptr;		///< Current frame
		SwsContext 		*m_swsContext = nullptr;///< SWSContext for scaling
		FFmpegFile		*m_ffmpegFile;			///< The FFmpeg file abstraction
		UnsignedInt64	m_startTime = 0;		///< Time the stream started
		UnsignedInt64	m_lastFrameTime = 0;	///< Time the last frame was displayed
		Int				m_currentFrame = 0;		///< Zero-based index of the currently prepared frame
		Int				m_frameCount = 0;		///< Estimated total frame count
		UnsignedInt		m_frameTime = 33;		///< Frame duration in milliseconds

		// Audio via XAudio2 (Windows) or SDL Audio (other platforms)
#ifdef _WIN32
		IXAudio2SourceVoice *m_audioVoice = nullptr;	///< XAudio2 source voice for video audio
#endif
		SwrContext		*m_swrContext = nullptr;			///< Resampling context (FFmpeg audio -> s16 interleaved)
		Int				m_audioSampleRate = 0;			///< Audio sample rate
		Int				m_audioChannels = 0;			///< Audio channel count

		FFmpegVideoStream(FFmpegFile* file);
		virtual ~FFmpegVideoStream();

		static void onFrame(AVFrame *frame, int stream_idx, int stream_type, void *user_data);

		void pauseAudio();		///< Pause XAudio2 voice (on focus loss)
		void resumeAudio();		///< Resume XAudio2 voice and reset timing (on focus regain)

	public:

		virtual void update();
		virtual Bool	isFrameReady();
		virtual void	frameDecompress();
		virtual void	frameRender( VideoBuffer *buffer );
		virtual void	frameNext();
		virtual Int		frameIndex();
		virtual Int		frameCount();
		virtual void	frameGoto( Int index );
		virtual Int		height();
		virtual Int		width();
};

//===============================
// FFmpegVideoPlayer
//===============================

class FFmpegVideoPlayer : public VideoPlayer
{

	protected:

		VideoStreamInterface* createStream( File* file );

	public:

		virtual void	init();
		virtual void	reset();
		virtual void	update();
		virtual void	deinit();

		FFmpegVideoPlayer();
		~FFmpegVideoPlayer();

		virtual void	loseFocus();
		virtual void	regainFocus();

		virtual VideoStreamInterface*	open( AsciiString movieTitle );
		virtual VideoStreamInterface*	load( AsciiString movieTitle );

		virtual void notifyVideoPlayerOfNewProvider( Bool nowHasValid );
		virtual void initializeBinkWithMiles();
};
