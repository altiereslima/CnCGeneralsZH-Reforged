/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   Generals
//
// Module:    Game Engine Common
//
// File name: ArchiveFileSystem.cpp
//
// Created:   11/26/01 TR
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/file.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/PerfTimer.h"

#include <vector>

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------

ArchiveFileSystem *TheArchiveFileSystem = NULL;


//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------

//------------------------------------------------------
// ArchivedFileInfo
//------------------------------------------------------
ArchiveFileSystem::ArchiveFileSystem() : m_focus( FILE_FOCUS_ALL )
{
}

ArchiveFileSystem::~ArchiveFileSystem() 
{
	ArchiveFileMap::iterator iter = m_archiveFileMap.begin();
	while (iter != m_archiveFileMap.end()) {
		ArchiveFile *file = iter->second;
		if (file != NULL) {
			delete file;
			file = NULL;
		}
		iter++;
	}
}

void ArchiveFileSystem::loadIntoDirectoryTree(const ArchiveFile *archiveFile, const AsciiString& archiveFilename, Bool overwrite)
{
	addToTree(m_rootDirectory, archiveFile, archiveFilename, overwrite);
}

void ArchiveFileSystem::addToTree(ArchivedDirectoryInfo &root, const ArchiveFile *archiveFile, const AsciiString& archiveFilename, Bool overwrite)
{

	FilenameList filenameList;

	archiveFile->getFileListInDirectory(AsciiString(""), AsciiString(""), AsciiString("*"), filenameList, TRUE);

	// a mod's maps never go in, merged or kept apart: the maps are ours (FileSystem::isMapPath)
	const Bool modArchive = modLayerOf(archiveFilename).isNotEmpty();
	Int mapsIgnored = 0;
	Int uiKept = 0;

	FilenameListIter it = filenameList.begin();

	while (it != filenameList.end()) {
		if (modArchive && FileSystem::isMapPath(*it)) {
			++mapsIgnored;
			it++;
			continue;
		}
		// add this filename to the directory tree.
		AsciiString path = *it;
		path.toLower();
		AsciiString token;
		AsciiString debugpath;

		ArchivedDirectoryInfo *dirInfo = &root;

		Bool infoInPath;
		infoInPath = path.nextToken(&token, "\\/");

		while (infoInPath && (!token.find('.') || path.find('.'))) {
			ArchivedDirectoryInfoMap::iterator tempiter = dirInfo->m_directories.find(token);
			if (tempiter == dirInfo->m_directories.end()) 
			{
				dirInfo->m_directories[token].clear();
				dirInfo->m_directories[token].m_directoryName = token;
			}

			dirInfo = &(dirInfo->m_directories[token]);
			debugpath.concat(token);
			debugpath.concat('\\');
			infoInPath = path.nextToken(&token, "\\/");
		}

		// token is the filename, and dirInfo is the directory that this file is in.
		AsciiString path2;
		path2 = debugpath;
		path2.concat(token);
		// a merged mod's interface file stays out where retail or the fork has one (FileSystem::isForkUIPath);
		// an image sheet of it is still read first, under its "ModUI~" name, for the images only the mod
		// has.  A texture our sheets name stays out even where only its other extension is ours, because
		// the loader takes a .dds before the .tga it was asked for.
		ArchivedFileLocationMap::const_iterator held = dirInfo->m_files.find(token);
		const Bool oursHeld = held != dirInfo->m_files.end() && modLayerOf(held->second).isEmpty();
		if (modArchive && &root == &m_rootDirectory && (isOurUITexture(path2) || (oursHeld && FileSystem::isForkUIPath(path2))))
		{
			if (path2.startsWith("data\\ini\\mappedimages\\"))
				m_modUICopies[path2] = archiveFilename;
			++uiKept;
			it++;
			continue;
		}
		if (held == dirInfo->m_files.end() || overwrite) {
//			DEBUG_LOG(("ArchiveFileSystem::loadIntoDirectoryTree - adding file %s, archived in %s\n", path2.str(), archiveFilename.str()));
			dirInfo->m_files[token] = archiveFilename;
		}

		it++;
	}
	if (mapsIgnored > 0)
		DEBUG_LOG(("ArchiveFileSystem - %d map files in %s are ignored, the maps are ours\n", mapsIgnored, archiveFilename.str()));
	if (uiKept > 0)
		DEBUG_LOG(("ArchiveFileSystem - %d interface files in %s are ignored, the interface is ours\n", uiKept, archiveFilename.str()));
}

Bool ArchiveFileSystem::isOurUITexture(const AsciiString& filename) const
{
	AsciiString path = filename;
	path.toLower();
	return m_uiTextures.find(path) != m_uiTextures.end();
}

void ArchiveFileSystem::getModUICopies(const AsciiString& directory, std::vector<AsciiString> &names) const
{
	AsciiString prefix = directory;
	prefix.toLower();
	prefix.concat('\\');
	for (std::map<AsciiString, AsciiString>::const_iterator it = m_modUICopies.begin(); it != m_modUICopies.end(); ++it)
	{
		if (it->first.startsWith(prefix.str()))
		{
			AsciiString name;
			name.format("%s%s", MOD_UI_COPY, it->first.str());
			names.push_back(name);
		}
	}
}

/// every texture our interface image sheets name, read from the sheets retail and the fork hold before
/// any mod is in the tree; the loader may ask for either the .tga or the .dds of one
void ArchiveFileSystem::collectUITextures()
{
	FilenameList sheets;
	getFileListInDirectory(AsciiString(""), AsciiString("Data\\INI\\MappedImages\\"), AsciiString("*.ini"), sheets, TRUE);
	for (FilenameListIter sheet = sheets.begin(); sheet != sheets.end(); ++sheet)
	{
		if (!FileSystem::isForkUIPath(*sheet))
			continue;
		File *file = openFile(sheet->str());
		std::string text(file->size(), '\0');
		file->read(&text[0], (Int)text.size());
		file->close();
		size_t at = 0;
		while (at < text.size())
		{
			size_t end = text.find('\n', at);
			if (end == std::string::npos)
				end = text.size();
			std::string line = text.substr(at, end - at);
			at = end + 1;
			line = line.substr(0, line.find(';'));
			const size_t equals = line.find('=');
			if (equals == std::string::npos)
				continue;
			AsciiString key = line.substr(0, equals).c_str();
			AsciiString value = line.substr(equals + 1).c_str();
			key.trim();
			value.trim();
			if (key.compareNoCase("Texture") != 0 || value.isEmpty())
				continue;
			value.toLower();
			AsciiString path;
			path.format("art\\textures\\%s", value.str());
			m_uiTextures.insert(path);
			if (path.endsWith(".tga"))
			{
				AsciiString dds = std::string(path.str(), path.getLength() - 4).c_str();
				dds.concat(".dds");
				m_uiTextures.insert(dds);
			}
		}
	}
	DEBUG_LOG(("ArchiveFileSystem::loadMods - %d interface textures stay ours\n", (Int)m_uiTextures.size()));
}

/// the fork's upscaled art goes by its file name, ReforgedTextures.big and the like
static Bool isReforgedArchiveName(const AsciiString &path)
{
	const char *slash = strrchr(path.str(), '\\');
	const char *forward = strrchr(path.str(), '/');
	const char *name = path.str();
	if (slash != NULL && slash + 1 > name)
		name = slash + 1;
	if (forward != NULL && forward + 1 > name)
		name = forward + 1;
	return strncasecmp(name, "Reforged", 8) == 0;
}

void ArchiveFileSystem::loadMods() {
	if (TheGlobalData->m_mods.empty())
		return;

	// retail < mods < the fork: the fork's Reforged*.big already in the tree go back on top afterwards.
	// ReforgedUI.big is EA's interface pages redrawn cell by cell, and a mod that ships its own copy of a
	// page has its own cameos at its own coordinates in it, so that one stays under the mods.
	std::vector<AsciiString> forkArt;
	for (ArchiveFileMap::const_iterator it = m_archiveFileMap.begin(); it != m_archiveFileMap.end(); ++it)
	{
		if (isReforgedArchiveName(it->first) && !it->first.endsWithNoCase("ReforgedUI.big"))
			forkArt.push_back(it->first);
	}

	collectUITextures();

	// -modfactions loads retail's factions and each mod's from their own files (FileSystem::setFocus)
	if (TheGlobalData->m_modFactions)
		keepModsApart(TheGlobalData->m_mods.size());

	// In command-line order, each with overwrite: a later -mod wins over an earlier one, and all of them
	// over the retail archives.  Under -modfactions a -modpatch goes over the full mod it was given
	// after, in that mod's own tree, or over retail's when it came first (-1).  Inside one folder the
	// first name wins, as in Win32BIGFileSystem's plain pass, so a mod's "!!" or "00_" archive is its
	// patch: the folder goes in descending name order, each with overwrite.
	std::vector< std::pair<Int, AsciiString> > layers;
	for (size_t i = 0; i < TheGlobalData->m_retailPatches.size(); ++i)
		layers.push_back(std::make_pair(-1, TheGlobalData->m_retailPatches[i]));
	for (size_t i = 0; i < TheGlobalData->m_mods.size(); ++i)
	{
		layers.push_back(std::make_pair((Int)i, TheGlobalData->m_mods[i]));
		for (size_t p = 0; i < TheGlobalData->m_modPatches.size() && p < TheGlobalData->m_modPatches[i].size(); ++p)
			layers.push_back(std::make_pair((Int)i, TheGlobalData->m_modPatches[i][p]));
	}
	for (size_t layer = 0; layer < layers.size(); ++layer)
	{
		const AsciiString &mod = layers[layer].second;
		FilenameList archives;
		if (mod.endsWith("\\") || mod.endsWith("/"))
			TheLocalFileSystem->getFileListInDirectory(mod, AsciiString(""), AsciiString("*.big"), archives, TRUE);
		else
			archives.insert(mod);

		for (FilenameList::reverse_iterator it = archives.rbegin(); it != archives.rend(); ++it)
		{
			if (isReforgedArchiveName(*it))
			{
				DEBUG_LOG(("ArchiveFileSystem::loadMods - %s is ignored, the fork's own Reforged archives win\n", it->str()));
				continue;
			}
			ArchiveFile *archiveFile = openArchiveFile(it->str());
			if (archiveFile == NULL)
			{
				DEBUG_LOG(("ArchiveFileSystem::loadMods - could not openArchiveFile(%s)\n", it->str()));
				continue;
			}
			DEBUG_LOG(("ArchiveFileSystem::loadMods - loading %s into the directory tree.\n", it->str()));
			m_archiveLayer[*it] = mod;
			addModArchive(layers[layer].first, archiveFile, *it);
		}
	}

	for (size_t i = 0; i < forkArt.size(); ++i)
	{
		DEBUG_LOG(("ArchiveFileSystem::loadMods - %s back over the mods\n", forkArt[i].str()));
		loadIntoDirectoryTree(m_archiveFileMap[forkArt[i]], forkArt[i], TRUE);
	}
}

AsciiString ArchiveFileSystem::modLayerOf(const AsciiString& archiveFilename) const
{
	std::map<AsciiString, AsciiString>::const_iterator it = m_archiveLayer.find(archiveFilename);
	return it == m_archiveLayer.end() ? AsciiString() : it->second;
}

void ArchiveFileSystem::keepModsApart(size_t modCount)
{
	m_modRoots.assign(modCount, ArchivedDirectoryInfo());
}

void ArchiveFileSystem::addModArchive(Int mod, ArchiveFile *archiveFile, const AsciiString& archiveFilename)
{
	m_archiveFileMap[archiveFilename] = archiveFile;
	// kept apart, a mod's files are reached only through its own focus or an "M<n>~" name: the merged
	// view stays retail's, so the menus, the lobby and every file a faction does not name are ours
	if (!m_modRoots.empty() && mod >= 0)
	{
		addToTree(m_modRoots[mod], archiveFile, archiveFilename, TRUE);
		m_archiveMod[archiveFilename] = mod;
		return;
	}
	loadIntoDirectoryTree(archiveFile, archiveFilename, TRUE);
}

const ArchivedDirectoryInfo &ArchiveFileSystem::focusedRoot() const
{
	return m_focus < 0 ? m_rootDirectory : m_modRoots[m_focus];
}

Bool ArchiveFileSystem::inFocus(const AsciiString& archiveFilename) const
{
	if (m_focus == FILE_FOCUS_ALL && m_modRoots.empty())
		return TRUE;
	std::map<AsciiString, Int>::const_iterator it = m_archiveMod.find(archiveFilename);
	const Int owner = it == m_archiveMod.end() ? FILE_FOCUS_RETAIL : it->second;
	return m_focus < 0 ? owner < 0 : owner == m_focus;
}

Bool ArchiveFileSystem::doesFileExist(const Char *filename) const
{
	AsciiString path = filename;
	path.toLower();
	AsciiString token;

	const ArchivedDirectoryInfo *dirInfo = &focusedRoot();

	path.nextToken(&token, "\\/");

	while (!token.find('.') || path.find('.'))
	{
		ArchivedDirectoryInfoMap::const_iterator tempiter = dirInfo->m_directories.find(token);
		if (tempiter != dirInfo->m_directories.end()) 
		{
			dirInfo = &tempiter->second;
			path.nextToken(&token, "\\/");
		}
		else
		{
			// the directory doesn't exist, so return false
			return FALSE;
		}
	}

	// token is the filename, and dirInfo is the directory that this file is in.
	if (dirInfo->m_files.find(token) == dirInfo->m_files.end()) {
		return FALSE;
	}
	return TRUE;
}

File * ArchiveFileSystem::openFile(const Char *filename, Int access /* = 0 */)
{
	if (strncmp(filename, MOD_UI_COPY, strlen(MOD_UI_COPY)) == 0)
	{
		const Char *path = filename + strlen(MOD_UI_COPY);
		return m_archiveFileMap[m_modUICopies[AsciiString(path)]]->openFile(path, access);
	}

	AsciiString archiveFilename;
	archiveFilename = getArchiveFilenameForFile(AsciiString(filename));

	if (archiveFilename.getLength() == 0) {
		return NULL;
	}

	return m_archiveFileMap[archiveFilename]->openFile(filename, access);
}

Bool ArchiveFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	if (fileInfo == NULL) {
		return FALSE;
	}

	if (filename.getLength() <= 0) {
		return FALSE;
	}

	AsciiString archiveFilename = getArchiveFilenameForFile(filename);
	ArchiveFileMap::const_iterator it = m_archiveFileMap.find(archiveFilename);
	if (it != m_archiveFileMap.end())
	{
		return it->second->getFileInfo(filename, fileInfo);
	}
	else
	{
		return FALSE;
	}
}

// Where two archives hold the same file, loadIntoDirectoryTree keeps whichever was loaded first.
// That is the right rule for content, but Zero Hour ships downscaled copies of a number of the base
// game's textures in TexturesZH.big, and those win over the full-size originals in the base game's
// Textures.big purely because of load order.  For the named pair of archives, and only inside the
// named directory, keep whichever copy is actually the larger file.
void ArchiveFileSystem::prioritizeLargerFiles(const AsciiString& directory, const char *inferiorArchive, const char *superiorArchive)
{
	// walk down to the directory
	AsciiString path = directory;
	path.toLower();
	AsciiString token;

	ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;
	while (path.nextToken(&token, "\\/"))
	{
		ArchivedDirectoryInfoMap::iterator dirIt = dirInfo->m_directories.find(token);
		if (dirIt == dirInfo->m_directories.end())
			return;

		dirInfo = &dirIt->second;
	}

	// find the archive that holds the bigger copies
	ArchiveFile *superior = NULL;
	AsciiString superiorName;
	for (ArchiveFileMap::iterator archiveIt = m_archiveFileMap.begin(); archiveIt != m_archiveFileMap.end(); ++archiveIt)
	{
		if (archiveIt->first.endsWithNoCase(superiorArchive))
		{
			superior = archiveIt->second;
			superiorName = archiveIt->first;
			break;
		}
	}

	if (superior == NULL)
		return;

	for (ArchivedFileLocationMap::iterator fileIt = dirInfo->m_files.begin(); fileIt != dirInfo->m_files.end(); ++fileIt)
	{
		if (!fileIt->second.endsWithNoCase(inferiorArchive))
			continue;

		ArchiveFileMap::iterator inferiorIt = m_archiveFileMap.find(fileIt->second);
		if (inferiorIt == m_archiveFileMap.end())
			continue;

		AsciiString filePath = directory;
		filePath.concat(fileIt->first);

		FileInfo inferiorInfo, superiorInfo;
		if (!inferiorIt->second->getFileInfo(filePath, &inferiorInfo))
			continue;
		if (!superior->getFileInfo(filePath, &superiorInfo))
			continue;

		if (superiorInfo.sizeLow > inferiorInfo.sizeLow)
		{
			DEBUG_LOG(("ArchiveFileSystem::prioritizeLargerFiles - %s (%d bytes) from %s wins over the %d byte copy in %s\n",
								 filePath.str(), superiorInfo.sizeLow, superiorName.str(), inferiorInfo.sizeLow, fileIt->second.str()));
			fileIt->second = superiorName;
		}
	}
}

AsciiString ArchiveFileSystem::getArchiveFilenameForFile(const AsciiString& filename) const
{
	return archiveInTree(focusedRoot(), filename);
}

File *ArchiveFileSystem::openModFile(Int mod, const AsciiString& filename, Int access)
{
	const AsciiString archive = archiveInTree(m_modRoots[mod], filename);
	return archive.isEmpty() ? NULL : m_archiveFileMap[archive]->openFile(filename.str(), access);
}

Bool ArchiveFileSystem::doesModFileExist(Int mod, const AsciiString& filename) const
{
	return archiveInTree(m_modRoots[mod], filename).isNotEmpty();
}

Bool ArchiveFileSystem::getModFileInfo(Int mod, const AsciiString& filename, FileInfo *fileInfo) const
{
	ArchiveFileMap::const_iterator it = m_archiveFileMap.find(archiveInTree(m_modRoots[mod], filename));
	return it != m_archiveFileMap.end() && it->second->getFileInfo(filename, fileInfo);
}

AsciiString ArchiveFileSystem::archiveInTree(const ArchivedDirectoryInfo &root, const AsciiString& filename)
{
	AsciiString path;
	path = filename;
	path.toLower();
	AsciiString token;
	AsciiString debugpath;

	const ArchivedDirectoryInfo *dirInfo = &root;

	path.nextToken(&token, "\\/");

	while (!token.find('.') || path.find('.')) {

		ArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.find(token);
		if (it != dirInfo->m_directories.end())
		{
			dirInfo = &it->second;
		}
		else
		{
			// the directory doesn't exist, so return NULL

			// dump the directories;
			//DEBUG_LOG(("directory %s not found in %s in archive file system\n", token.str(), debugpath.str()));
			//DEBUG_LOG(("directories in %s in archive file system are:\n", debugpath.str()));
			//ArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.begin();
			//while (it != dirInfo->m_directories.end()) {
			//	DEBUG_LOG(("\t%s\n", it->second.m_directoryName.str()));
			//	it++;
			//}
			//DEBUG_LOG(("end of directory list.\n"));
			return AsciiString::TheEmptyString;
		}

		debugpath.concat(token);
		debugpath.concat('\\');

		path.nextToken(&token, "\\/");
	}

	ArchivedFileLocationMap::const_iterator it = dirInfo->m_files.find(token);
	if (it != dirInfo->m_files.end())
	{
		return it->second;
	}
	else
	{
		return AsciiString::TheEmptyString;
	}

}

void ArchiveFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList &filenameList, Bool searchSubdirectories) const
{
	ArchiveFileMap::const_iterator it = m_archiveFileMap.begin();
	while (it != m_archiveFileMap.end()) {
		if (inFocus(it->first) && modLayerOf(it->first).isEmpty())
			it->second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, filenameList, searchSubdirectories);
		else if (inFocus(it->first))
		{
			FilenameList modded;
			it->second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, modded, searchSubdirectories);
			for (FilenameListIter name = modded.begin(); name != modded.end(); ++name)
			{
				if (!FileSystem::isMapPath(*name))
					filenameList.insert(*name);
			}
		}
		it++;
	}
}
