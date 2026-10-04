// Self-check for Matrix3D::Get_Inverse, which was reimplemented when the D3DX8
// dependency was dropped.  Run the 'wwmath_selfcheck' target; exit code 0 means pass.
//
// What this establishes: Get_Inverse inverts an affine matrix, and for one matrix whose inverse is
// exactly representable (power-of-two scales, see below) it produces those exact bits.
//
// What it does NOT establish: determinism.  The rotation case is checked to a 1e-4 tolerance, which
// passes under any rounding, with or without FMA contraction, on any compiler.  The exact case is
// exact on every IEEE machine by construction, so it cannot tell two compilers apart either.  It is
// not evidence that DetTrig or anything else computes the same bits on two platforms; E3 is.
//
// Failures are counted and returned, not assert()ed: this is built Release, where -DNDEBUG compiles
// assert out.  It used to print FAIL, carry on, print OK and exit 0, which ctest read as a pass.

#include "matrix3d.h"
#include <stdio.h>

static int failures = 0;

static void check_identity(const Matrix3D & m, const char * what)
{
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 4; c++) {
			float expected = (r == c) ? 1.0f : 0.0f;
			float diff = m[r][c] - expected;
			if (diff < 0.0f) diff = -diff;
			if (diff > 0.0001f) {
				printf("FAIL %s: [%d][%d] = %f, expected %f\n", what, r, c, m[r][c], expected);
				failures++;
			}
		}
	}
}

static void check_roundtrip(const Matrix3D & m, const char * what)
{
	Matrix3D inv, product;
	m.Get_Inverse(inv);

	Matrix3D::Multiply(m, inv, &product);
	check_identity(product, what);

	Matrix3D::Multiply(inv, m, &product);
	check_identity(product, what);
}

static void check_exact(const Matrix3D & m, const float expected[3][4], const char * what)
{
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 4; c++) {
			if (m[r][c] != expected[r][c]) {
				printf("FAIL %s: [%d][%d] = %.9g, expected exactly %.9g\n", what, r, c, m[r][c], expected[r][c]);
				failures++;
			}
		}
	}
}

int main(void)
{
	// Rotation about an arbitrary axis plus a translation - the general affine case.
	// The axis-angle form asserts a unit axis; a Debug build aborts on anything else.
	Vector3 axis(0.3f, -0.7f, 0.65f);
	axis.Normalize();
	Matrix3D rot(axis, 1.1f);
	rot.Set_Translation(Vector3(12.0f, -4.5f, 3.25f));
	check_roundtrip(rot, "rotate+translate");

	// Non-uniform scale: the orthogonal-inverse shortcut gets this one wrong.
	Matrix3D scale(Vector3(2.0f, 0.0f, 0.0f),
	               Vector3(0.0f, 0.5f, 0.0f),
	               Vector3(0.0f, 0.0f, 4.0f),
	               Vector3(1.0f, 2.0f, 3.0f));
	check_roundtrip(scale, "scale+translate");

	// The same matrix against an absolute answer rather than against itself.  Every scale is a power
	// of two, so each reciprocal and each product in the inverse is exact: this is the inverse, bit for
	// bit, on any IEEE compiler.  -0.0 compares equal to 0.0, so the sign of a zero is not checked.
	static const float scale_inverse[3][4] = {
		{ 0.5f, 0.0f, 0.0f,  -0.5f  },
		{ 0.0f, 2.0f, 0.0f,  -4.0f  },
		{ 0.0f, 0.0f, 0.25f, -0.75f },
	};
	Matrix3D inv;
	scale.Get_Inverse(inv);
	check_exact(inv, scale_inverse, "scale+translate inverse");

	if (failures != 0) {
		printf("Matrix3D::Get_Inverse FAILED: %d check(s)\n", failures);
		return 1;
	}
	printf("Matrix3D::Get_Inverse OK\n");
	return 0;
}
