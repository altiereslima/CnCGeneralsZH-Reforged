/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// A3a's frame (PosixDevice/Render/SdlGpuFrame), on this machine's GPU, offscreen, read back:
//   - a colour clear lands as D3D9's ARGB in the back buffer's B8G8R8A8 bytes, every pixel;
//   - a depth-only clear leaves the colour as it was (the colour is loaded, not cleared);
//   - the gamma pass maps each channel through its own ramp, on a picture where every pixel differs, so
//     a flipped, shifted or channel-swapped pass cannot match; the identity ramp's blit gives the picture
//     back unchanged;
//   - an armed control: the gamma output compared against the wrong ramp must not match.
//
// WHAT THIS DOES NOT PROVE:
//   - Presenting to a window: the swap chain needs a display, and ctest has none.  The window's path is
//     the same pass into the swap chain's texture instead of this test's.
//   - Any GPU but this one.  Exits 77, and ctest reports it skipped, where SDL has no GPU device.

#include "SdlGpuFrame.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static const unsigned int WIDTH = 64;
static const unsigned int HEIGHT = 48;

// B, G, R and A of pixel (x, y) in the gamma test's picture: no two pixels alike, no channel alike.
static void pattern(unsigned int x, unsigned int y, uint8_t out[4])
{
	out[0] = (uint8_t)(x * 4);
	out[1] = (uint8_t)(y * 5);
	out[2] = (uint8_t)((x * 3 + y * 7) & 0xFF);
	out[3] = 0xFF;
}

// How many pixels are more than one level from expected in any channel.
static unsigned int mismatches(const std::vector<uint8_t> &got, const uint16_t (*ramp)[256])
{
	unsigned int bad = 0;
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			uint8_t in[4];
			pattern(x, y, in);
			const uint8_t *px = &got[(y * WIDTH + x) * 4];
			// The ramp is indexed R, G, B; the bytes are B, G, R.
			const int expect[4] = {
				(int)((ramp[2][in[0]] * 255u + 32767u) / 65535u),
				(int)((ramp[1][in[1]] * 255u + 32767u) / 65535u),
				(int)((ramp[0][in[2]] * 255u + 32767u) / 65535u),
				in[3] };
			for (int c = 0; c < 4; ++c) {
				if (abs((int)px[c] - expect[c]) > 1) {
					++bad;
					break;
				}
			}
		}
	}
	return bad;
}

int main()
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("sdl_gpu_frame_selfcheck: SKIP - SDL_Init: %s\n", SDL_GetError());
		return 77;
	}
	std::string error;
	SdlGpuFrame *frame = SdlGpuFrame::Create(NULL, WIDTH, HEIGHT, error);
	if (frame == NULL) {
		printf("sdl_gpu_frame_selfcheck: SKIP - no GPU device here: %s\n", error.c_str());
		SDL_Quit();
		return 77;
	}
	printf("sdl_gpu_frame_selfcheck: %s\n", SDL_GetGPUDeviceDriver(frame->Device()));

	// A colour clear: D3DCOLOR 0x80402010 is A 0x80, R 0x40, G 0x20, B 0x10.
	frame->Clear_Back_Buffer(true, true, true, 0x80402010u, 1.0f, 0);
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	unsigned int wrong = 0;
	for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
		if (pixels[i] != 0x10 || pixels[i + 1] != 0x20 || pixels[i + 2] != 0x40 || pixels[i + 3] != 0x80) {
			++wrong;
		}
	}
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && wrong == 0);

	// Depth and stencil only: the colour is loaded, so it stays.
	frame->Clear_Back_Buffer(false, true, true, 0xFFFFFFFFu, 0.5f, 7);
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() >= 4 && pixels[0] == 0x10 && pixels[2] == 0x40);

	// The gamma pass, into a target of the test's.
	std::vector<uint8_t> picture(WIDTH * HEIGHT * 4);
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			pattern(x, y, &picture[(y * WIDTH + x) * 4]);
		}
	}
	CHECK(frame->Upload_Back_Buffer(picture));

	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	info.width = WIDTH;
	info.height = HEIGHT;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(target != NULL);

	uint16_t identity[3][256];
	uint16_t ramp[3][256];
	for (int i = 0; i < 256; ++i) {
		identity[0][i] = identity[1][i] = identity[2][i] = (uint16_t)(i * 257);
		ramp[0][i] = (uint16_t)((255 - i) * 257);	// red inverted
		ramp[1][i] = (uint16_t)(i * 257);			// green as it is
		ramp[2][i] = (uint16_t)((i / 2) * 257);		// blue halved
	}
	CHECK(Sdl_Gamma_Is_Identity(identity) && !Sdl_Gamma_Is_Identity(ramp));

	CHECK(frame->Present_To(target, WIDTH, HEIGHT, ramp));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	const unsigned int gamma_wrong = mismatches(pixels, ramp);
	CHECK(gamma_wrong == 0);
	// The armed control: the same output against the identity must differ nearly everywhere.
	const unsigned int control = mismatches(pixels, identity);
	CHECK(control > WIDTH * HEIGHT / 2);

	// The identity ramp is a blit: the picture back as it went in.
	CHECK(frame->Present_To(target, WIDTH, HEIGHT, identity));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	SDL_ReleaseGPUTexture(frame->Device(), target);
	delete frame;
	SDL_Quit();
	if (failures != 0) {
		printf("sdl_gpu_frame_selfcheck: %d FAILED (gamma: %u pixels wrong; control differed in %u)\n",
			failures, gamma_wrong, control);
		return 1;
	}
	printf("sdl_gpu_frame_selfcheck: clear, depth-only clear, gamma (control differs in %u of %u pixels) "
		"and the identity blit all hold\n", control, WIDTH * HEIGHT);
	return 0;
}
