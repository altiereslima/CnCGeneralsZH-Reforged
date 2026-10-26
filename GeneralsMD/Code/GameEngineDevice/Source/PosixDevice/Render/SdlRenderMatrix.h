// Render-only row-major matrix operations. No simulation arithmetic uses this header.
#pragma once
#include <math.h>

static void multiply(const float left[16], const float right[16], float result[16])
{
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (unsigned index = 0; index < 4; ++index) {
				sum += left[row * 4 + index] * right[index * 4 + column];
			}
			result[row * 4 + column] = sum;
		}
	}
}

// Visual-only inverse for reconstructing render positions from depth. Double intermediates keep
// distant camera translations stable; this does not reach simulation or replay arithmetic.
static bool render_inverse(const float source[16], float result[16])
{
	double rows[4][8];
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 8; ++c)
			rows[r][c] = c < 4 ? source[r * 4 + c] : (c - 4 == r ? 1.0 : 0.0);
	for (unsigned c = 0; c < 4; ++c) {
		unsigned pivot = c;
		for (unsigned r = c + 1; r < 4; ++r)
			if (fabs(rows[r][c]) > fabs(rows[pivot][c])) pivot = r;
		if (!isfinite(rows[pivot][c]) || fabs(rows[pivot][c]) < 1e-15) return false;
		for (unsigned k = 0; k < 8; ++k) {
			const double saved = rows[c][k]; rows[c][k] = rows[pivot][k]; rows[pivot][k] = saved;
		}
		const double scale = rows[c][c];
		for (unsigned k = 0; k < 8; ++k) rows[c][k] /= scale;
		for (unsigned r = 0; r < 4; ++r) {
			if (r == c) continue;
			const double factor = rows[r][c];
			for (unsigned k = 0; k < 8; ++k) rows[r][k] -= factor * rows[c][k];
		}
	}
	for (unsigned r = 0; r < 4; ++r)
		for (unsigned c = 0; c < 4; ++c) result[r * 4 + c] = (float)rows[r][c + 4];
	return true;
}
