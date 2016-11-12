/*
 * Copyright 2016 Gary R. Van Sickle (grvs@users.sourceforge.net).
 *
 * This file is part of UniversalCodeGrep.
 *
 * UniversalCodeGrep is free software: you can redistribute it and/or modify it under the
 * terms of version 3 of the GNU General Public License as published by the Free
 * Software Foundation.
 *
 * UniversalCodeGrep is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * UniversalCodeGrep.  If not, see <http://www.gnu.org/licenses/>.
 */

/** @file */

#ifndef SRC_LIBEXT_FILEID_H_
#define SRC_LIBEXT_FILEID_H_

#include <config.h>

#include "hints.hpp"

#include <sys/stat.h> // For the stat types.
#include <fts.h>

#include <string>
#include <atomic>
#include <future/memory.hpp>
#include <future/shared_mutex.hpp>

#include "integer.hpp"
#include "filesystem.hpp"


// Forward declarations.
struct dirent;
class FileID;  // UnsynchronizedFileID keeps a ptr to its parent directory's FileID.

/// File Types enum.
enum FileType
{
	FT_UNINITIALIZED,
	FT_UNKNOWN,
	FT_REG,
	FT_DIR,
	FT_SYMLINK,
	FT_STAT_FAILED
};

/**
 * Only one may be specified.
 */
enum FileAccessMode
{
	FAM_RDONLY = O_RDONLY,//!< FAM_RDONLY
	FAM_RDWR = O_RDWR,    //!< FAM_RDWR
	FAM_SEARCH = O_SEARCH //!< FAM_SEARCH
};

/**
 * May be bitwise-or combined with FileAccesMode and each other.
 */
enum FileCreationFlag : int
{
	FCF_CLOEXEC = O_CLOEXEC,    //!< FCF_CLOEXEC
	FCF_CREAT = O_CREAT,		//!< FCF_CREAT
	FCF_DIRECTORY = O_DIRECTORY,//!< FCF_DIRECTORY
	FCF_NOCTTY = O_NOCTTY,      //!< FCF_NOCTTY
	FCF_NOFOLLOW = O_NOFOLLOW,  //!< FCF_NOFOLLOW
	FCF_NOATIME = O_NOATIME
};

/// Bitwise-or operator for FileCreationFlag.
/// @note Yeah, I didn't realize this was necessary for non-class enums in C++ either.  I've been writing too much C....
constexpr inline FileCreationFlag operator|(FileCreationFlag a, FileCreationFlag b)
{
	return static_cast<FileCreationFlag>(static_cast<std::underlying_type<FileCreationFlag>::type>(a)
			| static_cast<std::underlying_type<FileCreationFlag>::type>(b));
}

/**
 * The public interface to the underlying UnsynchronizedFileID instance.  This class adds thread safety.
 */
class FileID
{
private:

	/// We're bringing this mutex type in from the future: @see <future/shared_mutex.hpp>.
	/// Under C++17, this is really a std::shared_mutex.
	/// In C++14, it's a std::shared_timed_mutex.
	/// In C++11, it's a regular std::mutex.
	using MutexType = std::shared_mutex;
	  /// Likewise with this type.  In C++14+, it's a real std::shared_lock, else it's a std::unique_lock.
	using ReaderLock = std::shared_lock<MutexType>;
	using WriterLock = std::unique_lock<MutexType>;

#if 0 /// @todo For double-checked locking on m_path.
	mutable std::atomic<std::string*> m_atomic_path_ptr { nullptr };
#endif

	/// Mutex for locking in copy and move constructors and some operations.
	mutable MutexType m_mutex;

public:

	/// pImpl forward declaration.
	/// Not private: only because we want to do some static_assert() checks on it.
	class UnsynchronizedFileID;

	/// @name Tag types for selecting FileID() constructors when the given path is known to be relative or absolute.
	/// @{
	struct path_type_tag {};
	struct path_known_relative_tag : path_type_tag {};
	struct path_known_absolute_tag : path_type_tag {};
	struct path_known_cwd_tag : path_type_tag {}; 	/// Our equivalent for AT_FDCWD, the cwd of the process.
	static constexpr path_known_relative_tag path_known_relative = path_known_relative_tag();
	static constexpr path_known_absolute_tag path_known_absolute = path_known_absolute_tag();
	static constexpr path_known_cwd_tag path_known_cwd = path_known_cwd_tag();
	/// @}

	/// @name Constructors.
	/// @{
	FileID();
	FileID(const FileID& other);
	FileID(FileID&& other);

	/// Our equivalent for AT_FDCWD, the cwd of the process.
	/// Different in that each FileID created with this constructor holds a real file handle to the "." directory.
	FileID(path_known_cwd_tag tag);
	FileID(path_known_relative_tag tag, std::shared_ptr<FileID> at_dir_fileid, std::string basename,
			const struct stat *stat_buf = nullptr, FileType type = FT_UNINITIALIZED);
	FileID(path_known_relative_tag tag, std::shared_ptr<FileID> at_dir_fileid, std::string basename, FileType type = FT_UNINITIALIZED);
	FileID(path_known_absolute_tag tag, std::shared_ptr<FileID> at_dir_fileid, std::string pathname, FileType type = FT_UNINITIALIZED);
	FileID(std::shared_ptr<FileID> at_dir_fileid, std::string pathname);
	FileID(const FTSENT *ftsent, bool stat_info_known_valid);

	/// @}

	/// Copy assignment.
	FileID& operator=(const FileID& other);

	/// Move assignment.
	FileID& operator=(FileID&& other);

	/// Destructor.
	~FileID();

	const std::string& GetBasename() const noexcept;

	/**
	 * Returns the "full path" of the file.  May be absolute or relative to the root AT dir.
	 * @return A std::string object (not a reference) containing the file's path.
	 */
	std::string GetPath() const noexcept;

	/**
	 * This is essentially a possibly-deferred "open()" for this class.
	 *
	 * @post GetFileDescriptor() will return a FileDescriptor to the file with the given access mode and creation flags.
	 *
	 * @param fam
	 * @param fcf
	 */
	void SetFileDescriptorMode(FileAccessMode fam, FileCreationFlag fcf);

	/**
	 *
	 * @return
	 */
	FileDescriptor GetFileDescriptor();

	/**
	 * Return the type of file this FileID represents.  May involve stat()ing the file.
	 * @return
	 */
	FileType GetFileType() const noexcept;
	bool IsRegularFile() const noexcept { return GetFileType() == FT_REG; };
	bool IsDir() const noexcept { return GetFileType() == FT_DIR; };

	std::shared_ptr<FileID> GetAtDir() const noexcept;

	const std::string& GetAtDirRelativeBasename() const noexcept;

	bool IsStatInfoValid() const noexcept;

	off_t GetFileSize() const noexcept;

	blksize_t GetBlockSize() const noexcept;

	const dev_ino_pair GetUniqueFileIdentifier() const noexcept;

	dev_t GetDev() const noexcept;

	void SetDevIno(dev_t d, ino_t i) noexcept;

private:

	void SetStatInfo(const struct stat &stat_buf) noexcept;

	/// The pImpl.
	std::unique_ptr<UnsynchronizedFileID> m_pimpl;
};

static_assert(std::is_assignable<FileID, FileID>::value, "FileID must be assignable to itself.");
static_assert(std::is_copy_assignable<FileID>::value, "FileID must be copy assignable to itself.");
static_assert(std::is_move_assignable<FileID>::value, "FileID must be move assignable to itself.");

#endif /* SRC_LIBEXT_FILEID_H_ */
