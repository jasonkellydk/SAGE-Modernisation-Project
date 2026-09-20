#include "SDL3Device/Common/SDL3LocalFileSystem.h"

#include "Common/GameMemory.h"
#include "SDL3Device/Common/SDL3LocalFile.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace
{
	using Path = std::filesystem::path;

	std::string normalizeSeparators(std::string value)
	{
		for (char &character : value)
		{
			if (character == '\\')
				character = '/';
		}
		return value;
	}

	std::string lowerString(const std::string &value)
	{
		std::string lower = value;
		for (char &character : lower)
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		return lower;
	}

	bool wildcardMatches(const std::string &value, const std::string &pattern)
	{
		// Win32's "*.*" means all files, including files without a dot.
		if (pattern == "*.*")
			return true;

		const std::string valueLower = lowerString(value);
		const std::string patternLower = lowerString(pattern);
		size_t valueIndex = 0;
		size_t patternIndex = 0;
		size_t starIndex = std::string::npos;
		size_t starMatch = 0;

		while (valueIndex < valueLower.size())
		{
			if (patternIndex < patternLower.size() &&
				(patternLower[patternIndex] == '?' ||
				 patternLower[patternIndex] == valueLower[valueIndex]))
			{
				++valueIndex;
				++patternIndex;
			}
			else if (patternIndex < patternLower.size() && patternLower[patternIndex] == '*')
			{
				starIndex = patternIndex++;
				starMatch = valueIndex;
			}
			else if (starIndex != std::string::npos)
			{
				patternIndex = starIndex + 1;
				valueIndex = ++starMatch;
			}
			else
			{
				return false;
			}
		}

		while (patternIndex < patternLower.size() && patternLower[patternIndex] == '*')
			++patternIndex;

		return patternIndex == patternLower.size();
	}

	Path resolveCaseInsensitive(const Path &input, bool allowMissing)
	{
		std::error_code error;
		if (std::filesystem::exists(input, error) && !error)
			return input;

#ifdef _WIN32
		// Windows already resolves the game's case-insensitive asset names.
		// A missing companion must not enumerate tens of thousands of texture
		// files again for every possible PBR suffix and search directory.
		return allowMissing ? input : Path();
#else

		Path current = input.root_path();
		if (current.empty())
		{
			current = std::filesystem::current_path(error);
			if (error)
				return Path();
		}

		bool missingTail = false;
		for (const Path &component : input.relative_path())
		{
			const std::string componentName = component.string();
			if (componentName.empty() || componentName == ".")
				continue;

			if (missingTail)
			{
				current /= component;
				continue;
			}

			Path exact = current / component;
			error.clear();
			if (std::filesystem::exists(exact, error) && !error)
			{
				current = exact;
				continue;
			}

			bool found = false;
			error.clear();
			for (const auto &entry : std::filesystem::directory_iterator(current, error))
			{
				if (error)
					break;
				if (lowerString(entry.path().filename().string()) == lowerString(componentName))
				{
					current = entry.path();
					found = true;
					break;
				}
			}

			if (found)
				continue;

			if (!allowMissing)
				return Path();

			current /= component;
			missingTail = true;
		}

		return current;
#endif
	}

	std::string joinLogicalPath(const std::string &first, const std::string &second)
	{
		std::string joined = normalizeSeparators(first);
		const std::string normalizedSecond = normalizeSeparators(second);
		if (joined.empty())
			return normalizedSecond;
		if (normalizedSecond.empty())
			return joined;
		if (joined.back() != '/')
			joined.push_back('/');
		joined += normalizedSecond;
		return joined;
	}

	std::string gamePath(const std::string &path)
	{
		std::string result = path;
#ifdef _WIN32
		for (char &character : result)
		{
			if (character == '/')
				character = '\\';
		}
#endif
		return result;
	}

	void enumerateDirectory(const Path &directory, const std::string &logicalDirectory,
		const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories)
	{
		std::error_code error;
		for (const auto &entry : std::filesystem::directory_iterator(directory, error))
		{
			if (error)
				break;

			const std::string name = entry.path().filename().string();
			if (name == "." || name == "..")
				continue;

			error.clear();
			const bool isDirectory = entry.is_directory(error);
			if (error)
				continue;

			if (!isDirectory && wildcardMatches(name, searchName))
			{
				const std::string result = gamePath(joinLogicalPath(logicalDirectory, name));
				filenameList.insert(AsciiString(result.c_str()));
			}

			if (searchSubdirectories && isDirectory)
			{
				error.clear();
				if (!entry.is_symlink(error) && !error)
				{
					enumerateDirectory(entry.path(), joinLogicalPath(logicalDirectory, name),
						searchName, filenameList, true);
				}
			}
		}
	}

	void setFileTimestamp(const std::filesystem::file_time_type &writeTime, FileInfo *fileInfo)
	{
		using namespace std::chrono;
		const auto fileClockNow = std::filesystem::file_time_type::clock::now();
		const auto delta = duration_cast<system_clock::duration>(writeTime - fileClockNow);
		const system_clock::time_point systemTime = system_clock::now() + delta;
		const auto windowsTicks = duration_cast<duration<Int64, std::ratio<1, 10000000>>>(
			systemTime.time_since_epoch()).count() + 116444736000000000LL;
		const UnsignedInt64 timestamp = windowsTicks > 0 ? static_cast<UnsignedInt64>(windowsTicks) : 0;
		fileInfo->timestampHigh = static_cast<Int>(timestamp >> 32);
		fileInfo->timestampLow = static_cast<Int>(timestamp & 0xffffffffu);
	}
}

SDL3LocalFileSystem::SDL3LocalFileSystem()
	: LocalFileSystem()
{
}

SDL3LocalFileSystem::~SDL3LocalFileSystem() = default;

void SDL3LocalFileSystem::init()
{
	const char *basePath = SDL_GetBasePath();
	if (basePath != nullptr && basePath[0] != '\0')
	{
		std::string base(basePath);
		std::error_code error;
		m_basePath = std::filesystem::absolute(Path(normalizeSeparators(base)), error);
		if (error)
			m_basePath.clear();
	}
	else
	{
		std::error_code error;
		m_basePath = std::filesystem::current_path(error);
		if (error)
			m_basePath.clear();
	}

	if (!m_basePath.empty())
		m_basePath = m_basePath.lexically_normal();

	DEBUG_LOG(("SDL3LocalFileSystem::init - base path is %s", m_basePath.string().c_str()));
}

void SDL3LocalFileSystem::reset()
{
}

void SDL3LocalFileSystem::update()
{
}

std::filesystem::path SDL3LocalFileSystem::resolvePath(const Char *filename, Int access) const
{
	if (filename == nullptr || filename[0] == '\0')
		return std::filesystem::path();

	Path path(normalizeSeparators(filename));
	if (path.is_relative())
	{
		Path basePath = m_basePath;
		if (basePath.empty())
		{
			std::error_code error;
			basePath = std::filesystem::current_path(error);
			if (error)
				return Path();
		}
		path = basePath / path;
	}

	path = path.lexically_normal();
	return resolveCaseInsensitive(path, (access & File::WRITE) != 0);
}

File *SDL3LocalFileSystem::openFile(const Char *filename, Int access, size_t bufferSize)
{
	const Path path = resolvePath(filename, access);
	if (path.empty())
		return nullptr;

	if ((access & File::WRITE) != 0)
	{
		const Path parent = path.parent_path();
		std::error_code error;
		if (!parent.empty() && !std::filesystem::create_directories(parent, error) && error)
			return nullptr;
	}

	SDL3LocalFile *file = newInstance(SDL3LocalFile);
	if (!file->open(path.string().c_str(), access, bufferSize))
	{
		deleteInstance(file);
		return nullptr;
	}

	file->deleteOnClose();
	return file;
}

Bool SDL3LocalFileSystem::doesFileExist(const Char *filename) const
{
	const Path path = resolvePath(filename, File::NONE);
	if (path.empty())
		return FALSE;

	std::error_code error;
	return std::filesystem::is_regular_file(path, error) && !error;
}

void SDL3LocalFileSystem::getFileListInDirectory(const AsciiString &currentDirectory,
	const AsciiString &originalDirectory, const AsciiString &searchName,
	FilenameList &filenameList, Bool searchSubdirectories) const
{
	const std::string logicalDirectory = joinLogicalPath(originalDirectory.str(), currentDirectory.str());
	const Path directory = logicalDirectory.empty()
		? resolvePath(".", File::NONE)
		: resolvePath(logicalDirectory.c_str(), File::NONE);
	if (directory.empty())
		return;

	enumerateDirectory(directory, logicalDirectory, searchName.str(), filenameList, searchSubdirectories);
}

Bool SDL3LocalFileSystem::getFileInfo(const AsciiString &filename, FileInfo *fileInfo) const
{
	if (fileInfo == nullptr)
		return FALSE;

	const Path path = resolvePath(filename.str(), File::NONE);
	if (path.empty())
		return FALSE;

	std::error_code error;
	const auto size = std::filesystem::file_size(path, error);
	if (error)
		return FALSE;

	const auto writeTime = std::filesystem::last_write_time(path, error);
	if (error)
		return FALSE;

	const UnsignedInt64 fileSize = static_cast<UnsignedInt64>(size);
	fileInfo->sizeHigh = static_cast<Int>(fileSize >> 32);
	fileInfo->sizeLow = static_cast<Int>(fileSize & 0xffffffffu);
	setFileTimestamp(writeTime, fileInfo);
	return TRUE;
}

Bool SDL3LocalFileSystem::createDirectory(AsciiString directory)
{
	if (directory.isEmpty())
		return FALSE;

	const Path path = resolvePath(directory.str(), File::WRITE);
	if (path.empty())
		return FALSE;

	std::error_code error;
	if (std::filesystem::is_directory(path, error) && !error)
		return TRUE;

	error.clear();
	return std::filesystem::create_directories(path, error) && !error;
}

AsciiString SDL3LocalFileSystem::normalizePath(const AsciiString &filePath) const
{
	if (filePath.isEmpty())
		return AsciiString::TheEmptyString;

	const Path path = resolvePath(filePath.str(), File::WRITE);
	if (path.empty())
		return AsciiString::TheEmptyString;

	return AsciiString(gamePath(path.string()).c_str());
}
