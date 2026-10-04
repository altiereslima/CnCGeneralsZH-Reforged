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

// The engine's paths, resolved against a POSIX file system.  posixpath.h says what and why; zhio.h's
// forwarders are here too, because each is a resolve followed by the C runtime call.

#include "posixpath.h"
#include "zhio.h"
#include "wwdebug.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

// One directory's entries, keyed by their ASCII-folded names, each key's spellings in byte order.
struct Listing
{
	struct timespec modified;
	std::map<std::string, std::vector<std::string> > by_folded;
};

// Every lookup takes this: Miles opens streams from its service thread through the file system, so
// lookups arrive from more than one thread.
std::mutex g_lock;

// The caches are built on first use rather than at file scope.  Files are opened before main: a
// static constructor that allocates starts the engine's memory manager (GameMemory.h), which looks
// for its pool-size file through zh_fopen, and that can happen before this file's own statics are
// constructed - a file-scope map there was used unconstructed, and then emptied when its constructor
// did run.  (g_lock is a std::mutex, whose constructor is constexpr: it is ready before any code runs.)
std::map<std::string, Listing> & listings()	// by real directory; "" is the current directory
{
	static std::map<std::string, Listing> s_listings;
	return s_listings;
}

std::set<std::string> & reported()			// ambiguities already logged, by directory and folded name
{
	static std::set<std::string> s_reported;
	return s_reported;
}

std::vector<std::string> & overlays()			// P1: read roots before the current directory, under g_lock
{
	static std::vector<std::string> s_overlays;
	return s_overlays;
}

bool g_root_read_only = false;					// P1 step 2; set once, before the engine starts

std::set<std::string> & refused()				// relative writes already logged, under g_lock
{
	static std::set<std::string> s_refused;
	return s_refused;
}

bool is_separator(char c)
{
	return c == '\\' || c == '/';
}

// ASCII only, as the engine's own nocase comparisons are.  A byte above 0x7F compares as itself.
std::string fold(const std::string & name)
{
	std::string folded(name);
	for (size_t i = 0; i < folded.size(); ++i) {
		if (folded[i] >= 'A' && folded[i] <= 'Z') folded[i] = (char)(folded[i] - 'A' + 'a');
	}
	return folded;
}

std::string join(const std::string & directory, const std::string & name)
{
	if (directory.empty()) return name;
	if (directory == "/") return "/" + name;
	return directory + "/" + name;
}

std::string parent_of(const std::string & real_path)
{
	const size_t slash = real_path.rfind('/');
	if (slash == std::string::npos) return "";
	if (slash == 0) return "/";
	return real_path.substr(0, slash);
}

bool modification_time(const std::string & directory, struct timespec & modified)
{
	struct stat status;
	if (stat(directory.empty() ? "." : directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
		return false;
	}
#if defined(__APPLE__)
	modified = status.st_mtimespec;
#else
	modified = status.st_mtim;
#endif
	return true;
}

// The listing of a directory, read again only when its modification time has moved.  Called with
// g_lock held.  NULL when the directory cannot be read.
const Listing * listing_of(const std::string & directory)
{
	struct timespec now;
	if (!modification_time(directory, now)) {
		return NULL;
	}
	std::map<std::string, Listing>::iterator cached = listings().find(directory);
	if (cached != listings().end() && cached->second.modified.tv_sec == now.tv_sec
		&& cached->second.modified.tv_nsec == now.tv_nsec) {
		return &cached->second;
	}

	DIR * handle = opendir(directory.empty() ? "." : directory.c_str());
	if (handle == NULL) {
		return NULL;
	}
	Listing fresh;
	fresh.modified = now;
	while (struct dirent * entry = readdir(handle)) {
		const std::string name = entry->d_name;
		if (name == "." || name == "..") continue;
		fresh.by_folded[fold(name)].push_back(name);
	}
	closedir(handle);
	for (std::map<std::string, std::vector<std::string> >::iterator it = fresh.by_folded.begin(); it != fresh.by_folded.end(); ++it) {
		std::sort(it->second.begin(), it->second.end());
	}
	Listing & stored = listings()[directory];
	stored = fresh;
	return &stored;
}

// The spelling on disk of one component, matched without regard to case.  Empty when there is none.
std::string match_in(const std::string & directory, const std::string & component)
{
	std::lock_guard<std::mutex> guard(g_lock);
	const Listing * listing = listing_of(directory);
	if (listing == NULL) {
		return std::string();
	}
	const std::string folded = fold(component);
	std::map<std::string, std::vector<std::string> >::const_iterator found = listing->by_folded.find(folded);
	if (found == listing->by_folded.end()) {
		return std::string();
	}
	// D2: an exact spelling would have been found by the caller's own stat, so this is the case where
	// none exists; byte order decides, and it is said once.
	if (found->second.size() > 1 && reported().insert(directory + "/" + folded).second) {
		WWDEBUG_WARNING(("PosixPath: \"%s\" matches %u names in \"%s\" that differ only in case; using \"%s\"\n",
			component.c_str(), (unsigned)found->second.size(), directory.empty() ? "." : directory.c_str(),
			found->second[0].c_str()));
	}
	return found->second[0];
}

} // namespace

namespace {

// One resolution from `start` ("" the current directory, "/" for an absolute path, or an overlay).
bool resolve_from(const std::string & start, const std::vector<std::string> & components, PosixPathIntent intent,
	std::string & real_path)
{
	std::string current = start;
	bool missing = false;	// once one component is missing, none after it can exist
	for (size_t i = 0; i < components.size(); ++i) {
		const std::string & name = components[i];
		const bool last = i + 1 == components.size();
		if (name == ".") {
			continue;
		}
		if (missing || name == "..") {
			current = join(current, name);
			continue;
		}
		const std::string exact = join(current, name);
		struct stat status;
		if (lstat(exact.c_str(), &status) == 0) {
			current = exact;
			continue;
		}
		const std::string on_disk = match_in(current, name);
		if (!on_disk.empty()) {
			current = join(current, on_disk);
			continue;
		}
		if (intent == POSIX_PATH_EXISTING || intent == POSIX_PATH_EXISTING_IN_ROOT
			|| (intent == POSIX_PATH_CREATE_LEAF && !last)) {
			return false;
		}
		missing = true;
		current = exact;
	}
	real_path = current.empty() ? "." : current;
	return true;
}

bool split_components(const char * engine_path, std::vector<std::string> & components)
{
	if (engine_path == NULL || engine_path[0] == '\0') {
		return false;
	}
	// A UNC path, and a drive letter: Windows spellings no POSIX file system has.
	if (is_separator(engine_path[0]) && is_separator(engine_path[1])) {
		return false;
	}
	if (((engine_path[0] >= 'A' && engine_path[0] <= 'Z') || (engine_path[0] >= 'a' && engine_path[0] <= 'z'))
		&& engine_path[1] == ':') {
		return false;
	}
	std::string component;
	for (const char * at = engine_path; ; ++at) {
		if (*at == '\0' || is_separator(*at)) {
			if (!component.empty()) components.push_back(component);
			component.clear();
			if (*at == '\0') break;
		}
		else {
			component += *at;
		}
	}
	return true;
}

std::vector<std::string> overlays_now()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return overlays();
}

} // namespace

bool PosixPath_Resolve(const char * engine_path, PosixPathIntent intent, std::string & real_path)
{
	real_path.clear();
	std::vector<std::string> components;
	if (!split_components(engine_path, components)) {
		return false;
	}
	if (is_separator(engine_path[0])) {
		return resolve_from("/", components, intent, real_path);
	}
	// P1: a relative path that is only read is looked for in each overlay first
	if (intent == POSIX_PATH_EXISTING) {
		const std::vector<std::string> roots = overlays_now();
		for (size_t i = 0; i < roots.size(); ++i) {
			if (resolve_from(roots[i], components, POSIX_PATH_EXISTING, real_path)) {
				return true;
			}
		}
	}
	return resolve_from("", components, intent, real_path);
}

void PosixPath_Set_Overlays(const std::vector<std::string> & real_directories)
{
	std::lock_guard<std::mutex> guard(g_lock);
	overlays() = real_directories;
}

std::vector<std::string> PosixPath_Overlays()
{
	return overlays_now();
}

void PosixPath_Set_Root_Read_Only(bool read_only)
{
	std::lock_guard<std::mutex> guard(g_lock);
	g_root_read_only = read_only;
}

bool PosixPath_Root_Read_Only()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_root_read_only;
}

void PosixPath_Forget_Directory(const char * real_directory)
{
	std::lock_guard<std::mutex> guard(g_lock);
	listings().erase(real_directory != NULL && strcmp(real_directory, ".") != 0 ? real_directory : "");
}

void PosixPath_Forget_All()
{
	std::lock_guard<std::mutex> guard(g_lock);
	listings().clear();
	reported().clear();
}

// ---- listing, as Win32LocalFileSystem lists ------------------------------------------------------

namespace {

char fold_char(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// '*' and '?' over the whole name, ASCII case folded.  Iterative, with one backtrack point for the
// last '*', which is exact for patterns with only '*' and '?'.
bool glob_matches(const char * pattern, const char * name)
{
	const char * star = NULL;
	const char * resume = NULL;
	while (*name != '\0') {
		if (*pattern == '*') {
			star = pattern++;
			resume = name;
		}
		else if (*pattern == '?' || (*pattern != '\0' && fold_char(*pattern) == fold_char(*name))) {
			++pattern;
			++name;
		}
		else if (star != NULL) {
			pattern = star + 1;
			name = ++resume;
		}
		else {
			return false;
		}
	}
	while (*pattern == '*') ++pattern;
	return *pattern == '\0';
}

struct Entry
{
	std::string name;
	bool directory;
};

// A directory's entries, less "." and "..", in byte order.  Empty when it cannot be read.
std::vector<Entry> entries_of(const std::string & real_directory)
{
	std::vector<Entry> entries;
	DIR * handle = opendir(real_directory.empty() ? "." : real_directory.c_str());
	if (handle == NULL) {
		return entries;
	}
	while (struct dirent * found = readdir(handle)) {
		Entry entry;
		entry.name = found->d_name;
		if (entry.name == "." || entry.name == "..") continue;
		struct stat status;
		const std::string path = join(real_directory, entry.name);
		entry.directory = stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
		entries.push_back(entry);
	}
	closedir(handle);
	std::sort(entries.begin(), entries.end(), [](const Entry & a, const Entry & b) { return a.name < b.name; });
	return entries;
}

// "a\b\*.ini" -> directory "a\b", pattern "*.ini".  With no separator, the directory is empty.
void split_search(const std::string & search, std::string & directory, std::string & pattern)
{
	size_t last = std::string::npos;
	for (size_t i = 0; i < search.size(); ++i) {
		if (is_separator(search[i])) last = i;
	}
	if (last == std::string::npos) {
		directory.clear();
		pattern = search;
	}
	else {
		directory = search.substr(0, last + 1);
		pattern = search.substr(last + 1);
	}
}

// The real directory an engine directory names in the current directory (or itself, when absolute);
// "" for an empty one.  False when it does not exist, which for a listing means nothing is found, as
// FindFirstFile finds nothing.
bool real_directory_of_root(const std::string & engine_directory, std::string & real)
{
	if (engine_directory.empty()) {
		real.clear();
		return true;
	}
	// The current directory's (or an absolute path's) own copy: the overlays are listed separately
	if (!PosixPath_Resolve(engine_directory.c_str(), POSIX_PATH_EXISTING_IN_ROOT, real)) {
		return false;
	}
	if (real == ".") real.clear();
	return true;
}

// A directory's entries in every root that has it (P1): an absolute directory in itself, a relative one
// in each overlay and then the current directory, merged by name without regard to case across roots
// (a name an earlier root has is that root's), then byte order.  Without overlays it is entries_of, unchanged.
std::vector<Entry> merged_entries_of(const std::string & engine_directory)
{
	std::vector<std::string> roots;
	if (engine_directory.empty() || !is_separator(engine_directory[0])) {
		roots = overlays_now();
	}
	std::vector<Entry> merged;
	std::set<std::string> seen;
	for (size_t r = 0; r <= roots.size(); ++r) {
		std::string real;
		if (r < roots.size()) {
			std::vector<std::string> components;
			if (engine_directory.empty()) {
				real = roots[r];
			}
			else if (!split_components(engine_directory.c_str(), components)
				|| !resolve_from(roots[r], components, POSIX_PATH_EXISTING, real)) {
				continue;
			}
		}
		else if (!real_directory_of_root(engine_directory, real)) {
			continue;
		}
		// A name an earlier root holds is that root's; within one root every entry stays, as without
		// overlays (a case-sensitive volume can hold two names that differ only in case)
		const std::vector<Entry> entries = entries_of(real);
		std::set<std::string> here;
		for (size_t i = 0; i < entries.size(); ++i) {
			const std::string folded = fold(entries[i].name);
			if (seen.count(folded) == 0) {
				merged.push_back(entries[i]);
				here.insert(folded);
			}
		}
		seen.insert(here.begin(), here.end());
	}
	if (!roots.empty()) {
		std::sort(merged.begin(), merged.end(), [](const Entry & a, const Entry & b) { return a.name < b.name; });
	}
	return merged;
}

} // namespace

bool PosixPath_Matches_Pattern(const char * pattern, const char * name)
{
	if (pattern == NULL || name == NULL || pattern[0] == '\0') {
		return false;
	}
	if (strcmp(pattern, "*.*") == 0) {
		return true;
	}
	if (strcmp(pattern, "*.") == 0) {
		return strchr(name, '.') == NULL;
	}
	return glob_matches(pattern, name);
}

void PosixPath_List_Like_Win32(const std::string & current_directory, const std::string & original_directory,
	const std::string & search_name, bool search_subdirectories, std::vector<std::string> & found)
{
	std::string directory, pattern;
	split_search(original_directory + current_directory + search_name, directory, pattern);
	{
		const std::vector<Entry> entries = merged_entries_of(directory);
		for (size_t i = 0; i < entries.size(); ++i) {
			if (!entries[i].directory && PosixPath_Matches_Pattern(pattern.c_str(), entries[i].name.c_str())) {
				found.push_back(original_directory + current_directory + entries[i].name);
			}
		}
	}

	if (!search_subdirectories) {
		return;
	}
	split_search(original_directory + current_directory + "*.", directory, pattern);
	const std::vector<Entry> entries = merged_entries_of(directory);
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].directory && PosixPath_Matches_Pattern(pattern.c_str(), entries[i].name.c_str())) {
			PosixPath_List_Like_Win32(current_directory + entries[i].name + '\\', original_directory, search_name,
				search_subdirectories, found);
		}
	}
}

// ---- the zh_* forwarders (zhio.h) ------------------------------------------------------------------

namespace {

bool resolve_or_fail(const char * path, PosixPathIntent intent, std::string & real)
{
	if (PosixPath_Resolve(path, intent, real)) {
		return true;
	}
	errno = ENOENT;
	return false;
}

void forget_parent(const std::string & real)
{
	PosixPath_Forget_Directory(parent_of(real).c_str());
}

// P1 step 2: true (and errno EROFS) when `path` is relative and the roots are read-only.  Said once a
// path, in the debug log, so the audit's two known writers - GameEngine::init's Data\INI\INIZH.big
// and the patch check's PatchAccessTest.txt - show up as refused, and any writer the audit missed too.
bool refuses_write(const char * path, const char * what)
{
	if (path == NULL || path[0] == '\0' || is_separator(path[0])) {
		return false;
	}
	bool first;
	{
		std::lock_guard<std::mutex> guard(g_lock);
		if (!g_root_read_only) {
			return false;
		}
		first = refused().insert(path).second;
	}
	if (first) {
		// stderr as well as the debug log: WWDEBUG_WARNING is compiled out of a release build, and a
		// refusal is what the harness (root-readonly-check.sh) and a bug report need to see
		fprintf(stderr, "generals: refused to %s \"%s\": the install root is read-only (P1)\n", what, path);
		WWDEBUG_WARNING(("PosixPath: refused to %s \"%s\": the install root is read-only (P1)\n", what, path));
	}
	errno = EROFS;
	return true;
}

} // namespace

FILE * zh_fopen(const char * path, const char * mode)
{
	const bool creates = mode != NULL && (mode[0] == 'w' || mode[0] == 'a');
	const bool writes = mode != NULL && strchr(mode, '+') != NULL;
	if ((creates || writes) && refuses_write(path, "write")) {
		return NULL;
	}
	std::string real;
	if (!resolve_or_fail(path, creates ? POSIX_PATH_CREATE_LEAF : (writes ? POSIX_PATH_EXISTING_IN_ROOT : POSIX_PATH_EXISTING), real)) {
		return NULL;
	}
	FILE * file = fopen(real.c_str(), mode);
	if (file != NULL && creates) {
		forget_parent(real);
	}
	return file;
}

int zh_open(const char * path, int flags, int permissions)
{
	const bool creates = (flags & O_CREAT) != 0;
	const bool writes = (flags & (O_WRONLY | O_RDWR | O_TRUNC | O_APPEND)) != 0;
	if ((creates || writes) && refuses_write(path, "write")) {
		return -1;
	}
	std::string real;
	if (!resolve_or_fail(path, creates ? POSIX_PATH_CREATE_LEAF : (writes ? POSIX_PATH_EXISTING_IN_ROOT : POSIX_PATH_EXISTING), real)) {
		return -1;
	}
	const int handle = open(real.c_str(), flags, permissions);
	if (handle >= 0 && creates) {
		forget_parent(real);
	}
	return handle;
}

int zh_access(const char * path, int mode)
{
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_EXISTING, real)) {
		return -1;
	}
	return access(real.c_str(), mode);
}

int zh_remove(const char * path)
{
	if (refuses_write(path, "remove")) {
		return -1;
	}
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_EXISTING_IN_ROOT, real)) {
		return -1;
	}
	const int result = remove(real.c_str());
	if (result == 0) {
		forget_parent(real);
	}
	return result;
}

int zh_unlink(const char * path)
{
	if (refuses_write(path, "delete")) {
		return -1;
	}
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_EXISTING_IN_ROOT, real)) {
		return -1;
	}
	const int result = unlink(real.c_str());
	if (result == 0) {
		forget_parent(real);
	}
	return result;
}

int zh_rename(const char * from, const char * to)
{
	if (refuses_write(from, "rename") || refuses_write(to, "rename onto")) {
		return -1;
	}
	std::string real_from, real_to;
	if (!resolve_or_fail(from, POSIX_PATH_EXISTING_IN_ROOT, real_from) || !resolve_or_fail(to, POSIX_PATH_CREATE_LEAF, real_to)) {
		return -1;
	}
	const int result = rename(real_from.c_str(), real_to.c_str());
	if (result == 0) {
		forget_parent(real_from);
		forget_parent(real_to);
	}
	return result;
}

int zh_mkdir(const char * path)
{
	if (refuses_write(path, "create the directory")) {
		return -1;
	}
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_CREATE_LEAF, real)) {
		return -1;
	}
	const int result = mkdir(real.c_str(), 0777);
	if (result == 0) {
		forget_parent(real);
	}
	return result;
}

int zh_stat(const char * path, struct stat * status)
{
	std::string real;
	if (!resolve_or_fail(path, POSIX_PATH_EXISTING, real)) {
		return -1;
	}
	return stat(real.c_str(), status);
}

int zh_read_text(int handle, void * buffer, unsigned bytes)
{
	const ssize_t got = read(handle, buffer, bytes);
	if (got <= 0) {
		return (int)got;
	}
	char * in = (char *)buffer;
	char * out = in;
	for (ssize_t i = 0; i < got; ++i) {
		if (in[i] != '\r') {
			*out++ = in[i];
		}
		else if (i + 1 < got) {
			if (in[i + 1] == '\n') ++i;
			*out++ = in[i];
		}
		else {
			char next;
			const ssize_t peeked = read(handle, &next, 1);
			if (peeked == 1 && next == '\n') {
				*out++ = '\n';
			}
			else {
				*out++ = '\r';
				if (peeked == 1) lseek(handle, -1, SEEK_CUR);
			}
		}
	}
	return (int)(out - in);
}
