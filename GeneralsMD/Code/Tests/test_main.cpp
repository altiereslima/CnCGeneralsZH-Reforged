/*
 * Runner for the micro-harness in test_harness.h.  One of these is linked into
 * every test binary; the tests themselves register from static initialisers.
 *
 * Exit code is the number of failed checks (capped at 125 so it stays a valid
 * process exit status), which is what CTest keys off.
 */
#include "test_harness.h"

#include <stdlib.h>
#include <chrono>

namespace
{
	/* Fixed-capacity registry: static-init order is unspecified, so a
	   std::vector here would risk being constructed after the first
	   registrar runs.  A POD array is zero-initialised before any of it. */
	const int MAX_TESTS = 1024;

	struct Entry
	{
		const char *name;
		void (*fn)(void);
	};

	Entry g_tests[MAX_TESTS];
	int g_numTests;

	const char *g_currentTest;
	int g_currentFails;
	int g_totalChecks;
	int g_totalFails;
	int g_failedTests;
}

void test_register(const char *name, void (*fn)(void))
{
	if (g_numTests >= MAX_TESTS)
	{
		fprintf(stderr, "test registry full, raise MAX_TESTS\n");
		exit(1);
	}
	g_tests[g_numTests].name = name;
	g_tests[g_numTests].fn = fn;
	++g_numTests;
}

void test_check(bool ok, const char *expr, const char *file, int line)
{
	++g_totalChecks;
	if (ok)
		return;

	++g_currentFails;
	++g_totalFails;

	/* Strip the directory so failures stay readable in a narrow console. */
	const char *base = strrchr(file, '\\');
	if (!base)
		base = strrchr(file, '/');
	printf("  FAIL %s:%d  %s\n", base ? base + 1 : file, line, expr);
}

int main(int argc, char *argv[])
{
	/* Optional substring filter, so a single failing test can be re-run alone:
	   test_wwlib.exe crc
	   and "--exclude=<substring>", repeatable, for the tests another ctest entry runs by themselves
	   (test_gameengine's money sweep, one entry per player count, so ctest -j runs them side by side). */
	const char *filter = 0;
	const char *excluded[16];
	int numExcluded = 0;
	for (int a = 1; a < argc; ++a)
	{
		if (strncmp(argv[a], "--exclude=", 10) == 0)
		{
			if (numExcluded == (int)(sizeof(excluded) / sizeof(excluded[0])))
			{
				fprintf(stderr, "too many --exclude arguments\n");
				return 1;
			}
			excluded[numExcluded++] = argv[a] + 10;
		}
		else if (!filter)
			filter = argv[a];
	}
	int ran = 0;

	/* ZH_TEST_PROGRESS names each test as it starts and says how long it took, flushed, so a run
	   that times out shows where it was (a Debug build of test_gameengine, W3).  Unset, the output
	   is the failures and the summary, as always. */
	const bool progress = getenv("ZH_TEST_PROGRESS") != 0;

	for (int i = 0; i < g_numTests; ++i)
	{
		if (filter && !strstr(g_tests[i].name, filter))
			continue;
		bool skip = false;
		for (int e = 0; e < numExcluded; ++e)
			skip = skip || strstr(g_tests[i].name, excluded[e]) != 0;
		if (skip)
			continue;

		g_currentTest = g_tests[i].name;
		g_currentFails = 0;
		++ran;

		const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
		if (progress)
		{
			printf("RUN  %s\n", g_currentTest);
			fflush(stdout);
		}

		g_tests[i].fn();

		if (progress)
		{
			printf("DONE %s %.1f s\n", g_currentTest,
				std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
			fflush(stdout);
		}

		if (g_currentFails)
		{
			++g_failedTests;
			printf("FAIL %s (%d)\n", g_currentTest, g_currentFails);
		}
	}

	printf("%d tests, %d checks, %d failed\n", ran, g_totalChecks, g_totalFails);

	if (!ran)
	{
		printf("no tests matched\n");
		return 1;
	}

	return g_failedTests > 125 ? 125 : g_failedTests;
}
