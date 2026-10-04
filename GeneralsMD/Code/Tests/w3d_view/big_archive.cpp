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
#include "big_archive.h"

#include <algorithm>

namespace {

unsigned bigEndian32(const unsigned char *p)
{
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

std::string normalise(const std::string &name)
{
	std::string key(name);
	for (size_t i = 0; i < key.size(); ++i) {
		char c = key[i];
		if (c == '/') c = '\\';
		if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
		key[i] = c;
	}
	return key;
}

} // namespace

BigArchive::~BigArchive()
{
	if (m_file != NULL) {
		fclose(m_file);
	}
}

bool BigArchive::open(const std::string &path)
{
	m_path = path;
	m_file = fopen(path.c_str(), "rb");
	if (m_file == NULL) {
		return false;
	}
	unsigned char header[16];
	if (fread(header, 1, sizeof(header), m_file) != sizeof(header) || header[0] != 'B' || header[1] != 'I'
		|| header[2] != 'G' || header[3] != 'F') {
		return false;
	}
	const unsigned count = bigEndian32(header + 8);
	fseek(m_file, 0x10, SEEK_SET);
	m_entries.reserve(count);
	for (unsigned i = 0; i < count; ++i) {
		unsigned char pair[8];
		if (fread(pair, 1, sizeof(pair), m_file) != sizeof(pair)) {
			return false;
		}
		std::string name;
		int c;
		while ((c = fgetc(m_file)) != EOF && c != 0) {
			name += (char)c;
		}
		Entry entry;
		entry.key = normalise(name);
		entry.offset = bigEndian32(pair);
		entry.size = bigEndian32(pair + 4);
		m_entries.push_back(entry);
	}
	std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) { return a.key < b.key; });
	return true;
}

const BigArchive::Entry *BigArchive::find(const std::string &name) const
{
	const std::string key = normalise(name);
	std::vector<Entry>::const_iterator it = std::lower_bound(m_entries.begin(), m_entries.end(), key,
		[](const Entry &entry, const std::string &k) { return entry.key < k; });
	return (it != m_entries.end() && it->key == key) ? &*it : NULL;
}

bool BigArchive::contains(const std::string &name) const
{
	return find(name) != NULL;
}

bool BigArchive::read(const std::string &name, std::vector<unsigned char> *out) const
{
	const Entry *entry = find(name);
	if (entry == NULL || m_file == NULL) {
		return false;
	}
	out->resize(entry->size);
	if (fseek(m_file, (long)entry->offset, SEEK_SET) != 0) {
		return false;
	}
	return entry->size == 0 || fread(&(*out)[0], 1, entry->size, m_file) == entry->size;
}

ArchiveSet::~ArchiveSet()
{
	for (size_t i = 0; i < m_archives.size(); ++i) {
		delete m_archives[i];
	}
}

int ArchiveSet::open(const std::vector<std::string> &paths)
{
	int opened = 0;
	for (size_t i = 0; i < paths.size(); ++i) {
		BigArchive *archive = new BigArchive;
		if (archive->open(paths[i])) {
			m_archives.push_back(archive);
			++opened;
		} else {
			delete archive;
		}
	}
	return opened;
}

bool ArchiveSet::read(const std::string &name, std::vector<unsigned char> *out, std::string *from) const
{
	for (size_t i = 0; i < m_archives.size(); ++i) {
		if (m_archives[i]->read(name, out)) {
			if (from != NULL) *from = m_archives[i]->path();
			return true;
		}
	}
	return false;
}
