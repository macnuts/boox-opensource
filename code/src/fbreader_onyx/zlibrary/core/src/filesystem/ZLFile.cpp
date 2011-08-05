/*
 * Copyright (C) 2004-2009 Geometer Plus <contact@geometerplus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <ZLStringUtil.h>
#include <ZLUnicodeUtil.h>

#include "ZLFile.h"
#include "ZLFSDir.h"
#include "ZLOutputStream.h"
#include "ZLInputStream.h"
#include "zip/ZLZip.h"
#include "tar/ZLTar.h"
#include "bzip2/ZLBzip2InputStream.h"
#include "ZLFSManager.h"
#include <QDebug>

std::map<std::string,weak_ptr<ZLInputStream> > ZLFile::ourPlainStreamCache;

ZLFile::ZLFile(const std::string &path, const std::string &aesKey)
  : myPath(path), aesKey(aesKey), myInfoIsFilled(false) {
	ZLFSManager::instance().normalize(myPath);
	{
		size_t index = ZLFSManager::instance().findLastFileNameDelimiter(myPath);
		if (index < myPath.length() - 1) {
			myNameWithExtension = myPath.substr(index + 1);
		} else {
			myNameWithExtension = myPath;
		}
	}
	myNameWithoutExtension = myNameWithExtension;

	std::map<std::string,ArchiveType> &forcedFiles = ZLFSManager::instance().myForcedFiles;
	std::map<std::string,ArchiveType>::iterator it = forcedFiles.find(myPath);
	if (it != forcedFiles.end()) {
		myArchiveType = it->second;
	} else {
		myArchiveType = NONE;
		std::string lowerCaseName = ZLUnicodeUtil::toLower(myNameWithoutExtension);

		if (ZLStringUtil::stringEndsWith(lowerCaseName, ".gz")) {
			myNameWithoutExtension = myNameWithoutExtension.substr(0, myNameWithoutExtension.length() - 3);
			lowerCaseName = lowerCaseName.substr(0, lowerCaseName.length() - 3);
			myArchiveType = (ArchiveType)(myArchiveType | GZIP);
		}
		if (ZLStringUtil::stringEndsWith(lowerCaseName, ".bz2")) {
			myNameWithoutExtension = myNameWithoutExtension.substr(0, myNameWithoutExtension.length() - 4);
			lowerCaseName = lowerCaseName.substr(0, lowerCaseName.length() - 4);
			myArchiveType = (ArchiveType)(myArchiveType | BZIP2);
		}
		if (ZLStringUtil::stringEndsWith(lowerCaseName, ".zip")) {
			myArchiveType = (ArchiveType)(myArchiveType | ZIP);
		} else if (ZLStringUtil::stringEndsWith(lowerCaseName, ".tar")) {
			myArchiveType = (ArchiveType)(myArchiveType | TAR);
		} else if (ZLStringUtil::stringEndsWith(lowerCaseName, ".tgz") ||
							 ZLStringUtil::stringEndsWith(lowerCaseName, ".ipk")) {
			//myNameWithoutExtension = myNameWithoutExtension.substr(0, myNameWithoutExtension.length() - 3) + "tar";
			myArchiveType = (ArchiveType)(myArchiveType | TAR | GZIP);
		}
	}

	int index = myNameWithoutExtension.rfind('.');
	if (index > 0) {
		myExtension = ZLUnicodeUtil::toLower(myNameWithoutExtension.substr(index + 1));
		myNameWithoutExtension = myNameWithoutExtension.substr(0, index);
	}
}

shared_ptr<ZLInputStream> ZLFile::envelopeCompressedStream(shared_ptr<ZLInputStream> &base) const {
	if (base != 0) {
		if (myArchiveType & GZIP) {
                  return shared_ptr<ZLInputStream>(new ZLGzipInputStream(base));
		}
		if (myArchiveType & BZIP2) {
                  return shared_ptr<ZLInputStream>(new ZLBzip2InputStream(base));
		}
	}
	return base;
}

shared_ptr<ZLInputStream> ZLFile::inputStream() const {
	shared_ptr<ZLInputStream> stream;

	int index = ZLFSManager::instance().findArchiveFileNameDelimiter(myPath);
	if (index == -1) {
          stream = ourPlainStreamCache[myPath].lock();
		if (!stream) {
			if (isDirectory()) {
                          return shared_ptr<ZLInputStream>();
			}
			stream.reset(ZLFSManager::instance().createPlainInputStream(myPath));
			stream = envelopeCompressedStream(stream);
			ourPlainStreamCache[myPath] = stream;
		}
	} else {
		ZLFile baseFile(myPath.substr(0, index));
		shared_ptr<ZLInputStream> base = baseFile.inputStream();
		if (base) {
		    std::string entryName = myPath.substr(index + 1);
			if (baseFile.myArchiveType & ZIP) {
			    ZLZipInputStream *zipInputStream = new ZLZipInputStream(base,
			            entryName);
			    if (ZLStringUtil::stringEndsWith(entryName, ".html")
			            && !ZLStringUtil::stringEndsWith(entryName, "cover.html"))
			    {
			        zipInputStream->setAESKey(aesKey);
			    }
			    stream.reset(zipInputStream);
			} else if (baseFile.myArchiveType & TAR) {
			    stream.reset(new ZLTarInputStream(base, entryName));
			}
		}
		stream = envelopeCompressedStream(stream);
	}

	return stream;
}

shared_ptr<ZLOutputStream> ZLFile::outputStream() const {
	if (isCompressed()) {
          return shared_ptr<ZLOutputStream>();
	}
	if (ZLFSManager::instance().findArchiveFileNameDelimiter(myPath) != -1) {
          return shared_ptr<ZLOutputStream>();
	}
	return shared_ptr<ZLOutputStream>(ZLFSManager::instance().createOutputStream(myPath));
}

shared_ptr<ZLDir> ZLFile::directory(bool createUnexisting) const {
	if (exists()) {
		if (isDirectory()) {
                  return shared_ptr<ZLDir>(ZLFSManager::instance().createPlainDirectory(myPath));
		} else if (myArchiveType & ZIP) {
                  return shared_ptr<ZLDir>(new ZLZipDir(myPath));
		} else if (myArchiveType & TAR) {
                  return shared_ptr<ZLDir>(new ZLTarDir(myPath));
		}
	} else if (createUnexisting) {
		myInfoIsFilled = false;
		return shared_ptr<ZLDir>(ZLFSManager::instance().createNewDirectory(myPath));
	}
	return shared_ptr<ZLDir>();
}

void ZLFile::fillInfo() const {
	myInfoIsFilled = true;

	int index = ZLFSManager::instance().findArchiveFileNameDelimiter(myPath);
	if (index == -1) {
		myInfo = ZLFSManager::instance().fileInfo(myPath);
	} else {
		const std::string archivePath = myPath.substr(0, index);
		ZLFile archive(archivePath);
		if (archive.exists()) {
			shared_ptr<ZLDir> dir = archive.directory();
			if (dir) {
				std::string itemName = myPath.substr(index + 1);
				myInfo = archive.myInfo;
				myInfo.IsDirectory = false;
				myInfo.Exists = false;
				std::vector<std::string> items;
				dir->collectFiles(items, true);
				for (std::vector<std::string>::const_iterator it = items.begin(); it != items.end(); ++it) {
					if (*it == itemName) {
						myInfo.Exists = true;
						break;
					}
				}
			} else {
				myInfo.Exists = false;
			}
		} else {
			myInfo.Exists = false;
		}
	}
}

bool ZLFile::remove() const {
	if (ZLFSManager::instance().removeFile(myPath)) {
		myInfoIsFilled = false;
		return true;
	} else {
		return false;
	}
}

void ZLFile::forceArchiveType(ArchiveType type) {
	if (myArchiveType != type) {
		myArchiveType = type;
		ZLFSManager::instance().myForcedFiles[myPath] = myArchiveType;
	}
}

std::string ZLFile::physicalFilePath() const {
	std::string path = myPath;
	int index;
	const ZLFSManager &manager = ZLFSManager::instance();
	while ((index = manager.findArchiveFileNameDelimiter(path)) != -1) {
		path = path.substr(0, index);
	}
	return path;
}

std::string ZLFile::resolvedPath() const {
	std::string physical = physicalFilePath();
	std::string postfix = myPath.substr(physical.length());
	return ZLFSManager::instance().resolveSymlink(physical) + postfix;
}

std::string ZLFile::fileNameToUtf8(const std::string &fileName) {
	return ZLFSManager::instance().convertFilenameToUtf8(fileName);
}

bool ZLFile::exists() const {
	if (!myInfoIsFilled) {
		fillInfo();
	}
	return myInfo.Exists;
}

size_t ZLFile::size() const {
	if (!myInfoIsFilled) {
		fillInfo();
	}
	return myInfo.Size;
}

bool ZLFile::isDirectory() const {
	if (!myInfoIsFilled) {
		fillInfo();
	}
	return myInfo.IsDirectory;
}
