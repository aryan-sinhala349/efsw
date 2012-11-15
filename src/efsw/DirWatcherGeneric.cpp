#include <efsw/DirWatcherGeneric.hpp>
#include <efsw/FileSystem.hpp>
#include <efsw/Debug.hpp>

namespace efsw {

DirWatcherGeneric::DirWatcherGeneric( DirWatcherGeneric * parent, WatcherGeneric * ws, const std::string& directory, bool recursive ) :
	Parent( parent ),
	Watch( ws ),
	Recursive( recursive ),
	Deleted( false )
{
	resetDirectory( directory );

	DirSnap.scan();
}

DirWatcherGeneric::~DirWatcherGeneric()
{
	/// If the directory was deleted mark the files as deleted
	if ( Deleted )
	{
		DirectorySnapshotDiff Diff = DirSnap.scan();

		if ( !DirSnap.exists() )
		{
			FileInfoList::iterator it;

			DiffIterator( FilesDeleted )
			{
				handleAction( (*it).Filepath, Actions::Delete );
			}

			DiffIterator( DirsDeleted )
			{
				handleAction( (*it).Filepath, Actions::Delete );
			}
		}
	}

	DirWatchMap::iterator it = Directories.begin();

	for ( ; it != Directories.end(); it++ )
	{
		if ( Deleted )
		{
			/// If the directory was deleted, mark the flag for file deletion
			it->second->Deleted = true;
		}

		efSAFE_DELETE( it->second );
	}
}

void DirWatcherGeneric::resetDirectory( std::string directory )
{
	std::string dir( directory );

	/// Is this a recursive watch?
	if ( Watch->Directory != directory )
	{
		if ( !( directory.size() && ( directory.at(0) == FileSystem::getOSSlash() || directory.at( directory.size() - 1 ) == FileSystem::getOSSlash() ) ) )
		{
			/// Get the real directory
			if ( NULL != Parent )
			{
				dir = Parent->DirSnap.DirectoryInfo.Filepath + directory;
			}
			else
			{
				efDEBUG( "resetDirectory(): Parent is NULL. Fatal error." );
			}
		}
	}

	DirSnap.setDirectoryInfo( dir );
}

void DirWatcherGeneric::handleAction( const std::string &filename, unsigned long action, std::string oldFilename)
{
	Watch->Listener->handleFileAction( Watch->ID, DirSnap.DirectoryInfo.Filepath, FileSystem::fileNameFromPath( filename ), (Action)action, oldFilename );
}

void DirWatcherGeneric::addChilds()
{
	if ( Recursive )
	{
		/// Create the subdirectories watchers
		std::string dir;

		for ( FileInfoMap::iterator it = DirSnap.Files.begin(); it != DirSnap.Files.end(); it++ )
		{
			if ( it->second.isDirectory() )
			{	
				/// Check if the directory is a symbolic link
				std::string curPath;
				std::string link( FileSystem::getLinkRealPath( it->second.Filepath, curPath ) );

				dir = it->first;

				if ( "" != link )
				{
					/// If it's a symlink check if the realpath exists as a watcher, or
					/// if the path is outside the current dir
					if ( Watch->WatcherImpl->pathInWatches( link ) || Watch->pathInWatches( link ) || !Watch->WatcherImpl->linkAllowed( curPath, link ) )
					{
						continue;
					}
					else
					{
						dir = link;
					}
				}
				else
				{
					if ( Watch->pathInWatches( dir ) || Watch->WatcherImpl->pathInWatches( dir ) )
					{
						continue;
					}
				}

				/** TODO Check if the watch directory was added succesfully */
				Directories[ dir ] = new DirWatcherGeneric( this, Watch, dir, Recursive );

				Directories[ dir ]->addChilds();
			}
		}
	}
}

void DirWatcherGeneric::watch()
{
	DirectorySnapshotDiff Diff = DirSnap.scan();

	if ( Diff.changed() )
	{
		FileInfoList::iterator it;
		MovedList::iterator mit;

		/// Files
		DiffIterator( FilesCreated )
		{
			handleAction( (*it).Filepath, Actions::Add );
		}

		DiffIterator( FilesModified )
		{
			handleAction( (*it).Filepath, Actions::Modified );
		}

		DiffIterator( FilesDeleted )
		{
			handleAction( (*it).Filepath, Actions::Delete );
		}

		DiffMovedIterator( FilesMoved )
		{
			handleAction( (*mit).second.Filepath, Actions::Moved, (*mit).first );
		}

		/// Directories
		DiffIterator( DirsCreated )
		{
			handleAction( (*it).Filepath, Actions::Add );
			createDirectory( (*it).Filepath );
		}

		DiffIterator( DirsModified )
		{
			handleAction( (*it).Filepath, Actions::Modified );
		}

		DiffIterator( DirsDeleted )
		{
			handleAction( (*it).Filepath, Actions::Delete );
			removeDirectory( (*it).Filepath );
		}

		DiffMovedIterator( DirsMoved )
		{
			handleAction( (*mit).second.Filepath, Actions::Moved, (*mit).first );
			moveDirectory( (*mit).first, (*mit).second.Filepath );
		}
	}

	/// Process the subdirectories looking for changes
	for ( DirWatchMap::iterator dit = Directories.begin(); dit != Directories.end(); dit++ )
	{
		/// Just watch
		dit->second->watch();
	}
}

DirWatcherGeneric * DirWatcherGeneric::createDirectory( std::string newdir )
{
	FileSystem::dirRemoveSlashAtEnd( newdir );
	newdir = FileSystem::fileNameFromPath( newdir );

	DirWatcherGeneric * dw = NULL;

	/// Check if the directory is a symbolic link
	std::string dir( DirSnap.DirectoryInfo.Filepath + newdir );

	FileSystem::dirAddSlashAtEnd( dir );

	std::string curPath;
	std::string link( FileSystem::getLinkRealPath( dir, curPath ) );
	bool skip = false;

	if ( "" != link )
	{
		/// If it's a symlink check if the realpath exists as a watcher, or
		/// if the path is outside the current dir
		if ( Watch->WatcherImpl->pathInWatches( link ) || Watch->pathInWatches( link ) || !Watch->WatcherImpl->linkAllowed( curPath, link ) )
		{
			skip = true;
		}
		else
		{
			dir = link;
		}
	}
	else
	{
		if ( Watch->pathInWatches( dir ) || Watch->WatcherImpl->pathInWatches( dir ) )
		{
			skip = true;
		}
	}

	/** @TODO: Check if the watch directory was added succesfully */
	if ( !skip )
	{
		/// Creates the new directory watcher of the subfolder and check for new files
		dw = new DirWatcherGeneric( this, Watch, dir, Recursive );
		dw->watch();

		/// Add it to the list of directories
		Directories[ newdir ] = dw;
	}

	return dw;
}

void DirWatcherGeneric::removeDirectory( std::string dir )
{
	FileSystem::dirRemoveSlashAtEnd( dir );
	dir = FileSystem::fileNameFromPath( dir );

	DirWatcherGeneric * dw			= NULL;
	DirWatchMap::iterator dit;

	/// Folder deleted

	/// Search the folder, it should exists
	dit = Directories.find( dir );

	if ( dit != Directories.end() )
	{
		dw = dit->second;

		/// Flag it as deleted so it fire the event for every file inside deleted
		dw->Deleted = true;

		/// Delete the DirWatcherGeneric
		efSAFE_DELETE( dw );

		/// Remove the directory from the map
		Directories.erase( dit->first );
	}
}

void DirWatcherGeneric::moveDirectory( std::string oldDir, std::string newDir )
{
	FileSystem::dirRemoveSlashAtEnd( oldDir );
	oldDir = FileSystem::fileNameFromPath( oldDir );

	FileSystem::dirRemoveSlashAtEnd( newDir );
	newDir = FileSystem::fileNameFromPath( newDir );

	DirWatcherGeneric * dw			= NULL;
	DirWatchMap::iterator dit;

	/// Directory existed?
	dit = Directories.find( oldDir );

	if ( dit != Directories.end() )
	{
		dw = dit->second;

		/// Remove the directory from the map
		Directories.erase( dit->first );

		Directories[ newDir ] = dw;

		dw->resetDirectory( newDir );
	}
}

bool DirWatcherGeneric::pathInWatches( std::string path )
{
	if ( DirSnap.DirectoryInfo.Filepath == path )
	{
		return true;
	}

	for ( DirWatchMap::iterator it = Directories.begin(); it != Directories.end(); it++ )
	{
		 if ( it->second->pathInWatches( path ) )
		 {
			 return true;
		 }
	}

	return false;
}

} 
