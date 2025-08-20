//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2025 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include <cassert>
#pragma warning(disable : 4091)
#include <shlobj.h>
#include <io.h>
#include <stdio.h>
#include <tchar.h>

#include "Windows.hxx"
#include "FSNodeWINDOWS.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FSNodeWINDOWS::FSNodeWINDOWS(string_view p)
  : _path{p.length() > 0 ? BSPF::stringToWstring(p) : L"~"}  // Default to home directory
{
  // Expand '~' to the users 'home' directory
  if (_path[0] == '~')
    _path.replace(0, 1, HomeFinder::getHomePath());
  _pathString = BSPF::wstringToString(_path);
  setFlags();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FSNodeWINDOWS::setFlags()
{
  // Get absolute path
  static std::array<TCHAR, MAX_PATH> buf;
  if (GetFullPathName(_path.c_str(), MAX_PATH - 1, buf.data(), NULL))
    _path = buf.data();

  _displayName = lastPathComponent(BSPF::wstringToString(_path));
  _pathString = BSPF::wstringToString(_path);

  // Check whether it is a directory, and whether the file actually exists
  const DWORD fileAttribs = GetFileAttributes(_path.c_str());

  if (fileAttribs == INVALID_FILE_ATTRIBUTES)
  {
    _isDirectory = _isFile = false;
    return false;
  }
  else
  {
    _isDirectory = static_cast<bool>(fileAttribs & FILE_ATTRIBUTE_DIRECTORY);
    _isFile = !_isDirectory;

    // Add a trailing backslash, if necessary
    if (_isDirectory && _path.length() > 0 && _path.back() != FSNode::PATH_SEPARATOR)
      _path += FSNode::PATH_SEPARATOR;
  }
  _isPseudoRoot = false;

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string FSNodeWINDOWS::getShortPath() const
{
  // If the path starts with the home directory, replace it with '~'
  const std::wstring& home = HomeFinder::getHomePath();
  if (home != L"" && BSPF::startsWithIgnoreCase(BSPF::wstringToString(_path), BSPF::wstringToString(home)))
  {
    std::wstring path = L"~";
    const wchar_t* const offset = _path.c_str() + home.size();
    if (*offset != FSNode::PATH_SEPARATOR)
      path += FSNode::PATH_SEPARATOR;
    path += offset;
    return BSPF::wstringToString(path);
  }
  return _pathString;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
size_t FSNodeWINDOWS::getSize() const
{
  if (_size == 0 && _isFile)
  {
    struct _stat st;
    _size = _tstat(_path.c_str(), &st) == 0 ? st.st_size : 0;
  }
  return _size;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AbstractFSNodePtr FSNodeWINDOWS::getParent() const
{
  if (_isPseudoRoot)
    return nullptr;
  else if (_path.size() > 3)
    return make_unique<FSNodeWINDOWS>(stemPathComponent(BSPF::wstringToString(_path)));
  else
    return make_unique<FSNodeWINDOWS>();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FSNodeWINDOWS::getChildren(AbstractFSList& myList, ListMode mode) const
{
  if (!_isPseudoRoot) [[likely]]
  {
    // Files enumeration
    WIN32_FIND_DATA desc{};
    HANDLE handle = FindFirstFileEx((_path + L"*").c_str(), FindExInfoBasic,
        &desc, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE)
      return false;

    do {
      const wchar_t* const name = desc.cFileName;

      // Skip files starting with '.' (we assume empty filenames never occur)
      if (name[0] == '.')
        continue;

      const bool isDirectory = static_cast<bool>(desc.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
      const bool isFile = !isDirectory;

      if ((isFile && mode == FSNode::ListMode::DirectoriesOnly) ||
          (isDirectory && mode == FSNode::ListMode::FilesOnly))
        continue;

      FSNodeWINDOWS entry;
      entry._isDirectory = isDirectory;
      entry._isFile = isFile;
      entry._displayName = BSPF::wstringToString(std::wstring(name));
      entry._path = _path;
      entry._path += name;
      entry._pathString = BSPF::wstringToString(entry._path);
      if (entry._isDirectory)
        entry._path += FSNode::PATH_SEPARATOR;
      entry._isPseudoRoot = false;
      entry._size = desc.nFileSizeHigh * (static_cast<size_t>(MAXDWORD) + 1) + desc.nFileSizeLow;

      myList.emplace_back(make_unique<FSNodeWINDOWS>(entry));
    } while (FindNextFile(handle, &desc));

    FindClose(handle);
  }
  else
  {
    // Drives enumeration
    static std::array<TCHAR, 100> drive_buffer;
    GetLogicalDriveStrings(static_cast<DWORD>(drive_buffer.size()), drive_buffer.data());

    static char drive_name[2] = { '\0', '\0' };
    for (TCHAR* current_drive = drive_buffer.data(); *current_drive;
         current_drive += _tcslen(current_drive) + 1)
    {
      FSNodeWINDOWS entry;

      drive_name[0] = current_drive[0];
      entry._displayName = drive_name;
      entry._isDirectory = true;
      entry._isFile = false;
      entry._isPseudoRoot = false;
      entry._path = current_drive;
      myList.emplace_back(make_unique<FSNodeWINDOWS>(entry));
    }
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FSNodeWINDOWS::exists() const
{
  return _waccess(_path.c_str(), 0 /*F_OK*/) == 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FSNodeWINDOWS::isReadable() const
{
  return _waccess(_path.c_str(), 4 /*R_OK*/) == 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FSNodeWINDOWS::isWritable() const
{
  return _waccess(_path.c_str(), 2 /*W_OK*/) == 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FSNodeWINDOWS::makeDir()
{
  if (!_isPseudoRoot && CreateDirectory(_path.c_str(), NULL) != 0)
    return setFlags();

  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool FSNodeWINDOWS::rename(string_view newfile)
{
  if (!_isPseudoRoot && MoveFile(_path.c_str(), BSPF::stringToWstring(newfile).c_str()) != 0)
  {
    _path = BSPF::stringToWstring(newfile);
    if (_path[0] == '~')
      _path.replace(0, 1, HomeFinder::getHomePath());

    _pathString = newfile;
    return setFlags();
  }

  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
size_t FSNodeWINDOWS::read(ByteBuffer& buffer, size_t size) const
{
    size_t sizeRead = 0;

    // File must actually exist
    if (!(exists() && isReadable()))
        throw runtime_error("File not found/readable");

    std::ifstream in(_path, std::ios::binary);
    if (in)
    {
        in.seekg(0, std::ios::end);
        sizeRead = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        if (sizeRead == 0)
            throw runtime_error("Zero-byte file");
        else if (size > 0)  // If a requested size to read is provided, honour it
            sizeRead = std::min(sizeRead, size);

        buffer = make_unique<uInt8[]>(sizeRead);
        in.read(reinterpret_cast<char*>(buffer.get()), sizeRead);
    }
    else
        throw runtime_error("File open/read error");

    return sizeRead;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
size_t FSNodeWINDOWS::read(stringstream& buffer) const
{
    size_t sizeRead = 0;

    // File must actually exist
    if (!(exists() && isReadable()))
        throw runtime_error("File not found/readable");

    std::ifstream in(_path);
    if (in)
    {
        in.seekg(0, std::ios::end);
        sizeRead = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        if (sizeRead == 0)
            throw runtime_error("Zero-byte file");

        buffer << in.rdbuf();
    }
    else
        throw runtime_error("File open/read error");

    return sizeRead;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
size_t FSNodeWINDOWS::write(const ByteBuffer& buffer, size_t size) const
{
    size_t sizeWritten = 0;


    std::ofstream out(_path, std::ios::binary);
    if (out)
    {
        out.write(reinterpret_cast<const char*>(buffer.get()), size);

        out.seekp(0, std::ios::end);
        sizeWritten = static_cast<size_t>(out.tellp());
    }
    else
        throw runtime_error("File open/write error");

    return sizeWritten;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
size_t FSNodeWINDOWS::write(const stringstream& buffer) const
{
    size_t sizeWritten = 0;

 
    std::ofstream out(_path);
    if (out)
    {
        out << buffer.rdbuf();

        out.seekp(0, std::ios::end);
        sizeWritten = static_cast<size_t>(out.tellp());
    }
    else
        throw runtime_error("File open/write error");

    return sizeWritten;
}