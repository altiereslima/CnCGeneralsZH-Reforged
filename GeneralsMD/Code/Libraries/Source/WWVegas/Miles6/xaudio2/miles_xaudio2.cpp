/*
 * Miles Sound System 6.5 surface, implemented on XAudio2 and FFmpeg.
 *
 * The retail mss32.dll is a 32-bit binary and cannot load into the x64 process, so the x64 build
 * links this instead of the import stub.  Every entry point MilesAudioManager.cpp reaches is here
 * with the semantics that file relies on; the rest of Miles is not.
 *
 * What maps onto what:
 *   HSAMPLE / H3DSAMPLE  -> one IXAudio2SourceVoice each, fed a whole PCM WAV image in one buffer.
 *   HSTREAM / HAUDIO     -> a file decoded by FFmpeg through the game's own file callbacks, fed to
 *                           a source voice in ~200ms buffers by the service thread below.
 *   3D positioning       -> distance attenuation and a stereo pan computed here, not by XAudio2's
 *                           X3DAudio: the game only ever asks for a position, a min/max distance
 *                           pair and an occlusion scalar.
 *   EOS callbacks        -> the service thread notices a voice run dry and calls back from there,
 *                           which is the thread context Miles used (MilesAudioManager locks for it).
 */

#include <windows.h>
#include <mmreg.h>
#include <xaudio2.h>
#include <xapo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "MSS/MSS.h"

namespace {

const int SERVICE_PERIOD_MS = 10;
const int STREAM_CHUNK_MS = 200;
const int STREAM_BUFFERS_AHEAD = 4;
const int USER_DATA_SLOTS = 8;
const float DEFAULT_PAN = 0.5f;
const int AVIO_BUFFER_BYTES = 32768;

struct WaveImage
{
	WAVEFORMATEX format;
	const unsigned char *data;
	unsigned int dataBytes;
	unsigned int samplesPerBlock;
};

// ---------------------------------------------------------------------------------------------
// RIFF/WAVE
// ---------------------------------------------------------------------------------------------

unsigned int readU32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

unsigned short readU16(const unsigned char *p)
{
	return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}

bool parseWave(const void *image, WaveImage *out)
{
	const unsigned char *bytes = (const unsigned char *)image;
	if (bytes == NULL) {
		return false;
	}
	if (memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0) {
		return false;
	}

	const unsigned int riffBytes = readU32(bytes + 4);
	const unsigned char *cursor = bytes + 12;
	const unsigned char *end = bytes + 8 + riffBytes;
	bool haveFormat = false;

	memset(out, 0, sizeof(*out));
	while (cursor + 8 <= end) {
		const unsigned int chunkBytes = readU32(cursor + 4);
		const unsigned char *body = cursor + 8;

		if (memcmp(cursor, "fmt ", 4) == 0 && chunkBytes >= 16) {
			out->format.wFormatTag = readU16(body);
			out->format.nChannels = readU16(body + 2);
			out->format.nSamplesPerSec = readU32(body + 4);
			out->format.nAvgBytesPerSec = readU32(body + 8);
			out->format.nBlockAlign = readU16(body + 12);
			out->format.wBitsPerSample = readU16(body + 14);
			if (chunkBytes >= 20) {
				out->samplesPerBlock = readU16(body + 18);
			}
			haveFormat = true;
		} else if (memcmp(cursor, "data", 4) == 0) {
			out->data = body;
			out->dataBytes = chunkBytes;
		}

		cursor = body + chunkBytes + (chunkBytes & 1);
	}

	return haveFormat && out->data != NULL;
}

unsigned int wavePcmSampleCount(const WaveImage &wave)
{
	if (wave.format.wFormatTag == WAVE_FORMAT_IMA_ADPCM) {
		if (wave.format.nBlockAlign == 0 || wave.format.nChannels == 0) {
			return 0;
		}
		const unsigned int blocks = wave.dataBytes / wave.format.nBlockAlign;
		const unsigned int perBlock = wave.samplesPerBlock != 0
			? wave.samplesPerBlock
			: 1 + (wave.format.nBlockAlign - 4 * wave.format.nChannels) * 2 / wave.format.nChannels;
		return blocks * perBlock;
	}

	const unsigned int frameBytes = wave.format.nBlockAlign != 0 ? wave.format.nBlockAlign : 1;
	return wave.dataBytes / frameBytes;
}

// ---------------------------------------------------------------------------------------------
// IMA ADPCM
// ---------------------------------------------------------------------------------------------

const int ADPCM_INDEX_STEP[16] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

const int ADPCM_STEP_TABLE[89] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
	279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
	1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
	4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
	16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

short decodeAdpcmNibble(int nibble, int *predictor, int *index)
{
	const int step = ADPCM_STEP_TABLE[*index];
	int difference = step >> 3;

	if (nibble & 4) difference += step;
	if (nibble & 2) difference += step >> 1;
	if (nibble & 1) difference += step >> 2;
	if (nibble & 8) difference = -difference;

	int sample = *predictor + difference;
	if (sample > 32767) sample = 32767;
	if (sample < -32768) sample = -32768;
	*predictor = sample;

	*index += ADPCM_INDEX_STEP[nibble];
	if (*index < 0) *index = 0;
	if (*index > 88) *index = 88;

	return (short)sample;
}

// Decodes every block into interleaved 16-bit PCM.  Returns the sample frame count.
unsigned int decodeAdpcm(const WaveImage &wave, std::vector<short> *out)
{
	const unsigned int channels = wave.format.nChannels;
	const unsigned int blockAlign = wave.format.nBlockAlign;
	if (channels == 0 || channels > 2 || blockAlign <= 4 * channels) {
		return 0;
	}

	const unsigned int blocks = wave.dataBytes / blockAlign;
	const unsigned int samplesPerBlock = 1 + (blockAlign - 4 * channels) * 2 / channels;
	out->clear();
	out->reserve((size_t)blocks * samplesPerBlock * channels);

	for (unsigned int block = 0; block < blocks; ++block) {
		const unsigned char *body = wave.data + (size_t)block * blockAlign;
		int predictor[2] = { 0, 0 };
		int index[2] = { 0, 0 };

		for (unsigned int channel = 0; channel < channels; ++channel) {
			predictor[channel] = (short)readU16(body + channel * 4);
			index[channel] = body[channel * 4 + 2];
			if (index[channel] > 88) index[channel] = 88;
			out->push_back((short)predictor[channel]);
		}

		const unsigned char *nibbles = body + 4 * channels;
		const unsigned int groupsPerChannel = (blockAlign - 4 * channels) / (4 * channels);
		std::vector<short> decoded[2];
		for (unsigned int channel = 0; channel < channels; ++channel) {
			decoded[channel].reserve(groupsPerChannel * 8);
		}

		for (unsigned int group = 0; group < groupsPerChannel; ++group) {
			for (unsigned int channel = 0; channel < channels; ++channel) {
				const unsigned char *word = nibbles + (group * channels + channel) * 4;
				for (int byteIndex = 0; byteIndex < 4; ++byteIndex) {
					decoded[channel].push_back(decodeAdpcmNibble(word[byteIndex] & 0x0f, &predictor[channel], &index[channel]));
					decoded[channel].push_back(decodeAdpcmNibble((word[byteIndex] >> 4) & 0x0f, &predictor[channel], &index[channel]));
				}
			}
		}

		const size_t frames = decoded[0].size();
		for (size_t frame = 0; frame < frames; ++frame) {
			for (unsigned int channel = 0; channel < channels; ++channel) {
				out->push_back(decoded[channel][frame]);
			}
		}
	}

	return channels != 0 ? (unsigned int)(out->size() / channels) : 0;
}

// ---------------------------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------------------------

struct Sample
{
	bool is3D;
	IXAudio2SourceVoice *voice;
	WAVEFORMATEX format;
	const unsigned char *data;
	unsigned int dataBytes;
	float volume;
	float pan;
	unsigned int baseRate;
	unsigned int rate;
	S32 userData[USER_DATA_SLOTS];
	AILSAMPLECB endOfSample;
	AIL3DSAMPLECB endOf3DSample;
	bool playing;
	float positionX, positionY, positionZ;
	float minDistance, maxDistance;
	float occlusion;
};

struct StreamBuffer
{
	std::vector<unsigned char> bytes;
};

struct Stream
{
	Stream()
		: format(NULL), avio(NULL), decoder(NULL), resampler(NULL), audioStream(-1), fileHandle(0),
		  ownsFileHandle(false), voice(NULL), loopCount(1), paused(false), playing(false),
		  exhausted(false), volume(1.0f), pan(DEFAULT_PAN), totalMs(0.0), framesSubmitted(0),
		  callback(NULL)
	{
		memset(&voiceFormat, 0, sizeof(voiceFormat));
	}

	AVFormatContext *format;
	AVIOContext *avio;
	AVCodecContext *decoder;
	SwrContext *resampler;
	int audioStream;
	AILFILEHANDLE fileHandle;
	bool ownsFileHandle;

	IXAudio2SourceVoice *voice;
	WAVEFORMATEX voiceFormat;
	std::deque<StreamBuffer> queued;

	int loopCount;
	bool paused;
	bool playing;
	bool exhausted;
	float volume;
	float pan;
	double totalMs;
	long long framesSubmitted;
	AILSTREAMCB callback;
};

struct Listener
{
	float positionX, positionY, positionZ;
	float faceX, faceY, faceZ;
	float upX, upY, upZ;
};

struct Engine
{
	IXAudio2 *xaudio;
	IXAudio2MasteringVoice *master;
	CRITICAL_SECTION lock;
	HANDLE serviceThread;
	volatile LONG serviceRunning;
	std::vector<Sample *> samples;
	std::vector<Stream *> streams;
	Listener listener;
	bool started;
};

Engine g_engine;

AILFILEOPENCB g_fileOpen;
AILFILECLOSECB g_fileClose;
AILFILESEEKCB g_fileSeek;
AILFILEREADCB g_fileRead;

struct _AIL_DIGDRIVER *const DIGITAL_DRIVER = (struct _AIL_DIGDRIVER *)(UINT_PTR)0x1;
struct _AIL_3DPOBJECT *const LISTENER_OBJECT = (struct _AIL_3DPOBJECT *)(UINT_PTR)0x2;

const HPROVIDER PROVIDER_FAST_2D = 1;
const HPROVIDER PROVIDER_DOLBY = 2;

float clampUnit(float value)
{
	if (value < 0.0f) return 0.0f;
	if (value > 1.0f) return 1.0f;
	return value;
}

void applyPan(IXAudio2SourceVoice *voice, unsigned int sourceChannels, float pan)
{
	if (voice == NULL || g_engine.master == NULL) {
		return;
	}

	XAUDIO2_VOICE_DETAILS masterDetails;
	g_engine.master->GetVoiceDetails(&masterDetails);
	if (masterDetails.InputChannels < 2) {
		return;
	}

	const float left = clampUnit(1.0f - pan);
	const float right = clampUnit(pan);
	// Miles' pan is a constant-power law: dead centre is not two half-volume speakers.
	const float leftGain = (float)sqrt(left);
	const float rightGain = (float)sqrt(right);

	std::vector<float> matrix((size_t)sourceChannels * masterDetails.InputChannels, 0.0f);
	for (unsigned int channel = 0; channel < sourceChannels; ++channel) {
		matrix[(size_t)0 * sourceChannels + channel] = leftGain;
		matrix[(size_t)1 * sourceChannels + channel] = rightGain;
	}
	voice->SetOutputMatrix(NULL, sourceChannels, masterDetails.InputChannels, &matrix[0]);
}

void computeSpatialGain(const Sample &sample, float *volumeOut, float *panOut)
{
	const Listener &listener = g_engine.listener;
	const float dx = sample.positionX - listener.positionX;
	const float dy = sample.positionY - listener.positionY;
	const float dz = sample.positionZ - listener.positionZ;
	const float distance = (float)sqrt(dx * dx + dy * dy + dz * dz);

	float attenuation = 1.0f;
	if (sample.maxDistance > sample.minDistance) {
		if (distance >= sample.maxDistance) {
			attenuation = 0.0f;
		} else if (distance > sample.minDistance) {
			attenuation = (sample.maxDistance - distance) / (sample.maxDistance - sample.minDistance);
		}
	}
	attenuation *= clampUnit(1.0f - sample.occlusion);

	float pan = DEFAULT_PAN;
	if (distance > 0.0001f) {
		// right = up x face, the axis a stereo pan moves along. face x up is left in either
		// handedness, and taking it sent every sound on the right of the screen to the left ear.
		const float rightX = listener.upY * listener.faceZ - listener.upZ * listener.faceY;
		const float rightY = listener.upZ * listener.faceX - listener.upX * listener.faceZ;
		const float rightZ = listener.upX * listener.faceY - listener.upY * listener.faceX;
		const float rightLength = (float)sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
		if (rightLength > 0.0001f) {
			const float projection = (dx * rightX + dy * rightY + dz * rightZ) / (rightLength * distance);
			pan = clampUnit(0.5f + 0.5f * projection);
		}
	}

	*volumeOut = clampUnit(sample.volume) * attenuation;
	*panOut = pan;
}

void refreshSampleOutput(Sample *sample)
{
	if (sample->voice == NULL) {
		return;
	}

	float volume = sample->volume;
	float pan = sample->pan;
	if (sample->is3D) {
		computeSpatialGain(*sample, &volume, &pan);
	}

	sample->voice->SetVolume(clampUnit(volume));
	applyPan(sample->voice, sample->format.nChannels, pan);
	if (sample->baseRate != 0) {
		float ratio = (float)sample->rate / (float)sample->baseRate;
		if (ratio < XAUDIO2_MIN_FREQ_RATIO) ratio = XAUDIO2_MIN_FREQ_RATIO;
		if (ratio > 4.0f) ratio = 4.0f;
		sample->voice->SetFrequencyRatio(ratio);
	}
}

void destroyVoice(IXAudio2SourceVoice **voice)
{
	if (*voice != NULL) {
		(*voice)->Stop(0);
		(*voice)->FlushSourceBuffers();
		(*voice)->DestroyVoice();
		*voice = NULL;
	}
}

bool ensureVoice(Sample *sample, const WAVEFORMATEX &format)
{
	if (g_engine.xaudio == NULL) {
		return false;
	}

	if (sample->voice != NULL &&
		sample->format.nChannels == format.nChannels &&
		sample->format.nSamplesPerSec == format.nSamplesPerSec &&
		sample->format.wBitsPerSample == format.wBitsPerSample) {
		sample->voice->Stop(0);
		sample->voice->FlushSourceBuffers();
		return true;
	}

	destroyVoice(&sample->voice);
	sample->format = format;
	return SUCCEEDED(g_engine.xaudio->CreateSourceVoice(&sample->voice, &format, 0, 4.0f));
}

// ---------------------------------------------------------------------------------------------
// Streams, decoded by FFmpeg through the game's file callbacks
// ---------------------------------------------------------------------------------------------

int streamRead(void *opaque, uint8_t *buffer, int bytes)
{
	Stream *stream = (Stream *)opaque;
	if (g_fileRead == NULL) {
		return AVERROR_EOF;
	}
	const U32 read = g_fileRead(stream->fileHandle, buffer, (U32)bytes);
	if (read == 0) {
		return AVERROR_EOF;
	}
	return (int)read;
}

int64_t streamSeek(void *opaque, int64_t offset, int whence)
{
	Stream *stream = (Stream *)opaque;
	if (g_fileSeek == NULL) {
		return -1;
	}

	if (whence == AVSEEK_SIZE) {
		const S32 here = g_fileSeek(stream->fileHandle, 0, SEEK_CUR);
		const S32 size = g_fileSeek(stream->fileHandle, 0, SEEK_END);
		g_fileSeek(stream->fileHandle, here, SEEK_SET);
		return size;
	}

	return g_fileSeek(stream->fileHandle, (S32)offset, (U32)whence);
}

void closeStreamDecoder(Stream *stream)
{
	if (stream->resampler != NULL) {
		swr_free(&stream->resampler);
	}
	if (stream->decoder != NULL) {
		avcodec_free_context(&stream->decoder);
	}
	if (stream->format != NULL) {
		avformat_close_input(&stream->format);
	}
	if (stream->avio != NULL) {
		av_freep(&stream->avio->buffer);
		avio_context_free(&stream->avio);
	}
	if (stream->ownsFileHandle && g_fileClose != NULL && stream->fileHandle != 0) {
		g_fileClose(stream->fileHandle);
	}
	stream->fileHandle = 0;
}

bool openStreamDecoder(Stream *stream, const char *filename)
{
	if (g_fileOpen == NULL || g_fileOpen(filename, &stream->fileHandle) == 0 || stream->fileHandle == 0) {
		return false;
	}
	stream->ownsFileHandle = true;

	unsigned char *avioBuffer = (unsigned char *)av_malloc(AVIO_BUFFER_BYTES);
	if (avioBuffer == NULL) {
		return false;
	}
	stream->avio = avio_alloc_context(avioBuffer, AVIO_BUFFER_BYTES, 0, stream, streamRead, NULL, streamSeek);
	if (stream->avio == NULL) {
		av_free(avioBuffer);
		return false;
	}

	stream->format = avformat_alloc_context();
	if (stream->format == NULL) {
		return false;
	}
	stream->format->pb = stream->avio;
	stream->format->flags |= AVFMT_FLAG_CUSTOM_IO;

	if (avformat_open_input(&stream->format, filename, NULL, NULL) < 0) {
		return false;
	}
	if (avformat_find_stream_info(stream->format, NULL) < 0) {
		return false;
	}

	stream->audioStream = av_find_best_stream(stream->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (stream->audioStream < 0) {
		return false;
	}

	AVStream *audio = stream->format->streams[stream->audioStream];
	const AVCodec *codec = avcodec_find_decoder(audio->codecpar->codec_id);
	if (codec == NULL) {
		return false;
	}
	stream->decoder = avcodec_alloc_context3(codec);
	if (stream->decoder == NULL ||
		avcodec_parameters_to_context(stream->decoder, audio->codecpar) < 0 ||
		avcodec_open2(stream->decoder, codec, NULL) < 0) {
		return false;
	}

	const int channels = stream->decoder->ch_layout.nb_channels >= 2 ? 2 : 1;
	AVChannelLayout outLayout;
	av_channel_layout_default(&outLayout, channels);
	if (swr_alloc_set_opts2(&stream->resampler, &outLayout, AV_SAMPLE_FMT_S16, stream->decoder->sample_rate,
			&stream->decoder->ch_layout, stream->decoder->sample_fmt, stream->decoder->sample_rate,
			0, NULL) < 0 || swr_init(stream->resampler) < 0) {
		av_channel_layout_uninit(&outLayout);
		return false;
	}
	av_channel_layout_uninit(&outLayout);

	memset(&stream->voiceFormat, 0, sizeof(stream->voiceFormat));
	stream->voiceFormat.wFormatTag = WAVE_FORMAT_PCM;
	stream->voiceFormat.nChannels = (WORD)channels;
	stream->voiceFormat.nSamplesPerSec = stream->decoder->sample_rate;
	stream->voiceFormat.wBitsPerSample = 16;
	stream->voiceFormat.nBlockAlign = (WORD)(channels * 2);
	stream->voiceFormat.nAvgBytesPerSec = stream->voiceFormat.nSamplesPerSec * stream->voiceFormat.nBlockAlign;

	if (stream->format->duration > 0) {
		stream->totalMs = (double)stream->format->duration * 1000.0 / (double)AV_TIME_BASE;
	} else if (audio->duration > 0) {
		stream->totalMs = (double)audio->duration * av_q2d(audio->time_base) * 1000.0;
	}

	if (g_engine.xaudio == NULL ||
		FAILED(g_engine.xaudio->CreateSourceVoice(&stream->voice, &stream->voiceFormat, 0, 2.0f))) {
		return false;
	}
	return true;
}

// Decodes up to one chunk.  Returns false at the end of the file with nothing more to give.
bool decodeStreamChunk(Stream *stream, std::vector<unsigned char> *out)
{
	const size_t wanted = (size_t)stream->voiceFormat.nAvgBytesPerSec * STREAM_CHUNK_MS / 1000;
	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	if (packet == NULL || frame == NULL) {
		if (packet != NULL) av_packet_free(&packet);
		if (frame != NULL) av_frame_free(&frame);
		return false;
	}

	bool reachedEnd = false;
	while (out->size() < wanted && !reachedEnd) {
		const int readResult = av_read_frame(stream->format, packet);
		if (readResult < 0) {
			avcodec_send_packet(stream->decoder, NULL);
			reachedEnd = true;
		} else if (packet->stream_index != stream->audioStream) {
			av_packet_unref(packet);
			continue;
		} else if (avcodec_send_packet(stream->decoder, packet) < 0) {
			av_packet_unref(packet);
			continue;
		}
		av_packet_unref(packet);

		for (;;) {
			const int receiveResult = avcodec_receive_frame(stream->decoder, frame);
			if (receiveResult < 0) {
				break;
			}

			const int maxOutSamples = (int)swr_get_out_samples(stream->resampler, frame->nb_samples);
			const size_t offset = out->size();
			out->resize(offset + (size_t)maxOutSamples * stream->voiceFormat.nBlockAlign);
			uint8_t *destination = &(*out)[offset];
			const int converted = swr_convert(stream->resampler, &destination, maxOutSamples,
				(const uint8_t **)frame->extended_data, frame->nb_samples);
			out->resize(offset + (converted > 0 ? (size_t)converted * stream->voiceFormat.nBlockAlign : 0));
			av_frame_unref(frame);
		}
	}

	av_packet_free(&packet);
	av_frame_free(&frame);

	if (reachedEnd) {
		avcodec_flush_buffers(stream->decoder);
		return !out->empty() ? true : false;
	}
	return true;
}

bool rewindStream(Stream *stream)
{
	if (av_seek_frame(stream->format, stream->audioStream, 0, AVSEEK_FLAG_BACKWARD) < 0) {
		return false;
	}
	avcodec_flush_buffers(stream->decoder);
	return true;
}

void serviceStream(Stream *stream, std::vector<AILSTREAMCB> *callbacks, std::vector<HSTREAM> *callbackHandles)
{
	if (stream->voice == NULL || !stream->playing) {
		return;
	}

	XAUDIO2_VOICE_STATE state;
	stream->voice->GetState(&state);
	while (stream->queued.size() > state.BuffersQueued) {
		stream->queued.pop_front();
	}

	// queued holds one entry per buffer the voice still has, so it is the count to top up against.
	while (!stream->exhausted && stream->queued.size() < STREAM_BUFFERS_AHEAD) {
		StreamBuffer chunk;
		if (!decodeStreamChunk(stream, &chunk.bytes) || chunk.bytes.empty()) {
			if (stream->loopCount > 1 && rewindStream(stream)) {
				stream->loopCount -= 1;
				continue;
			}
			stream->exhausted = true;
			break;
		}

		stream->queued.push_back(chunk);
		const StreamBuffer &stored = stream->queued.back();
		XAUDIO2_BUFFER buffer;
		memset(&buffer, 0, sizeof(buffer));
		buffer.AudioBytes = (UINT32)stored.bytes.size();
		buffer.pAudioData = &stored.bytes[0];
		stream->voice->SubmitSourceBuffer(&buffer);
		stream->framesSubmitted += (long long)(stored.bytes.size() / stream->voiceFormat.nBlockAlign);
		stream->voice->GetState(&state);
	}

	if (stream->exhausted && state.BuffersQueued == 0) {
		stream->playing = false;
		if (stream->callback != NULL) {
			callbacks->push_back(stream->callback);
			callbackHandles->push_back((HSTREAM)stream);
		}
	}
}

DWORD WINAPI serviceThreadMain(LPVOID)
{
	while (InterlockedCompareExchange(&g_engine.serviceRunning, 1, 1) == 1) {
		std::vector<AILSAMPLECB> sampleCallbacks;
		std::vector<HSAMPLE> sampleHandles;
		std::vector<AIL3DSAMPLECB> sample3DCallbacks;
		std::vector<H3DSAMPLE> sample3DHandles;
		std::vector<AILSTREAMCB> streamCallbacks;
		std::vector<HSTREAM> streamHandles;

		EnterCriticalSection(&g_engine.lock);
		for (size_t i = 0; i < g_engine.samples.size(); ++i) {
			Sample *sample = g_engine.samples[i];
			if (sample->voice == NULL) {
				continue;
			}
			if (sample->is3D) {
				refreshSampleOutput(sample);
			}
			if (!sample->playing) {
				continue;
			}

			XAUDIO2_VOICE_STATE state;
			sample->voice->GetState(&state);
			if (state.BuffersQueued == 0) {
				sample->playing = false;
				if (sample->is3D && sample->endOf3DSample != NULL) {
					sample3DCallbacks.push_back(sample->endOf3DSample);
					sample3DHandles.push_back((H3DSAMPLE)sample);
				} else if (!sample->is3D && sample->endOfSample != NULL) {
					sampleCallbacks.push_back(sample->endOfSample);
					sampleHandles.push_back((HSAMPLE)sample);
				}
			}
		}

		for (size_t i = 0; i < g_engine.streams.size(); ++i) {
			serviceStream(g_engine.streams[i], &streamCallbacks, &streamHandles);
		}
		LeaveCriticalSection(&g_engine.lock);

		// Callbacks run outside the lock: the game takes its own locks in them.
		for (size_t i = 0; i < sampleCallbacks.size(); ++i) {
			sampleCallbacks[i](sampleHandles[i]);
		}
		for (size_t i = 0; i < sample3DCallbacks.size(); ++i) {
			sample3DCallbacks[i](sample3DHandles[i]);
		}
		for (size_t i = 0; i < streamCallbacks.size(); ++i) {
			streamCallbacks[i](streamHandles[i]);
		}

		Sleep(SERVICE_PERIOD_MS);
	}
	return 0;
}

// ---------------------------------------------------------------------------------------------
// Capture
//
// Everything the game plays ends up in the one mastering voice, so an effect sitting on that voice
// is the whole soundtrack and nothing else on the machine.  The effect passes the mix through
// untouched and writes a copy to a WAV file.
//
// The file is the tap's own: XAudio2 holds the only other reference, so the destructor runs after
// the last Process call and there is no window where one thread is writing a file the other has
// closed.  Nothing is buffered off to another thread either - a write that blocks makes the live
// output stutter, and nobody is listening to a capture run.
// ---------------------------------------------------------------------------------------------

const int WAVE_HEADER_BYTES = 44;
const int WAVE_RIFF_SIZE_OFFSET = 4;
const int WAVE_DATA_SIZE_OFFSET = 40;
const int WAVE_FMT_CHUNK_BYTES = 16;
const int WAVE_PCM_TAG = 1;
const int CAPTURE_BITS_PER_SAMPLE = 16;
const float CAPTURE_SAMPLE_SCALE = 32767.0f;
const int CAPTURE_CONVERSION_SAMPLES = 4096;

void writeLittleU32(FILE *file, unsigned int value)
{
	fwrite(&value, sizeof(value), 1, file);
}

void writeLittleU16(FILE *file, unsigned short value)
{
	fwrite(&value, sizeof(value), 1, file);
}

void writeWaveHeader(FILE *file, unsigned int channels, unsigned int samplesPerSecond,
	unsigned int dataBytes)
{
	const unsigned int blockAlign = channels * (CAPTURE_BITS_PER_SAMPLE / 8);
	fwrite("RIFF", 1, 4, file);
	writeLittleU32(file, WAVE_HEADER_BYTES - 8 + dataBytes);
	fwrite("WAVEfmt ", 1, 8, file);
	writeLittleU32(file, WAVE_FMT_CHUNK_BYTES);
	writeLittleU16(file, WAVE_PCM_TAG);
	writeLittleU16(file, (unsigned short)channels);
	writeLittleU32(file, samplesPerSecond);
	writeLittleU32(file, samplesPerSecond * blockAlign);
	writeLittleU16(file, (unsigned short)blockAlign);
	writeLittleU16(file, CAPTURE_BITS_PER_SAMPLE);
	fwrite("data", 1, 4, file);
	writeLittleU32(file, dataBytes);
}

class CaptureTap : public IXAPO
{
public:
	CaptureTap(FILE *file, unsigned int channels, unsigned int samplesPerSecond)
		: m_references(1), m_file(file), m_channels(channels),
		  m_samplesPerSecond(samplesPerSecond), m_dataBytes(0), m_sizesWrittenAt(0)
	{
		writeWaveHeader(m_file, m_channels, m_samplesPerSecond, 0);
	}

	STDMETHOD(QueryInterface)(REFIID riid, void **object)
	{
		if (object == NULL) {
			return E_POINTER;
		}
		if (riid == __uuidof(IXAPO) || riid == __uuidof(IUnknown)) {
			*object = static_cast<IXAPO *>(this);
			AddRef();
			return S_OK;
		}
		*object = NULL;
		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)()
	{
		return (ULONG)InterlockedIncrement(&m_references);
	}

	STDMETHOD_(ULONG, Release)()
	{
		const LONG left = InterlockedDecrement(&m_references);
		if (left == 0) {
			delete this;
		}
		return (ULONG)left;
	}

	STDMETHOD(GetRegistrationProperties)(XAPO_REGISTRATION_PROPERTIES **properties)
	{
		XAPO_REGISTRATION_PROPERTIES *copy = (XAPO_REGISTRATION_PROPERTIES *)
			CoTaskMemAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES));
		if (copy == NULL) {
			return E_OUTOFMEMORY;
		}
		memset(copy, 0, sizeof(*copy));
		wcscpy_s(copy->FriendlyName, L"capture");
		wcscpy_s(copy->CopyrightInfo, L"");
		copy->MajorVersion = 1;
		copy->Flags = XAPO_FLAG_CHANNELS_MUST_MATCH | XAPO_FLAG_FRAMERATE_MUST_MATCH
			| XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH
			| XAPO_FLAG_INPLACE_SUPPORTED | XAPO_FLAG_INPLACE_REQUIRED;
		copy->MinInputBufferCount = 1;
		copy->MaxInputBufferCount = 1;
		copy->MinOutputBufferCount = 1;
		copy->MaxOutputBufferCount = 1;
		*properties = copy;
		return S_OK;
	}

	STDMETHOD(IsInputFormatSupported)(const WAVEFORMATEX *, const WAVEFORMATEX *requested,
		WAVEFORMATEX **supported)
	{
		if (supported != NULL) {
			*supported = const_cast<WAVEFORMATEX *>(requested);
		}
		return S_OK;
	}

	STDMETHOD(IsOutputFormatSupported)(const WAVEFORMATEX *, const WAVEFORMATEX *requested,
		WAVEFORMATEX **supported)
	{
		if (supported != NULL) {
			*supported = const_cast<WAVEFORMATEX *>(requested);
		}
		return S_OK;
	}

	STDMETHOD(Initialize)(const void *, UINT32)
	{
		return S_OK;
	}

	STDMETHOD_(void, Reset)()
	{
	}

	STDMETHOD(LockForProcess)(UINT32, const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *, UINT32,
		const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *)
	{
		return S_OK;
	}

	STDMETHOD_(void, UnlockForProcess)()
	{
	}

	STDMETHOD_(void, Process)(UINT32, const XAPO_PROCESS_BUFFER_PARAMETERS *input, UINT32,
		XAPO_PROCESS_BUFFER_PARAMETERS *output, BOOL)
	{
		if (output != NULL) {
			output[0].ValidFrameCount = input[0].ValidFrameCount;
			output[0].BufferFlags = input[0].BufferFlags;
		}

		const UINT32 samples = input[0].ValidFrameCount * m_channels;
		short converted[CAPTURE_CONVERSION_SAMPLES];

		if (input[0].BufferFlags == XAPO_BUFFER_SILENT) {
			memset(converted, 0, sizeof(converted));
			for (UINT32 written = 0; written < samples; ) {
				const UINT32 batch = min(samples - written, (UINT32)CAPTURE_CONVERSION_SAMPLES);
				writeSamples(converted, batch);
				written += batch;
			}
			return;
		}

		const float *mix = (const float *)input[0].pBuffer;
		for (UINT32 read = 0; read < samples; ) {
			const UINT32 batch = min(samples - read, (UINT32)CAPTURE_CONVERSION_SAMPLES);
			for (UINT32 index = 0; index < batch; ++index) {
				const float value = mix[read + index];
				const float clamped = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
				converted[index] = (short)(clamped * CAPTURE_SAMPLE_SCALE);
			}
			writeSamples(converted, batch);
			read += batch;
		}
	}

	STDMETHOD_(UINT32, CalcInputFrames)(UINT32 outputFrameCount)
	{
		return outputFrameCount;
	}

	STDMETHOD_(UINT32, CalcOutputFrames)(UINT32 inputFrameCount)
	{
		return inputFrameCount;
	}

private:
	~CaptureTap()
	{
		writeSizes();
		fclose(m_file);
	}

	void writeSamples(const short *samples, UINT32 count)
	{
		m_dataBytes += (unsigned int)fwrite(samples, sizeof(short), count, m_file) * sizeof(short);

		// A recording run is usually killed from the script that started it rather than left to shut
		// down, and a file whose header still says nothing was written is one no editor will open.
		// Writing the two sizes back once a second costs two seeks and makes a killed run usable.
		const unsigned int refreshEvery = m_samplesPerSecond * m_channels * sizeof(short);
		if (m_dataBytes - m_sizesWrittenAt < refreshEvery) {
			return;
		}
		m_sizesWrittenAt = m_dataBytes;
		writeSizes();
		fseek(m_file, 0, SEEK_END);
	}

	void writeSizes()
	{
		fseek(m_file, WAVE_RIFF_SIZE_OFFSET, SEEK_SET);
		writeLittleU32(m_file, WAVE_HEADER_BYTES - 8 + m_dataBytes);
		fseek(m_file, WAVE_DATA_SIZE_OFFSET, SEEK_SET);
		writeLittleU32(m_file, m_dataBytes);
	}

	volatile LONG m_references;
	FILE *m_file;
	unsigned int m_channels;
	unsigned int m_samplesPerSecond;
	unsigned int m_dataBytes;
	unsigned int m_sizesWrittenAt;
};

} // namespace

// =================================================================================================
// Startup and global state
// =================================================================================================

extern "C" {

S32 AILCALL AIL_startup(void)
{
	if (g_engine.started) {
		return 1;
	}

	memset(&g_engine.listener, 0, sizeof(g_engine.listener));
	g_engine.listener.faceY = 1.0f;
	g_engine.listener.upZ = 1.0f;
	InitializeCriticalSection(&g_engine.lock);
	g_engine.started = true;
	return 1;
}

S32 AILCALL AIL_quick_startup(S32 use_digital, S32, U32, S32, S32)
{
	if (!use_digital) {
		return 0;
	}
	if (g_engine.xaudio != NULL) {
		return 1;
	}

	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(XAudio2Create(&g_engine.xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
		return 0;
	}
	if (FAILED(g_engine.xaudio->CreateMasteringVoice(&g_engine.master))) {
		g_engine.xaudio->Release();
		g_engine.xaudio = NULL;
		return 0;
	}

	InterlockedExchange(&g_engine.serviceRunning, 1);
	g_engine.serviceThread = CreateThread(NULL, 0, serviceThreadMain, NULL, 0, NULL);
	return 1;
}

void AILCALL AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls)
{
	if (pdig != NULL) *pdig = DIGITAL_DRIVER;
	if (pmdi != NULL) *pmdi = NULL;
	if (pdls != NULL) *pdls = NULL;
}

S32 AILCALL AIL_ex_start_capture(const char *pathname)
{
	if (g_engine.master == NULL || pathname == NULL) {
		return 0;
	}

	XAUDIO2_VOICE_DETAILS details;
	g_engine.master->GetVoiceDetails(&details);

	FILE *file = NULL;
	if (fopen_s(&file, pathname, "wb") != 0 || file == NULL) {
		return 0;
	}

	CaptureTap *tap = new CaptureTap(file, details.InputChannels, details.InputSampleRate);
	XAUDIO2_EFFECT_DESCRIPTOR descriptor;
	descriptor.pEffect = static_cast<IXAPO *>(tap);
	descriptor.InitialState = TRUE;
	descriptor.OutputChannels = details.InputChannels;
	XAUDIO2_EFFECT_CHAIN chain;
	chain.EffectCount = 1;
	chain.pEffectDescriptors = &descriptor;

	const HRESULT result = g_engine.master->SetEffectChain(&chain);

	// the chain holds the reference that matters from here; ours goes, and on a refusal that is
	// the last one, so the tap closes its own file on the way out
	tap->Release();
	return SUCCEEDED(result) ? 1 : 0;
}

void AILCALL AIL_ex_stop_capture(void)
{
	if (g_engine.master != NULL) {
		g_engine.master->SetEffectChain(NULL);
	}
}

void AILCALL AIL_shutdown(void)
{
	AIL_ex_stop_capture();

	if (g_engine.serviceThread != NULL) {
		InterlockedExchange(&g_engine.serviceRunning, 0);
		WaitForSingleObject(g_engine.serviceThread, 2000);
		CloseHandle(g_engine.serviceThread);
		g_engine.serviceThread = NULL;
	}

	if (g_engine.master != NULL) {
		g_engine.master->DestroyVoice();
		g_engine.master = NULL;
	}
	if (g_engine.xaudio != NULL) {
		g_engine.xaudio->Release();
		g_engine.xaudio = NULL;
	}
	if (g_engine.started) {
		DeleteCriticalSection(&g_engine.lock);
		g_engine.started = false;
	}
}

char *AILCALL AIL_set_redist_directory(const char *)
{
	return NULL;
}

void AILCALL AIL_lock(void)
{
	if (g_engine.started) {
		EnterCriticalSection(&g_engine.lock);
	}
}

void AILCALL AIL_unlock(void)
{
	if (g_engine.started) {
		LeaveCriticalSection(&g_engine.lock);
	}
}

S32 AILCALL AIL_get_timer_highest_delay(void)
{
	return SERVICE_PERIOD_MS;
}

void AILCALL AIL_set_file_callbacks(AILFILEOPENCB opencb, AILFILECLOSECB closecb, AILFILESEEKCB seekcb, AILFILEREADCB readcb)
{
	g_fileOpen = opencb;
	g_fileClose = closecb;
	g_fileSeek = seekcb;
	g_fileRead = readcb;
}

// =================================================================================================
// Providers and filters
// =================================================================================================

S32 AILCALL AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name)
{
	// MilesAudioManager looks its provider up by name ("Miles Fast 2D Positional Audio"), and
	// indexes its array with the result without checking it, so those names have to be here.
	static char fast2D[] = "Miles Fast 2D Positional Audio";
	static char dolby[] = "Dolby Surround";

	switch (*next) {
		case 0:
			*dest = PROVIDER_FAST_2D;
			*name = fast2D;
			*next = 1;
			return 1;
		case 1:
			*dest = PROVIDER_DOLBY;
			*name = dolby;
			*next = 2;
			return 1;
		default:
			return 0;
	}
}

S32 AILCALL AIL_open_3D_provider(HPROVIDER lib)
{
	return (lib == PROVIDER_FAST_2D || lib == PROVIDER_DOLBY) ? 0 : 1;
}

void AILCALL AIL_close_3D_provider(HPROVIDER)
{
}

S32 AILCALL AIL_enumerate_filters(HPROENUM *, HPROVIDER *, char **)
{
	// No pipeline filters: the only one the game looks for is the Mono Delay, and a missing one
	// leaves m_delayFilter NULL, which it already handles.
	return 0;
}

void AILCALL AIL_set_3D_speaker_type(HPROVIDER, S32)
{
}

HPROVIDER AILCALL AIL_set_sample_processor(HSAMPLE, S32, HPROVIDER provider)
{
	return provider;
}

void AILCALL AIL_set_filter_sample_preference(HSAMPLE, const char *, const void *)
{
}

// =================================================================================================
// 2D samples
// =================================================================================================

HSAMPLE AILCALL AIL_allocate_sample_handle(HDIGDRIVER dig)
{
	if (dig == NULL || g_engine.xaudio == NULL) {
		return NULL;
	}

	Sample *sample = new Sample;
	memset(sample, 0, sizeof(*sample));
	sample->volume = 1.0f;
	sample->pan = DEFAULT_PAN;
	sample->maxDistance = 1.0f;

	EnterCriticalSection(&g_engine.lock);
	g_engine.samples.push_back(sample);
	LeaveCriticalSection(&g_engine.lock);
	return (HSAMPLE)sample;
}

void AILCALL AIL_release_sample_handle(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	for (size_t i = 0; i < g_engine.samples.size(); ++i) {
		if (g_engine.samples[i] == sample) {
			g_engine.samples.erase(g_engine.samples.begin() + i);
			break;
		}
	}
	destroyVoice(&sample->voice);
	LeaveCriticalSection(&g_engine.lock);
	delete sample;
}

void AILCALL AIL_init_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	if (sample->voice != NULL) {
		sample->voice->Stop(0);
		sample->voice->FlushSourceBuffers();
	}
	sample->playing = false;
	sample->volume = 1.0f;
	sample->pan = DEFAULT_PAN;
	sample->endOfSample = NULL;
	sample->data = NULL;
	sample->dataBytes = 0;
	LeaveCriticalSection(&g_engine.lock);
}

S32 AILCALL AIL_set_sample_file(HSAMPLE handle, const void *file_image, S32)
{
	Sample *sample = (Sample *)handle;
	WaveImage wave;
	if (sample == NULL || !parseWave(file_image, &wave) || wave.format.wFormatTag != WAVE_FORMAT_PCM) {
		return 0;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->playing = false;
	const bool ready = ensureVoice(sample, wave.format);
	if (ready) {
		sample->data = wave.data;
		sample->dataBytes = wave.dataBytes;
		sample->baseRate = wave.format.nSamplesPerSec;
		sample->rate = wave.format.nSamplesPerSec;
		refreshSampleOutput(sample);
	}
	LeaveCriticalSection(&g_engine.lock);
	return ready ? 1 : 0;
}

void AILCALL AIL_start_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || sample->voice == NULL || sample->data == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->voice->Stop(0);
	sample->voice->FlushSourceBuffers();

	XAUDIO2_BUFFER buffer;
	memset(&buffer, 0, sizeof(buffer));
	buffer.AudioBytes = sample->dataBytes;
	buffer.pAudioData = sample->data;
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	if (SUCCEEDED(sample->voice->SubmitSourceBuffer(&buffer))) {
		refreshSampleOutput(sample);
		sample->voice->Start(0);
		sample->playing = true;
	}
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_stop_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || sample->voice == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->voice->Stop(0);
	sample->playing = false;
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_resume_sample(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || sample->voice == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	XAUDIO2_VOICE_STATE state;
	sample->voice->GetState(&state);
	if (state.BuffersQueued > 0) {
		sample->voice->Start(0);
		sample->playing = true;
	}
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_end_sample(HSAMPLE handle)
{
	AIL_stop_sample(handle);
}

void AILCALL AIL_set_sample_volume_pan(HSAMPLE handle, F32 volume, F32 pan)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->volume = volume;
	sample->pan = pan;
	refreshSampleOutput(sample);
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_sample_volume_pan(HSAMPLE handle, F32 *volume, F32 *pan)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}
	if (volume != NULL) *volume = sample->volume;
	if (pan != NULL) *pan = sample->pan;
}

void AILCALL AIL_set_sample_playback_rate(HSAMPLE handle, S32 playback_rate)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL || playback_rate <= 0) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->rate = (unsigned int)playback_rate;
	refreshSampleOutput(sample);
	LeaveCriticalSection(&g_engine.lock);
}

S32 AILCALL AIL_sample_playback_rate(HSAMPLE handle)
{
	Sample *sample = (Sample *)handle;
	return sample != NULL ? (S32)sample->rate : 0;
}

void AILCALL AIL_set_sample_user_data(HSAMPLE handle, U32 index, S32 value)
{
	Sample *sample = (Sample *)handle;
	if (sample != NULL && index < USER_DATA_SLOTS) {
		sample->userData[index] = value;
	}
}

S32 AILCALL AIL_sample_user_data(HSAMPLE handle, U32 index)
{
	Sample *sample = (Sample *)handle;
	return (sample != NULL && index < USER_DATA_SLOTS) ? sample->userData[index] : 0;
}

AILSAMPLECB AILCALL AIL_register_EOS_callback(HSAMPLE handle, AILSAMPLECB EOS)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return NULL;
	}

	EnterCriticalSection(&g_engine.lock);
	AILSAMPLECB previous = sample->endOfSample;
	sample->endOfSample = EOS;
	LeaveCriticalSection(&g_engine.lock);
	return previous;
}

void AILCALL AIL_get_DirectSound_info(HSAMPLE, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB)
{
	// There is no DirectSound object behind XAudio2.  The two callers both handle NULL: the speaker
	// config falls back to stereo, and Bink plays its own audio.
	if (lplpDS != NULL) *lplpDS = NULL;
	if (lplpDSB != NULL) *lplpDSB = NULL;
}

// =================================================================================================
// 3D samples
// =================================================================================================

H3DSAMPLE AILCALL AIL_allocate_3D_sample_handle(HPROVIDER lib)
{
	if (lib != PROVIDER_FAST_2D && lib != PROVIDER_DOLBY) {
		return NULL;
	}

	HSAMPLE handle = AIL_allocate_sample_handle(DIGITAL_DRIVER);
	Sample *sample = (Sample *)handle;
	if (sample != NULL) {
		sample->is3D = true;
		sample->minDistance = 1.0f;
		sample->maxDistance = 1000.0f;
	}
	return (H3DSAMPLE)sample;
}

void AILCALL AIL_release_3D_sample_handle(H3DSAMPLE handle)
{
	AIL_release_sample_handle((HSAMPLE)handle);
}

S32 AILCALL AIL_set_3D_sample_file(H3DSAMPLE handle, const void *file_image)
{
	Sample *sample = (Sample *)handle;
	if (sample != NULL) {
		sample->occlusion = 0.0f;
	}
	return AIL_set_sample_file((HSAMPLE)handle, file_image, 0);
}

void AILCALL AIL_start_3D_sample(H3DSAMPLE handle)
{
	AIL_start_sample((HSAMPLE)handle);
}

void AILCALL AIL_stop_3D_sample(H3DSAMPLE handle)
{
	AIL_stop_sample((HSAMPLE)handle);
}

void AILCALL AIL_resume_3D_sample(H3DSAMPLE handle)
{
	AIL_resume_sample((HSAMPLE)handle);
}

void AILCALL AIL_end_3D_sample(H3DSAMPLE handle)
{
	AIL_stop_sample((HSAMPLE)handle);
}

void AILCALL AIL_set_3D_sample_volume(H3DSAMPLE handle, F32 volume)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->volume = volume;
	refreshSampleOutput(sample);
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_set_3D_sample_playback_rate(H3DSAMPLE handle, S32 playback_rate)
{
	AIL_set_sample_playback_rate((HSAMPLE)handle, playback_rate);
}

S32 AILCALL AIL_3D_sample_playback_rate(H3DSAMPLE handle)
{
	return AIL_sample_playback_rate((HSAMPLE)handle);
}

void AILCALL AIL_set_3D_sample_distances(H3DSAMPLE handle, F32 max_dist, F32 min_dist)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->maxDistance = max_dist;
	sample->minDistance = min_dist;
	refreshSampleOutput(sample);
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_set_3D_sample_occlusion(H3DSAMPLE handle, F32 occlusion)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	sample->occlusion = clampUnit(occlusion);
	refreshSampleOutput(sample);
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_set_3D_user_data(H3DPOBJECT obj, U32 index, S32 value)
{
	AIL_set_sample_user_data((HSAMPLE)obj, index, value);
}

S32 AILCALL AIL_3D_user_data(H3DPOBJECT obj, U32 index)
{
	return AIL_sample_user_data((HSAMPLE)obj, index);
}

AIL3DSAMPLECB AILCALL AIL_register_3D_EOS_callback(H3DSAMPLE handle, AIL3DSAMPLECB EOS)
{
	Sample *sample = (Sample *)handle;
	if (sample == NULL) {
		return NULL;
	}

	EnterCriticalSection(&g_engine.lock);
	AIL3DSAMPLECB previous = sample->endOf3DSample;
	sample->endOf3DSample = EOS;
	LeaveCriticalSection(&g_engine.lock);
	return previous;
}

// =================================================================================================
// 3D positioning
// =================================================================================================

H3DPOBJECT AILCALL AIL_open_3D_listener(HPROVIDER lib)
{
	return (lib == PROVIDER_FAST_2D || lib == PROVIDER_DOLBY) ? LISTENER_OBJECT : NULL;
}

void AILCALL AIL_close_3D_listener(H3DPOBJECT)
{
}

void AILCALL AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z)
{
	if (obj == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	if (obj == LISTENER_OBJECT) {
		g_engine.listener.positionX = X;
		g_engine.listener.positionY = Y;
		g_engine.listener.positionZ = Z;
	} else {
		Sample *sample = (Sample *)obj;
		sample->positionX = X;
		sample->positionY = Y;
		sample->positionZ = Z;
		refreshSampleOutput(sample);
	}
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_set_3D_orientation(H3DPOBJECT obj, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up, F32 Y_up, F32 Z_up)
{
	if (obj != LISTENER_OBJECT) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	g_engine.listener.faceX = X_face;
	g_engine.listener.faceY = Y_face;
	g_engine.listener.faceZ = Z_face;
	g_engine.listener.upX = X_up;
	g_engine.listener.upY = Y_up;
	g_engine.listener.upZ = Z_up;
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_set_3D_velocity_vector(H3DPOBJECT, F32, F32, F32)
{
	// No doppler: Miles' was off in this game's settings too.
}

// =================================================================================================
// Streams
// =================================================================================================

HSTREAM AILCALL AIL_open_stream(HDIGDRIVER dig, const char *filename, S32)
{
	if (dig == NULL || filename == NULL || g_engine.xaudio == NULL) {
		return NULL;
	}

	Stream *stream = new Stream;
	if (!openStreamDecoder(stream, filename)) {
		closeStreamDecoder(stream);
		destroyVoice(&stream->voice);
		delete stream;
		return NULL;
	}

	EnterCriticalSection(&g_engine.lock);
	g_engine.streams.push_back(stream);
	LeaveCriticalSection(&g_engine.lock);
	return (HSTREAM)stream;
}

void AILCALL AIL_close_stream(HSTREAM handle)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	for (size_t i = 0; i < g_engine.streams.size(); ++i) {
		if (g_engine.streams[i] == stream) {
			g_engine.streams.erase(g_engine.streams.begin() + i);
			break;
		}
	}
	destroyVoice(&stream->voice);
	stream->queued.clear();
	closeStreamDecoder(stream);
	LeaveCriticalSection(&g_engine.lock);
	delete stream;
}

void AILCALL AIL_start_stream(HSTREAM handle)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL || stream->voice == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	stream->playing = true;
	stream->paused = false;
	stream->exhausted = false;
	stream->voice->SetVolume(clampUnit(stream->volume));
	applyPan(stream->voice, stream->voiceFormat.nChannels, stream->pan);
	stream->voice->Start(0);
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_pause_stream(HSTREAM handle, S32 onoff)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL || stream->voice == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	stream->paused = onoff != 0;
	if (stream->paused) {
		stream->voice->Stop(0);
	} else {
		stream->voice->Start(0);
		stream->playing = true;
	}
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_set_stream_loop_count(HSTREAM handle, S32 count)
{
	Stream *stream = (Stream *)handle;
	if (stream != NULL) {
		stream->loopCount = count;
	}
}

S32 AILCALL AIL_stream_loop_count(HSTREAM handle)
{
	Stream *stream = (Stream *)handle;
	return stream != NULL ? stream->loopCount : 0;
}

void AILCALL AIL_set_stream_volume_pan(HSTREAM handle, F32 volume, F32 pan)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}

	EnterCriticalSection(&g_engine.lock);
	stream->volume = volume;
	stream->pan = pan;
	if (stream->voice != NULL) {
		stream->voice->SetVolume(clampUnit(volume));
		applyPan(stream->voice, stream->voiceFormat.nChannels, pan);
	}
	LeaveCriticalSection(&g_engine.lock);
}

void AILCALL AIL_stream_volume_pan(HSTREAM handle, F32 *volume, F32 *pan)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}
	if (volume != NULL) *volume = stream->volume;
	if (pan != NULL) *pan = stream->pan;
}

void AILCALL AIL_stream_ms_position(HSTREAM handle, S32 *total_milliseconds, S32 *current_milliseconds)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return;
	}

	if (total_milliseconds != NULL) {
		*total_milliseconds = (S32)stream->totalMs;
	}
	if (current_milliseconds != NULL) {
		S32 position = 0;
		if (stream->voice != NULL && stream->voiceFormat.nSamplesPerSec != 0) {
			XAUDIO2_VOICE_STATE state;
			stream->voice->GetState(&state);
			position = (S32)(state.SamplesPlayed * 1000 / stream->voiceFormat.nSamplesPerSec);
		}
		*current_milliseconds = position;
	}
}

AILSTREAMCB AILCALL AIL_register_stream_callback(HSTREAM handle, AILSTREAMCB callback)
{
	Stream *stream = (Stream *)handle;
	if (stream == NULL) {
		return NULL;
	}

	EnterCriticalSection(&g_engine.lock);
	AILSTREAMCB previous = stream->callback;
	stream->callback = callback;
	LeaveCriticalSection(&g_engine.lock);
	return previous;
}

// =================================================================================================
// Quick API - mission briefing speech
// =================================================================================================

HAUDIO AILCALL AIL_quick_load_and_play(const char *filename, U32 loop_count, S32)
{
	HSTREAM stream = AIL_open_stream(DIGITAL_DRIVER, filename, 0);
	if (stream == NULL) {
		return NULL;
	}

	AIL_set_stream_loop_count(stream, loop_count == 0 ? 1 : (S32)loop_count);
	AIL_start_stream(stream);
	return (HAUDIO)stream;
}

void AILCALL AIL_quick_unload(HAUDIO audio)
{
	AIL_close_stream((HSTREAM)audio);
}

void AILCALL AIL_quick_set_volume(HAUDIO audio, F32 volume, F32 extravol)
{
	AIL_set_stream_volume_pan((HSTREAM)audio, volume, extravol);
}

// =================================================================================================
// File format helpers
// =================================================================================================

S32 AILCALL AIL_WAV_info(const void *data, AILSOUNDINFO *info)
{
	WaveImage wave;
	if (info == NULL || !parseWave(data, &wave)) {
		return 0;
	}

	memset(info, 0, sizeof(*info));
	info->format = wave.format.wFormatTag;
	info->data_ptr = wave.data;
	info->data_len = wave.dataBytes;
	info->rate = wave.format.nSamplesPerSec;
	info->bits = wave.format.wBitsPerSample;
	info->channels = wave.format.nChannels;
	info->samples = wavePcmSampleCount(wave);
	info->block_size = wave.format.nBlockAlign;
	info->initial_ptr = data;
	return 1;
}

S32 AILCALL AIL_decompress_ADPCM(const AILSOUNDINFO *info, void **outdata, U32 *outsize)
{
	if (info == NULL || outdata == NULL || outsize == NULL) {
		return 0;
	}

	WaveImage wave;
	if (!parseWave(info->initial_ptr, &wave) || wave.format.wFormatTag != WAVE_FORMAT_IMA_ADPCM) {
		return 0;
	}

	std::vector<short> pcm;
	const unsigned int frames = decodeAdpcm(wave, &pcm);
	if (frames == 0) {
		return 0;
	}

	// The caller hands the result straight to AIL_set_sample_file, so it has to be a WAV image.
	const unsigned int channels = wave.format.nChannels;
	const unsigned int payloadBytes = (unsigned int)(pcm.size() * sizeof(short));
	const unsigned int imageBytes = 44 + payloadBytes;
	unsigned char *image = (unsigned char *)malloc(imageBytes);
	if (image == NULL) {
		return 0;
	}

	const unsigned int blockAlign = channels * 2;
	const unsigned int byteRate = wave.format.nSamplesPerSec * blockAlign;
	memcpy(image, "RIFF", 4);
	*(unsigned int *)(image + 4) = imageBytes - 8;
	memcpy(image + 8, "WAVEfmt ", 8);
	*(unsigned int *)(image + 16) = 16;
	*(unsigned short *)(image + 20) = WAVE_FORMAT_PCM;
	*(unsigned short *)(image + 22) = (unsigned short)channels;
	*(unsigned int *)(image + 24) = wave.format.nSamplesPerSec;
	*(unsigned int *)(image + 28) = byteRate;
	*(unsigned short *)(image + 32) = (unsigned short)blockAlign;
	*(unsigned short *)(image + 34) = 16;
	memcpy(image + 36, "data", 4);
	*(unsigned int *)(image + 40) = payloadBytes;
	memcpy(image + 44, &pcm[0], payloadBytes);

	*outdata = image;
	*outsize = imageBytes;
	return 1;
}

void AILCALL AIL_mem_free_lock(void *ptr)
{
	free(ptr);
}

} // extern "C"
