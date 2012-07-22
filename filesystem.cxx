#include "filesystem.h"

#include <cassert>
#include <unistd.h>
#include <cstring>

#ifdef WINDOWS
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#endif

#include "exception.h"

// My policy on case insensitivity on Windows: pretend it doesn't exist.  If two paths with different cases are compared, subsetted, whatever, they will be considered inequivalent.

static bool IsAbsolute(String const &RawPath)
{
#ifdef WINDOWS
	if ((RawPath.size() < 2) || (RawPath[1] != ':'))
		return false;
#else
	if (RawPath[0] != '/')
		return false;
#endif
	return true;
}

class PathStringIterator
{
	public:
		PathStringIterator(String const &PathString) :
			PathString(PathString), PreviousMarker(0), Marker(0)
			{}

		bool FindNext(void)
		{
			if (Marker == String::npos) return false;

			PreviousMarker = Marker;

			Marker = PathString.find(u8"/", PreviousMarker);
#ifdef WINDOWS
			if (Marker == String::npos)
				Marker = PathString.find(u8"\\", PreviousMarker);
#endif
			if (Marker != String::npos) Marker += 1;
			return true;
		}

		String Read(void) const
		{
			assert(Marker > 0); // FindNext must be called first
			if (Marker - 1 - PreviousMarker == 0) return String();
			return PathString.substr(PreviousMarker, Marker - 1 - PreviousMarker);
		}

	private:
		String const &PathString;
		size_t PreviousMarker, Marker;
};

Path::Path(String const &Absolute)
{
	if (Absolute.empty())
		throw Error::Construction("Absolute paths must not be empty.");

	if (!IsAbsolute(Absolute))
		throw Error::Construction("Base paths must be constructed with absolute paths.");
	
	PathStringIterator AbsoluteIterator(Absolute);

	while (AbsoluteIterator.FindNext())
	{
		String Part = AbsoluteIterator.Read();
		if (Part == u8"")
			continue;
		else if (Part == u8"..")
		{
			if (Parts.empty()) throw Error::Construction(".. directory specified at root level!");
#ifdef WINDOWS
			if (Parts.size() == 1) throw Error::Construction(".. directory specified at root level!");
#endif
			Parts.pop_back();
		}
		else if (Part == u8".")
			continue;
		else Parts.push_back(Part);
	}
}

Path::Path(Path const &Other) : Parts(Other.Parts) {}

Path::~Path(void) {}

String Path::AsAbsoluteString(void) const
{
	StringStream Out;
#ifdef WINDOWS
	bool First = true;
#else
	if (Parts.empty())
		Out << u8"/";
#endif

	for (auto Part : Parts)
	{
#ifdef WINDOWS
		if (First) First = false;
		else Out << u8"/";
		Out << Part;
#else
		Out << u8"/" << Part;
#endif
	}
	return Out.str();
}

Path::operator char const *(void) const
	{ return AsAbsoluteString().c_str(); }

Path::operator String(void) const
	{ return AsAbsoluteString(); }

String Path::AsRelativeString(DirectoryPath const &From) const
{
	StringStream Out;
	bool First = true;
	auto AppendPart = [&Out, &First](String const &Part)
	{
		if (First) First = false;
		else Out << u8"/";
		Out << Part;
	};

	PartCollection::const_iterator HerePart = Parts.begin(), FromPart = From.Parts.begin();
	FindCommonRoot(From.Parts, HerePart, FromPart);
	for (; FromPart != From.Parts.end(); FromPart++)
		AppendPart(u8"..");
	for (; HerePart != Parts.end(); HerePart++)
		AppendPart(*HerePart);

	return Out.str();
}

bool Path::IsRoot(void) const
{
#ifdef WINDOWS
	assert(!Parts.empty());
	return Parts.size() <= 1;
#else
	return Parts.empty();
#endif
}

unsigned int Path::Depth(void) const
{
#ifdef WINDOWS
	assert(!Parts.empty());
	return Parts.size() - 1;
#else
	return Parts.size();
#endif
}

Path::Path(PartCollection const &Parts) : Parts(Parts) {}

Path::PartCollection Path::FindCommonRoot(PartCollection const &OtherParts, PartCollection::const_iterator &LocalDivergence, PartCollection::const_iterator &OtherDivergence) const
{
	PartCollection::const_iterator &LocalPart = LocalDivergence, &OtherPart = OtherDivergence;
	PartCollection Out;
	while ((LocalPart != Parts.end()) && (OtherPart != OtherParts.end()))
	{
		if (*LocalPart != *OtherPart) break;
		Out.push_back(*LocalPart);
		++LocalPart;
		++OtherPart;
	}
	return std::move(Out);
}

FilePath::FilePath(String const &Absolute) : Path(Absolute) {}
		
FilePath FilePath::Qualify(String const &RawPath)
{
	if (IsAbsolute(RawPath))
		return FilePath(RawPath);
	return FilePath(LocateWorkingDirectory().AsAbsoluteString() + "/" + RawPath);
}

String FilePath::File(void) const { return Parts.back(); }

DirectoryPath FilePath::Directory(void) const { return DirectoryPath(PartCollection(Parts.begin(), --Parts.end())); }

bool FilePath::Exists(void) const
{
#ifdef WINDOWS
        return GetFileAttributesW(reinterpret_cast<wchar_t const *>(AsNativeString("\\\\?\\" + AsAbsoluteString()).c_str())) != 0xFFFFFFFF;
#else
        return access(AsAbsoluteString().c_str(), F_OK) == 0;
#endif
}

FileInput &&FilePath::Read(void) const 
{
#ifdef WINDOWS
	FileInput Out;
	Out.open(reinterpret_cast<wchar_t const *>(AsNativeString(*this).c_str()));
	return std::move(Out);
#else
	return std::move(FileInput(AsNativeString(*this).c_str())); 
#endif
}

FileOutput &&FilePath::Write(bool Append, bool Truncate) const
{
	return std::move(FileOutput(AsNativeString(*this),
		FileOutput::out | (Append ? FileOutput::app : FileOutput::openmode(0)) | (Truncate ? FileOutput::trunc : FileOutput::openmode(0))));
}

FilePath::operator FileInput&&(void) const { return Read(); }

FilePath::operator FileOutput&&(void) const { return Write(); }

bool FilePath::Delete(void) const
{
#ifdef WINDOWS
	return _wunlink(AsNativeString(AsAbsoluteString()).c_str()) == 0;
#else
	return unlink(AsAbsoluteString().c_str()) == 0;
#endif
}

FilePath::FilePath(Path::PartCollection const &Parts, String const &Filename) : Path(Parts)
	{ this->Parts.push_back(Filename); }

DirectoryPath::DirectoryPath(void) : Path(Path::PartCollection()) {}

DirectoryPath DirectoryPath::Qualify(String const &RawPath)
{
	if (IsAbsolute(RawPath))
		return DirectoryPath(RawPath);
	return DirectoryPath(LocateWorkingDirectory().AsAbsoluteString() + "/" + RawPath);
}

DirectoryPath::DirectoryPath(String const &Absolute) : Path(Absolute) {}

bool DirectoryPath::Create(bool EnsureAncestors) const
{
	auto MakeSingleDirectory = [](DirectoryPath const &Ancestor) -> bool
	{
#ifdef WINDOWS
		int Result = _wmkdir(Ancestor);
#else
		int Result = mkdir(Ancestor, 777);
#endif
		if (Result == -1 && errno != EEXIST)
			return false;
		return true;
	};

	if (EnsureAncestors)
	{
		DirectoryPath Ancestor;
		for (Path::PartCollection::const_iterator CurrentPart = Parts.begin(); CurrentPart != Parts.end(); CurrentPart++)
		{
			Ancestor.Enter(*CurrentPart);
			if (!MakeSingleDirectory(Ancestor))
				return false;
		}
		return true;
	}
	else
	{
		return MakeSingleDirectory(*this);
	}
}

DirectoryPath &DirectoryPath::Exit(void)
{
	assert(!IsRoot());
	Parts.pop_back();
	return *this;
}

DirectoryPath &DirectoryPath::Enter(String const &Directory)
{
	Parts.push_back(Directory);
	return *this;
}

FilePath DirectoryPath::Select(String const &File) const
	{ return FilePath(Parts, File); }

static void ProcessDirectoryContents(String const &DirectoryName, std::function<void(String const &Element, bool IsFile)> Process)
{
#ifdef WINDOWS
	WIN32_FIND_DATA ElementInfo;
	HANDLE DirectoryResource;
	DirectoryResource = FindFirstFile(AsNativeString(DirectoryName).c_str(), &ElementInfo);
	if (DirectoryResource == INVALID_HANDLE_VALUE) return;
	do Process(AsString(NativeString(ElementInfo.cFileName)), ElementInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
		while (FindNextFile(DirectoryResource, &ElementInfo) != 0);
	FindClose(DirectorResource);
#else
	DIR *DirectoryResource = opendir(DirectoryName.c_str());
	if (DirectoryResource == nullptr) return;

	dirent *ElementInfo;
	while ((ElementInfo = readdir(DirectoryResource)) != nullptr)
	{
		String ElementName(ElementInfo->d_name);
		if ((ElementName == ".") || (ElementName == "..")) continue;
		Process(std::move(ElementName), ElementInfo->d_type != DT_DIR);
	}

	closedir(DirectoryResource);
#endif
}

std::list<String> DirectoryPath::ListFiles(void) const
{
	std::list<String> Out;
	ProcessDirectoryContents(*this, [&](String const &Element, bool IsFile) { if (IsFile) Out.push_back(Element); });
	return std::move(Out);
}

std::list<String> DirectoryPath::ListDirectories(void) const
{
	std::list<String> Out;
	ProcessDirectoryContents(*this, [&](String const &Element, bool IsFile) { if (!IsFile) Out.push_back(Element); });
	return std::move(Out);
}

void DirectoryPath::Walk(std::function<void(FilePath const &File)> const &Process) const
{
	std::list<String> Transitions;
	DirectoryPath Marker(*this);

	auto ProcessFiles = [&]()
		{ for (auto &File : Marker.ListFiles()) Process(Marker.Select(File)); };

	auto QueueNewTransitions = [&]()
	{
		// Add new transitions from directories in current directory
		std::list<String> Directories = Marker.ListDirectories();
		Transitions.insert(Transitions.begin(), Directories.begin(), Directories.end());
	};

	// Perform a depth first exploration of the filesystem subtree.  Files are processed before directories.
	ProcessFiles();
	QueueNewTransitions();

	while (!Transitions.empty())
	{
		String NextTransition = Transitions.front();
		Transitions.pop_front();

		// If there's an empty transition, move back up
		if (NextTransition.empty())
		{
			Marker.Exit();
			continue;
		}

		// Otherwise, enter the next directory and process it.
		Marker.Enter(NextTransition);
		Transitions.push_front("");
		QueueNewTransitions();
		ProcessFiles();
	}
}

DirectoryPath DirectoryPath::FindCommonRoot(DirectoryPath const &Other) const
{
	Path::PartCollection::const_iterator Part(Parts.begin()), OtherPart(Other.Parts.begin());
	return DirectoryPath(Path::FindCommonRoot(Other.Parts, Part, OtherPart));
}

DirectoryPath::DirectoryPath(Path::PartCollection const &Parts) : Path(Parts) {}

DirectoryPath LocateWorkingDirectory(void)
{
	 char WorkingDirectoryBuffer[FILENAME_MAX];
	 if (!getcwd(WorkingDirectoryBuffer, sizeof(WorkingDirectoryBuffer)))
		 throw Error::System("Couldn't obtain working directory!");
	 return DirectoryPath(WorkingDirectoryBuffer);
}

static String GetUserConfigDirectory(void)
{
#ifdef WINDOWS
	PWSTR PathResult;
	HRESULT Result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &PathResult);
	if (Result != S_OK)
		throw Error::System("Couldn't find user config directory!  Received error " + AsString(Result));
	String Out(PathResult);
	CoTaskMemFree(PathResult);
	return std::move(Out);
#else
	char *HomePath = getenv("XDG_CONFIG_HOME");
	if (HomePath == nullptr)
		HomePath = getenv("HOME");
	if (HomePath == nullptr)
		throw Error::System("User's local config directory and home directory are undefined!");
	return String(HomePath);
#endif
}

static String GetGlobalConfigDirectory(void)
{
#ifdef WINDOWS
	PWSTR PathResult;
	HRESULT Result = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &PathResult);
	if (Result != S_OK)
		throw Error::System("Couldn't find global config directory!  Received error " + AsString(Result));
	String Out(PathResult);
	CoTaskMemFree(PathResult);
	return std::move(Out);
#else
	return String(u8"/etc");
#endif
}

FilePath LocateUserConfigFile(String const &Filename)
	{ return DirectoryPath(GetUserConfigDirectory()).Select(Filename); }

FilePath LocateUserConfigFile(String const &Project, String const &Filename)
	{ return DirectoryPath(GetUserConfigDirectory()).Enter(Project).Select(Filename); }

FilePath LocateGlobalConfigFile(String const &Filename)
	{ return DirectoryPath(GetGlobalConfigDirectory()).Select(Filename); }

FilePath LocateGlobalConfigFile(String const &Project, String const &Filename)
	{ return DirectoryPath(GetGlobalConfigDirectory()).Enter(Project).Select(Filename); }

DirectoryPath LocateDocumentDirectory(void)
{
#ifdef WINDOWS
	PWSTR PathResult;
	HRESULT Result = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &PathResult);
	if (Result != S_OK)
		throw Error::System("Couldn't find user document directory!  Received error " + AsString(Result));
	DirectoryPath Out(PathResult);
	CoTaskMemFree(PathResult);
	return std::move(Out);
#else
	char *HomePath = getenv("HOME");
	if (HomePath == nullptr)
		throw Error::System("User's home directory is undefined!");
	return DirectoryPath(HomePath);
#endif
}

DirectoryPath LocateDocumentDirectory(String const &Project)
	{ return LocateDocumentDirectory().Enter(Project); }

DirectoryPath LocateTemporaryDirectory(void)
{
#ifdef WINDOWS
	TCHAR TemporaryPath[MAX_PATH];
	int Result = GetTempPath(MAX_PATH, TemporaryPath);
	if (Result == 0)
		throw Error::System("Could not find the temporary file directory!");
	return DirectoryPath(TemporaryPath);
#else
	char const *TemporaryPath = getenv("TMPDIR");
	if (TemporaryPath == nullptr) TemporaryPath = getenv("P_tmpdir");
	if (TemporaryPath == nullptr) TemporaryPath = "/tmp";
	return DirectoryPath(TemporaryPath);
#endif
}

FilePath CreateTemporaryFile(DirectoryPath const &TempDirectory, FileOutput &Output)
{
	String Template = TempDirectory.AsAbsoluteString() + "/XXXXXX";
	std::vector<char> Filename(Template.size() + 1);
	std::copy(Template.begin(), Template.end(), Filename.begin());
	Filename[Template.size()] = '\0';
	int Result = mkstemp(&Filename[0]);
	if (Result == -1)
		throw Error::System("Failed to locate temporary file in " + TempDirectory.AsAbsoluteString() + "!");
	close(Result);
	Output.open(&Filename[0], FileOutput::trunc);
	return FilePath(&Filename[0]);
}

