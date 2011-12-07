#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <map>
#include <queue>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "lzo/lzoconf.h"
#include "lzo/lzo1x.h"
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>

#include "Block.h"
#include "abfile.h"
#include "abfs.h"
rlog::RLog logger(0, LOG_INFO, true);
rlog::RLog *g_RLog = &logger;

std::string fsroot, g_dirMount;

static const char *getpath(const char *path) {
	assert(path[0] != '\0');

	if ((path[0] == '/') && (path[1] == '\0'))
		return ".";
	return ++path;
}

class FileManager {
	typedef std::map<std::string, ABFile*> filemap;
	filemap mOpenFiles;
	boost::recursive_mutex mMutex;

public:

	void releaseFile(cstr path) {
		Lock l(mMutex);
		rDebug("ABFS:release %s", path);

		ABFile *f = mOpenFiles[path];
//		int s = mOpenFiles.size();
//        filemap::iterator it=mOpenFiles.find(path);

		assert(f);
		//it!=mOpenFiles.end());
//        f=it->second;
		f->mRefCount--;
		if (f->mRefCount < 1) {
			mOpenFiles.erase(path);
			delete f;
		}
	}
	int getattr(cstr path, struct stat *st) {
		Lock l(mMutex);
		ABFile *f = getOpenFile(path);

		int r = ABFile::getattr(path, st, f == 0);
		if (f) {
			st->st_size = f->getLength();
		}
		/*
		 if (r == 0) {
		 std::map<cstr, ABFile*>::iterator it = mOpenFiles.find(path);
		 if (it != mOpenFiles.end()) {
		 st->st_size = it->second->getLength();
		 }
		 }*/
		return r;
	}

	ABFile *getOpenFile(cstr path) {
		return mOpenFiles[path];
	}
	ABFile *getFile(cstr path, int flags) {
		Lock l(mMutex);
		rDebug("ABFS:getfile %s", path);
		ABFile *f = mOpenFiles[path];
		if (!f) {
			f = new ABFile(path);
			if (!f->open(path, flags)) //fi->mode);
					{
				delete f;
				return 0;
			}
//            std::make_pair<cstr,ABFile*>(path,f);
			mOpenFiles[path] = f;

			ABFile *f2 = mOpenFiles[path];
			assert(f==f2);
		} else {
			if ((flags & 3) != f->mFlags) { ///trying to open a read-only opened file, in write mode.... change it.
				if ((flags & 3) == O_WRONLY || (flags & 3) == O_RDWR) {
					f->reopen(O_RDWR);
				}
			}
		}
		f->mRefCount++;
		return f;
	}
};
FileManager gFileManager;
static int abfsGetattr(const char *name, struct stat *st) {
	int r = 0;

	name = getpath(name);

	// Speed optimization: Fast path for '.' questions.
	//
	if ((name[0] == '.') && (name[1] == '\0')) {
		r = lstat(name, st);

		return 0;
	}
	/*
	 memset(stbuf, 0, sizeof(struct stat));
	 if (strcmp(path, "/") == 0) {
	 stbuf->st_mode = S_IFDIR | 0755;
	 stbuf->st_nlink = 2;
	 } else if (strcmp(path, hello_path) == 0) {
	 stbuf->st_mode = S_IFREG | 0444;
	 stbuf->st_nlink = 1;
	 stbuf->st_size = strlen(hello_str);
	 } else
	 res = -ENOENT;
	 */
	r = gFileManager.getattr(name, st);
	return r;
}

static int abfsReaddir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi) {
	(void) offset;
	(void) fi;

	//   ABFile *file=(ABFile*)fi->fh;
	path = getpath(path); //file->mFs->mFsRoot;

	DIR *dp;
	struct dirent *de;
	dp = ::opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		//printf("%s\n", namelist[n]->d_name);
		struct stat st;
		std::string f = path;
		f += de->d_name;
		std::string pathname = f;
		f += "/meta";
		bool dir;
		//if exists meta, it's a file, else a dir
		if (0 == stat(f.c_str(), &st))
			dir = false;
		else
			dir = true;
		stat(pathname.c_str(), &st);
		st.st_mode = (st.st_mode & (~S_IFMT)) | (dir ? S_IFDIR : S_IFREG);
		if (filler(buf, de->d_name, &st, 0))
			break;
	}
	closedir(dp);

	return 0;
}

static int abfsOpen(const char *path, struct fuse_file_info *fi) {

	path = getpath(path);
	ABFile *file = gFileManager.getFile(path, fi->flags);
	rDebug("FuseCompress::open %p name: %s", (void *) file, path);

	if (!file)
		return -ENOENT;

	/*    if ((fi->flags & 3) == O_RDONLY)
	 {
	 if (!file->exists())
	 return -ENOENT;
	 }*/
	fi->fh = (uint64_t) file;
	//        return -EACCES;

	return 0;
}

int abfsTruncate(const char *name, off_t size) {
	int r = 0;

	name = getpath(name);
	rDebug("FuseCompress::truncate file %s, to size: %llx",
			name, (long long int) size);

	ABFile *f = gFileManager.getFile(name, O_WRONLY);
	f->truncate(size);
	gFileManager.releaseFile(name);
	/*	file = g_FileManager->Get(name);
	 if (!file)
	 return -errno;

	 file->Lock();

	 if (file->truncate(name, size) == -1)
	 r = -errno;

	 file->Unlock();

	 g_FileManager->Put(file);
	 */
	return r;
}

static int abfsRead(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi) {

	ABFile *file = (ABFile*) fi->fh;

	rDebug("FuseCompress::read(B) %p name: %s, size: 0x%x, offset: 0x%llx",
			(void *) file, path, (unsigned int) size, (long long int) offset);

	return file->read((byte*) buf, offset, size);

}

static int abfsWrite(const char *path, const char *buf, size_t size,
		off_t offset, struct fuse_file_info *fi) {

	ABFile *file = (ABFile*) fi->fh;
	rDebug("FuseCompress::write %p name: %s, size: 0x%x, offset: 0x%llx",
			(void *) file, path, (unsigned int) size, (long long int) offset);
	return file->write((byte*) buf, offset, size);

}

#define NCOMPBUFFS 3
//byte compbuffers[NCOMPBUFFS][BLOCKSIZE + 1000];

/**
 * Temporary buffers used for compression. We share a few for all open files to
 * conserve memory
 */
std::queue<byte*> gCompBuffers;
boost::mutex gCompBuffMutex;
byte *getCompBuffer() {
	gCompBuffMutex.lock();
	byte *buf = gCompBuffers.front();
//    if (gCompBuffers.size()>0)
	while (buf == 0) {
		gCompBuffMutex.unlock();
		sleep(0);
		gCompBuffMutex.lock();
		buf = gCompBuffers.front();
	}

	gCompBuffers.pop();

	gCompBuffMutex.unlock();
	return buf;
}
void returnCompBuffer(byte *buf) {
	gCompBuffMutex.lock();
	gCompBuffers.push(buf);
	gCompBuffMutex.unlock();
}
static void test() {
	ABFile file("lars");
	file.open("lars", O_RDWR);
	file.exists();

	byte buf[2000];
	FILE *inp = fopen("yabe1.0", "rb");
	memset(buf, '5', sizeof(buf));
	//file.write(buf,0,20);
	int pos = 0;
	size_t len = sizeof(buf);
	/* while((len=fread(buf, 1,sizeof (buf), inp))>0){
	 file.write(buf, pos, lean);
	 pos += len;
	 }*/
	for (int i = 0; i < 10; i++) {
		pos = rand() % file.getLength();
		fseek(inp, pos, SEEK_SET);
		len = pos % 2000;
		len = fread(buf, 1, len, inp);
		file.write(buf, pos, len);
	}
	fclose(inp);
	/*
	 for(int i = 0;i < 100;i++){
	 memset(buf, 'a' + i % 28, sizeof (buf));
	 file.write(buf, i * 200, 200);
	 }*/
	file.close();
	printf("size:%d", (int) (file.getLength()));
	ABFile f2("lars");
	f2.open("lars", O_RDWR);

	FILE *out = fopen("outtest", "wb");
	pos = 0;
	while ((len = f2.read(buf, pos, sizeof(buf))) > 0) {
		fwrite(buf, len, 1, out);

		pos += len;

	}
	fclose(out);

	//	file.read(buf,200*15-10,200);
	//printf("b:%s",buf);
}

using namespace boost;

int abfsMknod(const char *name, mode_t mode, dev_t rdev) {
	name = getpath(name);
	if (S_ISREG(mode)) {
		if (!ABFile::create(name, mode))
			return -errno;
		return 0;
	}

	return -1;
}

int abfsUnlink(cstr file) {
	rDebug("FuseCompress::unlink %s", file);

	rInfo("unlink %s", file);
	boost::filesystem::path myfile(getpath(file));

	if (!filesystem::exists(myfile))
		return -ENOENT;
	//is this correct? errno..
	boost::filesystem::remove_all(myfile);
	return 0;
}
DIR *gDir;
static void *abfsInit(struct fuse_conn_info *conn) {

	if (fchdir(dirfd(gDir)) == -1) {
		rError("Failed to change directory");
		abort();
	}
	closedir(gDir);

//	chdir(fsroot.c_str());

	//add some compression working buffers.
	for (int i = 0; i < NCOMPBUFFS; i++) {
		byte *b = new byte[BLOCKSIZE + 1000];
		returnCompBuffer(b);
	}
	return 0;
}

int abfsSetxattr(const char *path, const char *name, const char *value,
		size_t size, int flags) {

	return 0;
}
int abfsRelease(cstr path, struct fuse_file_info *fi) {
//    ABFile *file = (ABFile*) fi->fh;
	gFileManager.releaseFile(getpath(path));
	return 0;
}
/**
 * Kill the whole file system
 */
void abfsDestroy(void *) {
	while (gCompBuffers.size()) {
		byte *b = gCompBuffers.front();
		delete[] b;
		gCompBuffers.pop();
	}

}

int abfsMkdir(const char *path, mode_t mode) {
	int r = 0;
	if (0 != mkdir(getpath(path), mode)) {
		r = -errno;
	}
	return r;
}
int abfsRmdir(cstr path) {
	path = getpath(path);

	if (::rmdir(path) == -1)
		return -errno;

	return 0;
}

int abfsRename(const char *f1, const char *f2) {
	return rename(getpath(f1), getpath(f2));
}

using namespace std;
namespace po = boost::program_options;
bool g_DebugMode;
void print_help(const po::options_description &rDesc) {
	cout << rDesc<<std::endl;
	cout<<"Unrecocognized options are passed on to the fuse library.\n"
			"E.g. -d (debug)\n";
}
int main(int argc, char *argv[]) {
	lzo_init();

	//    test();

//	fsroot = "/tmp/abfs";
	struct fuse_operations abfs_oper;
	memset(&abfs_oper, 0, sizeof(abfs_oper));
	abfs_oper.getattr = abfsGetattr;
	abfs_oper.readdir = abfsReaddir;
	abfs_oper.open = abfsOpen;
	abfs_oper.read = abfsRead;
	abfs_oper.write = abfsWrite;
	abfs_oper.unlink = abfsUnlink;
	abfs_oper.init = abfsInit;
	//	hello_oper.create =create;
	abfs_oper.mknod = abfsMknod;
	//hello_oper.setxattr = setxattr;
	abfs_oper.truncate = abfsTruncate;
	abfs_oper.release = abfsRelease;
	abfs_oper.destroy = abfsDestroy;
	abfs_oper.mkdir = abfsMkdir;
	abfs_oper.rmdir = abfsRmdir;
	abfs_oper.rename = abfsRename;

	boost::program_options::options_description desc(
			"Usage: " "abfs" " [options] dir_lower dir_mount\n" "\nAllowed options");

	string compressorName;
	string commandLineOptions;

	vector<string> fuseOptions;
	fuseOptions.push_back(argv[0]);

#define VERSION "0.1"
	cout<<"ABFS Version " VERSION<<std::endl;

	desc.add_options()("help,h", "print this help")("dir_lower", po::value<string>(&fsroot),
			"storage directory")("dir_mount", po::value<string>(&g_dirMount),
			"mount point")

			;

	po::positional_options_description pdesc;
	pdesc.add("dir_lower", 1);
	pdesc.add("dir_mount", 1);

	po::variables_map vm;

	try {
		po::parsed_options parsed = po::command_line_parser(argc, argv).options(
				desc).positional(pdesc).allow_unregistered().run();
		po::store(parsed, vm);
		po::notify(vm);

		if (vm.count("help")) {
			print_help(desc);
			exit(EXIT_SUCCESS);
		}
		if (vm.count("dir_lower")) {
			fsroot = vm["dir_lower"].as<string>();
		} else {
			print_help(desc);
			exit(EXIT_FAILURE);
		}

		// Set up default options for fuse.
		//
		string mountdir = vm["dir_mount"].as<string>();
		fuseOptions.push_back(mountdir);


		if ((gDir = opendir(fsroot.c_str())) == NULL) {
			int errns = errno;

			cerr << "Failed to open storage directory " << "'" << fsroot
					<< "': " << strerror(errns) << endl;
			exit(EXIT_FAILURE);
		}

		vector<string> to_pass_further = po::collect_unrecognized(
				parsed.options, po::exclude_positional);

		umask(0);

		vector<cstr> fuseopts;
		fuseopts.push_back(argv[0]);
		for (int i = 0; i < to_pass_further.size(); i++) {
			fuseopts.push_back(to_pass_further[i].c_str());
			cout<<to_pass_further[i].c_str();
		}
		fuseopts.push_back(g_dirMount.c_str());
		fuseopts.push_back("-s");
		return fuse_main(fuseopts.size(), (char**)&fuseopts[0], &abfs_oper, NULL);
	} catch (...) {
		print_help( desc);
		exit(EXIT_FAILURE);
	}

}
