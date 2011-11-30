/*
 * abfile.h
 *
 *  Created on: Oct 8, 2011
 *      Author: august
 */

#ifndef ABFILE_H_
#define ABFILE_H_
#include <queue>
#include <map>
#include <string>
#include <stdint.h>
#include "Block1.h"
#include "abfs.h"
class ABFS;

typedef   std::map<int,Block*> blockmap;
typedef const char * cstr;
class ABFile {
    boost::recursive_mutex mMutex;
    blockmap blocks;
//    std::queue<int> blockQueue;

    FILE *mMetaFile;
    int flushBlocks();
    uint64_t mLength;
    uint64_t mCounter;
    int mFlags;//O_RDWRT etc.
    void persistLength(off_t offset);
public:
    friend class FileManager;
    bool reopen(int flags)
    {
        Lock l(mMutex);
        mFlags=flags;
        fclose(mMetaFile);
      	std::string metafile=filename;
	metafile+="/meta";

	mMetaFile=fopen(metafile.c_str(),"r+b");
    }
    void truncate(off_t size);
    int mRefCount;
    ABFile(cstr path):mLength(0),mCounter(0),mRefCount(0)
    {
    	filename=path;
    }
    ~ABFile()
    {
        close();
    }
    ABFS *mFs;
    bool exists();
    uint64_t getLength(){return mLength;}
    bool open(cstr path,int mode);
    static int getattr(cstr path, struct stat *st, bool readsize /* = false */);
    static bool create(cstr path,mode_t mode);

    void getBlockFile(int blocknr,char *filename);
    std::string filename;
    Block *getWriteBlock(off_t offset);

    FILE *getWriteBlockFile(int blocknr);
    int write(byte *buf, off_t offset, int len);
    int read(byte *buf, off_t offset, int len);
    void close();
};






#endif /* ABFILE_H_ */
