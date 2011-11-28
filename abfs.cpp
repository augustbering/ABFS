/*
 FUSE: Filesystem in Userspace
 Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

 This program can be distributed under the terms of the GNU GPL.
 See the file COPYING.

 gcc -Wall `pkg-config fuse --cflags --libs` hello.c -o hello
 */

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

#include "Block1.h"
#include "abfile.h"
#include "abfs.h"
rlog::RLog logger(0, LOG_INFO, true);
rlog::RLog *g_RLog = &logger;
static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";

cstr fsroot = 0;

class FileManager {
	typedef std::map<std::string, ABFile*> filemap;
    filemap mOpenFiles;
    boost::recursive_mutex mMutex;

public:

    void releaseFile(cstr path){
        Lock l(mMutex);
        rDebug("ABFS:release %s",path);

        ABFile *f=mOpenFiles[path];
        int s=mOpenFiles.size();
//        filemap::iterator it=mOpenFiles.find(path);
        
        assert(f);//it!=mOpenFiles.end());
//        f=it->second;
        f->mRefCount--;
        if (f->mRefCount<1)
        {
            mOpenFiles.erase(path);
            //TODO:iterator
            delete f;
        }
    }
    int getattr(cstr path, struct stat *st) {
        Lock l(mMutex);
        ABFile *f=getOpenFile(path);
        
        int r = ABFile::getattr(path, st, f==0);
        if (f)
        {
            st->st_size=f->getLength();
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

    ABFile *getOpenFile(cstr path)
    {
        return mOpenFiles[path];
    }
    ABFile *getFile(cstr path,int flags) {
        Lock l(mMutex);
        rDebug("ABFS:getfile %s",path);
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
            
            ABFile *f2=mOpenFiles[path];
            assert(f==f2);
        }
        else{
            if ((flags&3)!=f->mFlags)
            {///trying to open a read-only opened file, in write mode.... change it.
               if ((flags&3)==O_WRONLY ||(flags&3)==O_RDWR)
               {
                   f->mFlags=O_RDWR;
               }
            }
        }
        f->mRefCount++;
        return f;
    }
};
FileManager gFileManager;
static int hello_getattr(const char *name, struct stat *st) {
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

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
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




static int hello_open(const char *path, struct fuse_file_info *fi) {

    path = getpath(path);
    ABFile *file = gFileManager.getFile(path,fi->flags);
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

int truncate(const char *name, off_t size) {
    int r = 0;

    name = getpath(name);
    rDebug("FuseCompress::truncate file %s, to size: %llx", name,
            (long long int) size);

    ABFile *f=gFileManager.getFile(name,O_WRONLY);
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

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi) {

    ABFile *file = (ABFile*) fi->fh;

    rDebug("FuseCompress::read(B) %p name: %s, size: 0x%x, offset: 0x%llx",
            (void *) file, path, (unsigned int) size, (long long int) offset);

    return file->read((byte*) buf, offset, size);

}

static int hello_write(const char *path, const char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi) {

    ABFile *file = (ABFile*) fi->fh;
    rDebug("FuseCompress::write %p name: %s, size: 0x%x, offset: 0x%llx",
            (void *) file, path, (unsigned int) size, (long long int) offset);
    return file->write((byte*) buf, offset, size);

}

#define NCOMPBUFFS 3
//byte compbuffers[NCOMPBUFFS][BLOCKSIZE + 1000];
std::queue<byte*> compbuffers;
boost::mutex gCompBuffMutex;
byte *getBuffer(){
    gCompBuffMutex.lock();
    byte *buf=compbuffers.front();
//    if (compbuffers.size()>0)
    while (buf==0){
        gCompBuffMutex.unlock();   
        sleep(0);
        gCompBuffMutex.lock();
        buf=compbuffers.front();
    }
    
        compbuffers.pop();

    gCompBuffMutex.unlock();
    return buf;
}
void returnCompBuffer(byte *buf)
{
    gCompBuffMutex.lock();
    compbuffers.push(buf);
    gCompBuffMutex.unlock();    
}
void test() {
    ABFile file("lars");
    file.open("lars", O_RDWR);
    file.exists();

    byte buf[2000];
    FILE *inp = fopen("yabe1.0", "rb");
    memset(buf, '5', sizeof (buf));
    //file.write(buf,0,20);
    int pos = 0;
    size_t len = sizeof (buf);
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
    while ((len = f2.read(buf, pos, sizeof (buf))) > 0) {
        fwrite(buf, len, 1, out);

        pos += len;

    }
    fclose(out);

    //	file.read(buf,200*15-10,200);
    //printf("b:%s",buf);
}

const char *getpath(const char *path) {
    assert(path[0] != '\0');

    if ((path[0] == '/') && (path[1] == '\0'))
        return ".";
    return ++path;
}

using namespace boost;


int fuseMknod(const char *name, mode_t mode, dev_t rdev) {
    name = getpath(name);
    if (S_ISREG(mode))
    {
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
    int count = boost::filesystem::remove_all(myfile);
    return 0;
}

static void *init(struct fuse_conn_info *conn) {
    chdir(fsroot);
    
    //add some compression working buffers.
    for (int i=0;i<NCOMPBUFFS;i++){
        byte *b=new byte[BLOCKSIZE + 1000];
        returnCompBuffer(b);
    }
    return 0;
}

int setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {

	return 0;
}
int release(cstr path,struct fuse_file_info *fi)
{
//    ABFile *file = (ABFile*) fi->fh;
    gFileManager.releaseFile(getpath(path));
    return 0;
}
void abfsdestroy(void *)
{
    while (compbuffers.size())
    {
        byte *b=compbuffers.front();
        delete []b;
        compbuffers.pop();
    }
    
}
int main(int argc, char *argv[]) {
    lzo_init();

    //    test();

    fsroot = "/tmp/abfs";
    struct fuse_operations hello_oper;
    memset(&hello_oper, 0, sizeof (hello_oper));
    hello_oper.getattr = hello_getattr;
    hello_oper.readdir = hello_readdir;
    hello_oper.open = hello_open;
    hello_oper.read = hello_read;
    hello_oper.write = hello_write;
    hello_oper.unlink = abfsUnlink;
    hello_oper.init = init;
    //	hello_oper.create =create;
    hello_oper.mknod = fuseMknod;
    //hello_oper.setxattr = setxattr;
    hello_oper.truncate = truncate;
    hello_oper.release=release;
    hello_oper.destroy=abfsdestroy;

    return fuse_main(argc, argv, &hello_oper, NULL);

}
