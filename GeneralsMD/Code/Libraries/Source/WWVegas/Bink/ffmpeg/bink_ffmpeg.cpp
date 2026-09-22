/*
 * Bink 1.x surface, implemented on FFmpeg and XAudio2.
 *
 * BINKW32.DLL is a 32-bit binary, so the x64 build cannot bind to the movie player the game ships
 * with.  FFmpeg decodes both of Bink's codecs natively (binkvideo, binkaudio), so the .bik files
 * on disk play unchanged; this file is the same API in front of them.
 *
 * The struct handed back is a BINK with the decoder hanging off the end of it, so BinkVideoStream's
 * reads of Width/Height/Frames/FrameNum keep working.
 */

#include <windows.h>
#include <xaudio2.h>
#include <deque>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "bink.h"

namespace {

const int AUDIO_BUFFERS_AHEAD = 4;
const int MAX_BINK_VOLUME = 32768;

struct Movie
{
	BINK pub;

	AVFormatContext *format;
	AVCodecContext *videoDecoder;
	AVCodecContext *audioDecoder;
	SwsContext *scaler;
	SwrContext *resampler;
	int videoStream;
	int audioStream;

	AVFrame *frame;
	int scaledFormat;
	int scaledWidth;
	int scaledHeight;

	IXAudio2 *xaudio;
	IXAudio2MasteringVoice *master;
	IXAudio2SourceVoice *voice;
	WAVEFORMATEX audioFormat;
	std::deque<std::vector<unsigned char> > audioQueued;
	bool audioEnabled;
	float volume;

	LARGE_INTEGER startTicks;
	LARGE_INTEGER ticksPerSecond;
	bool started;
	bool frameDecoded;
	bool endOfFile;
};

bool soundEnabled = true;

void startClock(Movie *movie)
{
	QueryPerformanceFrequency(&movie->ticksPerSecond);
	QueryPerformanceCounter(&movie->startTicks);
	movie->started = true;
}

double elapsedSeconds(Movie *movie)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (double)(now.QuadPart - movie->startTicks.QuadPart) / (double)movie->ticksPerSecond.QuadPart;
}

double frameDueSeconds(Movie *movie, unsigned int frameNumber)
{
	if (movie->pub.FrameRate == 0) {
		return 0.0;
	}
	const double divisor = movie->pub.FrameRateDiv != 0 ? (double)movie->pub.FrameRateDiv : 1.0;
	return (double)(frameNumber - 1) * divisor / (double)movie->pub.FrameRate;
}

void openAudio(Movie *movie)
{
	if (!soundEnabled || movie->audioStream < 0 || movie->audioDecoder == NULL) {
		return;
	}
	if (FAILED(XAudio2Create(&movie->xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
		return;
	}
	if (FAILED(movie->xaudio->CreateMasteringVoice(&movie->master))) {
		movie->xaudio->Release();
		movie->xaudio = NULL;
		return;
	}

	const int channels = movie->audioDecoder->ch_layout.nb_channels >= 2 ? 2 : 1;
	AVChannelLayout outLayout;
	av_channel_layout_default(&outLayout, channels);
	const int result = swr_alloc_set_opts2(&movie->resampler, &outLayout, AV_SAMPLE_FMT_S16,
		movie->audioDecoder->sample_rate, &movie->audioDecoder->ch_layout, movie->audioDecoder->sample_fmt,
		movie->audioDecoder->sample_rate, 0, NULL);
	av_channel_layout_uninit(&outLayout);
	if (result < 0 || swr_init(movie->resampler) < 0) {
		return;
	}

	memset(&movie->audioFormat, 0, sizeof(movie->audioFormat));
	movie->audioFormat.wFormatTag = WAVE_FORMAT_PCM;
	movie->audioFormat.nChannels = (WORD)channels;
	movie->audioFormat.nSamplesPerSec = movie->audioDecoder->sample_rate;
	movie->audioFormat.wBitsPerSample = 16;
	movie->audioFormat.nBlockAlign = (WORD)(channels * 2);
	movie->audioFormat.nAvgBytesPerSec = movie->audioFormat.nSamplesPerSec * movie->audioFormat.nBlockAlign;

	if (SUCCEEDED(movie->xaudio->CreateSourceVoice(&movie->voice, &movie->audioFormat))) {
		movie->voice->SetVolume(movie->volume);
		movie->voice->Start(0);
		movie->audioEnabled = true;
	}
}

void queueAudioFrame(Movie *movie, AVFrame *frame)
{
	if (!movie->audioEnabled || movie->voice == NULL) {
		return;
	}

	const int maxOutSamples = (int)swr_get_out_samples(movie->resampler, frame->nb_samples);
	std::vector<unsigned char> chunk((size_t)maxOutSamples * movie->audioFormat.nBlockAlign);
	if (chunk.empty()) {
		return;
	}

	uint8_t *destination = &chunk[0];
	const int converted = swr_convert(movie->resampler, &destination, maxOutSamples,
		(const uint8_t **)frame->extended_data, frame->nb_samples);
	if (converted <= 0) {
		return;
	}
	chunk.resize((size_t)converted * movie->audioFormat.nBlockAlign);

	XAUDIO2_VOICE_STATE state;
	movie->voice->GetState(&state);
	while (movie->audioQueued.size() > state.BuffersQueued) {
		movie->audioQueued.pop_front();
	}

	movie->audioQueued.push_back(chunk);
	const std::vector<unsigned char> &stored = movie->audioQueued.back();
	XAUDIO2_BUFFER buffer;
	memset(&buffer, 0, sizeof(buffer));
	buffer.AudioBytes = (UINT32)stored.size();
	buffer.pAudioData = &stored[0];
	movie->voice->SubmitSourceBuffer(&buffer);
}

// Reads packets until the next video frame is decoded, queueing any audio met along the way.
bool decodeNextVideoFrame(Movie *movie)
{
	AVPacket *packet = av_packet_alloc();
	if (packet == NULL) {
		return false;
	}

	bool decoded = false;
	while (!decoded) {
		const int readResult = av_read_frame(movie->format, packet);
		if (readResult < 0) {
			movie->endOfFile = true;
			avcodec_send_packet(movie->videoDecoder, NULL);
			if (avcodec_receive_frame(movie->videoDecoder, movie->frame) == 0) {
				decoded = true;
			}
			break;
		}

		AVCodecContext *decoder = NULL;
		if (packet->stream_index == movie->videoStream) {
			decoder = movie->videoDecoder;
		} else if (packet->stream_index == movie->audioStream && movie->audioEnabled) {
			decoder = movie->audioDecoder;
		}

		if (decoder != NULL && avcodec_send_packet(decoder, packet) == 0) {
			if (decoder == movie->videoDecoder) {
				if (avcodec_receive_frame(movie->videoDecoder, movie->frame) == 0) {
					decoded = true;
				}
			} else {
				AVFrame *audioFrame = av_frame_alloc();
				while (audioFrame != NULL && avcodec_receive_frame(movie->audioDecoder, audioFrame) == 0) {
					queueAudioFrame(movie, audioFrame);
					av_frame_unref(audioFrame);
				}
				if (audioFrame != NULL) {
					av_frame_free(&audioFrame);
				}
			}
		}
		av_packet_unref(packet);
	}

	av_packet_free(&packet);
	return decoded;
}

AVPixelFormat pixelFormatForSurface(unsigned int surfaceType)
{
	switch (surfaceType & 0xff) {
		case BINKSURFACE24:   return AV_PIX_FMT_BGR24;
		case BINKSURFACE24R:  return AV_PIX_FMT_RGB24;
		case BINKSURFACE32:
		case BINKSURFACE32A:  return AV_PIX_FMT_BGRA;
		case BINKSURFACE32R:
		case BINKSURFACE32RA: return AV_PIX_FMT_RGBA;
		case BINKSURFACE565:  return AV_PIX_FMT_RGB565LE;
		case BINKSURFACE555:  return AV_PIX_FMT_RGB555LE;
		default:              return AV_PIX_FMT_BGRA;
	}
}

} // namespace

// =================================================================================================

extern "C" {

HBINK __stdcall BinkOpen(const char *name, unsigned int)
{
	if (name == NULL) {
		return NULL;
	}

	Movie *movie = new Movie;
	memset(&movie->pub, 0, sizeof(movie->pub));
	movie->format = NULL;
	movie->videoDecoder = NULL;
	movie->audioDecoder = NULL;
	movie->scaler = NULL;
	movie->resampler = NULL;
	movie->videoStream = -1;
	movie->audioStream = -1;
	movie->frame = av_frame_alloc();
	movie->scaledFormat = -1;
	movie->scaledWidth = 0;
	movie->scaledHeight = 0;
	movie->xaudio = NULL;
	movie->master = NULL;
	movie->voice = NULL;
	movie->audioEnabled = false;
	movie->volume = 1.0f;
	movie->started = false;
	movie->frameDecoded = false;
	movie->endOfFile = false;

	if (movie->frame == NULL || avformat_open_input(&movie->format, name, NULL, NULL) < 0) {
		BinkClose((HBINK)movie);
		return NULL;
	}
	if (avformat_find_stream_info(movie->format, NULL) < 0) {
		BinkClose((HBINK)movie);
		return NULL;
	}

	movie->videoStream = av_find_best_stream(movie->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (movie->videoStream < 0) {
		BinkClose((HBINK)movie);
		return NULL;
	}
	movie->audioStream = av_find_best_stream(movie->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

	AVStream *video = movie->format->streams[movie->videoStream];
	const AVCodec *videoCodec = avcodec_find_decoder(video->codecpar->codec_id);
	movie->videoDecoder = videoCodec != NULL ? avcodec_alloc_context3(videoCodec) : NULL;
	if (movie->videoDecoder == NULL ||
		avcodec_parameters_to_context(movie->videoDecoder, video->codecpar) < 0 ||
		avcodec_open2(movie->videoDecoder, videoCodec, NULL) < 0) {
		BinkClose((HBINK)movie);
		return NULL;
	}

	if (movie->audioStream >= 0) {
		AVStream *audio = movie->format->streams[movie->audioStream];
		const AVCodec *audioCodec = avcodec_find_decoder(audio->codecpar->codec_id);
		movie->audioDecoder = audioCodec != NULL ? avcodec_alloc_context3(audioCodec) : NULL;
		if (movie->audioDecoder != NULL &&
			(avcodec_parameters_to_context(movie->audioDecoder, audio->codecpar) < 0 ||
			 avcodec_open2(movie->audioDecoder, audioCodec, NULL) < 0)) {
			avcodec_free_context(&movie->audioDecoder);
		}
	}

	movie->pub.Width = (unsigned int)movie->videoDecoder->width;
	movie->pub.Height = (unsigned int)movie->videoDecoder->height;
	movie->pub.FrameNum = 1;
	movie->pub.LastFrameNum = 0;

	AVRational rate = video->avg_frame_rate;
	if (rate.num <= 0 || rate.den <= 0) {
		rate = video->r_frame_rate;
	}
	if (rate.num <= 0 || rate.den <= 0) {
		rate.num = 30;
		rate.den = 1;
	}
	movie->pub.FrameRate = (unsigned int)rate.num;
	movie->pub.FrameRateDiv = (unsigned int)rate.den;

	if (video->nb_frames > 0) {
		movie->pub.Frames = (unsigned int)video->nb_frames;
	} else if (movie->format->duration > 0) {
		const double seconds = (double)movie->format->duration / (double)AV_TIME_BASE;
		movie->pub.Frames = (unsigned int)(seconds * av_q2d(rate) + 0.5);
	} else {
		movie->pub.Frames = 1;
	}

	openAudio(movie);
	startClock(movie);
	return (HBINK)movie;
}

void __stdcall BinkClose(HBINK handle)
{
	Movie *movie = (Movie *)handle;
	if (movie == NULL) {
		return;
	}

	if (movie->voice != NULL) {
		movie->voice->Stop(0);
		movie->voice->DestroyVoice();
	}
	if (movie->master != NULL) {
		movie->master->DestroyVoice();
	}
	if (movie->xaudio != NULL) {
		movie->xaudio->Release();
	}
	if (movie->resampler != NULL) {
		swr_free(&movie->resampler);
	}
	if (movie->scaler != NULL) {
		sws_freeContext(movie->scaler);
	}
	if (movie->frame != NULL) {
		av_frame_free(&movie->frame);
	}
	if (movie->videoDecoder != NULL) {
		avcodec_free_context(&movie->videoDecoder);
	}
	if (movie->audioDecoder != NULL) {
		avcodec_free_context(&movie->audioDecoder);
	}
	if (movie->format != NULL) {
		avformat_close_input(&movie->format);
	}
	delete movie;
}

int __stdcall BinkWait(HBINK handle)
{
	Movie *movie = (Movie *)handle;
	if (movie == NULL) {
		return 0;
	}
	if (!movie->started) {
		startClock(movie);
		return 0;
	}
	return elapsedSeconds(movie) < frameDueSeconds(movie, movie->pub.FrameNum) ? 1 : 0;
}

int __stdcall BinkDoFrame(HBINK handle)
{
	Movie *movie = (Movie *)handle;
	if (movie == NULL) {
		return 1;
	}
	if (movie->frameDecoded) {
		return 0;
	}

	movie->frameDecoded = decodeNextVideoFrame(movie);
	return movie->frameDecoded ? 0 : 1;
}

void __stdcall BinkNextFrame(HBINK handle)
{
	Movie *movie = (Movie *)handle;
	if (movie == NULL) {
		return;
	}

	movie->pub.LastFrameNum = movie->pub.FrameNum;
	if (movie->pub.FrameNum < movie->pub.Frames) {
		movie->pub.FrameNum += 1;
	}
	movie->frameDecoded = false;
}

int __stdcall BinkGoto(HBINK handle, unsigned int frame, int)
{
	Movie *movie = (Movie *)handle;
	if (movie == NULL || movie->format == NULL) {
		return 0;
	}
	if (frame < 1) {
		frame = 1;
	}

	AVStream *video = movie->format->streams[movie->videoStream];
	const double seconds = frameDueSeconds(movie, frame);
	const int64_t timestamp = (int64_t)(seconds / av_q2d(video->time_base));
	if (av_seek_frame(movie->format, movie->videoStream, timestamp, AVSEEK_FLAG_BACKWARD) < 0) {
		return 0;
	}

	avcodec_flush_buffers(movie->videoDecoder);
	if (movie->audioDecoder != NULL) {
		avcodec_flush_buffers(movie->audioDecoder);
	}
	if (movie->voice != NULL) {
		movie->voice->FlushSourceBuffers();
		movie->audioQueued.clear();
	}

	movie->pub.FrameNum = frame;
	movie->frameDecoded = false;
	movie->endOfFile = false;
	QueryPerformanceCounter(&movie->startTicks);
	movie->startTicks.QuadPart -= (LONGLONG)(seconds * (double)movie->ticksPerSecond.QuadPart);
	return 1;
}

int __stdcall BinkCopyToBuffer(HBINK handle, void *dest, int destpitch, unsigned int destheight,
                               unsigned int destx, unsigned int desty, unsigned int flags)
{
	Movie *movie = (Movie *)handle;
	if (movie == NULL || dest == NULL || !movie->frameDecoded || movie->frame->data[0] == NULL) {
		return 0;
	}

	const AVPixelFormat destinationFormat = pixelFormatForSurface(flags);
	const int width = (int)movie->pub.Width;
	const int height = (int)movie->pub.Height;
	const int copyHeight = (int)destheight < height ? (int)destheight : height;

	if (movie->scaler == NULL || movie->scaledFormat != destinationFormat ||
		movie->scaledWidth != width || movie->scaledHeight != height) {
		if (movie->scaler != NULL) {
			sws_freeContext(movie->scaler);
		}
		movie->scaler = sws_getContext(width, height, (AVPixelFormat)movie->frame->format,
			width, height, destinationFormat, SWS_BILINEAR, NULL, NULL, NULL);
		movie->scaledFormat = destinationFormat;
		movie->scaledWidth = width;
		movie->scaledHeight = height;
	}
	if (movie->scaler == NULL) {
		return 0;
	}

	const int bytesPerPixel = av_get_bits_per_pixel(av_pix_fmt_desc_get(destinationFormat)) / 8;
	unsigned char *destinationBase = (unsigned char *)dest + (size_t)desty * destpitch + (size_t)destx * bytesPerPixel;
	uint8_t *destinationPlanes[4] = { destinationBase, NULL, NULL, NULL };
	int destinationStrides[4] = { destpitch, 0, 0, 0 };

	sws_scale(movie->scaler, movie->frame->data, movie->frame->linesize, 0, copyHeight,
		destinationPlanes, destinationStrides);
	return 1;
}

int __stdcall BinkSetVolume(HBINK handle, unsigned int, int volume)
{
	Movie *movie = (Movie *)handle;
	if (movie == NULL) {
		return 0;
	}

	float level = (float)volume / (float)MAX_BINK_VOLUME;
	if (level < 0.0f) level = 0.0f;
	if (level > 1.0f) level = 1.0f;
	movie->volume = level;
	if (movie->voice != NULL) {
		movie->voice->SetVolume(level);
	}
	return 1;
}

void __stdcall BinkSetSoundTrack(unsigned int, unsigned int *)
{
	// The game calls this with zero tracks when Miles handed it no DirectSound object to give Bink.
	// This player owns its own output device, so there is nothing to turn off.
}

int __stdcall BinkSetSoundSystem(BINKSNDSYSOPEN, BINKSNDPARAM)
{
	soundEnabled = true;
	return 1;
}

int __stdcall BinkOpenDirectSound(BINKSNDPARAM)
{
	return 1;
}

} // extern "C"
