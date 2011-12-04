#include "abfile.h"
#include <string.h>
#include <stdio.h>
#include <map>
#include <algorithm>
#include <fuse.h>
#include <sys/statfs.h>
#include "abfs.h"
#include <errno.h>
#include <sys/stat.h>
/**
 * Max number of blocks to keep in memory at once for one open file
 */
const unsigned int maxblocks = 3;
void ABFile::truncate(off_t size) {
	Lock l(mMutex);

	if (size < mLength) {
		//delete old files.
		char fname[PATH_MAX];
		for (int i = BLOCKNR(size) + 1; i < BLOCKNR(mLength) + 1; i++) {
			getBlockFile(i, fname);
			unlink(fname);
		}
	}
	mLength = size;
	persistLength(size);
}
Block *ABFile::getWriteBlock(off_t offset) {

	Lock l(mMutex);
	int blocknr = BLOCKNR(offset);
	std::_Rb_tree_iterator<std::pair<const int, Block*> > blockit;
	blockit = mBlocks.find(blocknr);

	if (blockit == mBlocks.end()) {
		Block *block = new Block(this, blocknr);
		block->mLastUse = mCounter++;
		mBlocks[blocknr] = block;
		flushBlocks();
		return block;
	} else
		return blockit->second;

}

FILE *ABFile::getWriteBlockFile(int blocknr) {
	char buf[PATH_MAX];
	getBlockFile(blocknr, buf);
	return fopen(buf, "wb");
}

void ABFile::getBlockFile(int blocknr, char *filenameout) {
	sprintf(filenameout, "%s/%d", filename.c_str(), blocknr);

}

bool ABFile::open(cstr path, int flags) {
	filename = path;
//	mode_t defaultmode = 0755;//octal
	std::string metafile = filename;
	metafile += "/meta";
	const char *mode;
	if ((3 & flags) == O_RDONLY)
		mode = "rb";
	else
		mode = "r+b";
	mFlags = flags;
	mMetaFile = fopen(metafile.c_str(), mode);
	if (mMetaFile) {
		int r = fread(&mLength, sizeof(mLength), 1, mMetaFile);
		assert(r==1);

		//	fclose(mfile);
	}
	return true;
}

/**
 * Flush oldest blocks to disk
 */
int ABFile::flushBlocks() {
	int flushed = 0;
//	Comp cmp;
	//std::sort(blocks.begin(),blocks.end(),cmp);
	while (mBlocks.size() > maxblocks) { //remove last and write to disk.
		blockmap::iterator it = mBlocks.begin();
		blockmap::iterator oldestblock = it;

		for (; it != mBlocks.end(); it++) {
			if (it->second->mLastUse < oldestblock->second->mLastUse)
				oldestblock = it;
		}

		Block *b = oldestblock->second;
		mBlocks.erase(oldestblock);
		if (b->mDirty) {
			rInfo("Flushing block %d", b->mBlockNr);
			b->write();
		}
		delete b;
		flushed++;
	}
	return flushed;
}

void ABFile::persistLength(off_t offset) {
	mLength = std::max<int>(offset, mLength);
	fseek(mMetaFile, 0, SEEK_SET);
	int wr = fwrite(&mLength, sizeof(mLength), 1, mMetaFile);
	assert(wr==1);
}

int ABFile::write(byte *buf, off_t offset, int len) {

	Lock l(mMutex);
	int written = 0;
	while (len > 0) {

		Block *b = getWriteBlock(offset);
		//can't get write block=?
		int off = offset - BLOCKSTART(b);
		int blen = std::min<int>(len, BLOCKSIZE - off);
		if ((off == 0 && len > BLOCKSIZE) || offset >= mLength)
			; //whole block is rewritten, no need to load
		else
			b->read();

		memcpy(b->data + off, buf, blen);
		off_t dlen = off + blen;
		if (dlen > b->dataLen)
			b->dataLen = dlen;

		b->mDirty = true;
		buf += blen;
		offset += blen;
		len -= blen;
		written += blen;

//		releaseBlock(b);
	}
	persistLength(offset);
	return written;
}

int ABFile::read(byte *buf, off_t offset, int len) {
	Lock l(mMutex);
	int total = 0;
	while (len > 0) {
		Block *b = getWriteBlock(offset);
		int read = b->read();
		if (offset + read < mLength) {
			//might be the block is all zero and doesn't exist on disk.
			//fill with zeros
			memset(b->data + read, 0, BLOCKSIZE - read);
			read = b->dataLen = std::min<int>(BLOCKSIZE,
					mLength - BLOCKSTART(b));
		}
		int off = offset - BLOCKSTART(b);
		int blen = std::min<int>(len, read - off);
		if (blen <= 0) //EOF
			return total;

		memcpy(buf, b->data + off, blen);
		buf += blen;
		offset += blen;
		len -= blen;
		total += blen;
	}
	return total;
}

bool ABFile::exists() {
	struct stat st;
	return 0 == stat(filename.c_str(), &st);

}

bool ABFile::create(cstr path, mode_t mode) {
//	mode_t defaultmode = 0755;//octal

	mode = 0755; //for directory
	std::string filename = path;
	//if (mode & (O_RDWR | O_WRONLY))
	{
		if (0 != mkdir(filename.c_str(), 0777 & mode))
			return -errno;
		std::string meta = filename;
		meta += "/meta";
		FILE *f = fopen(meta.c_str(), "wb");
		if (!f)
			return false;
		uint64_t len = 0;
		fwrite(&len, sizeof(len), 1, f);
		fclose(f);
	}

	return true;

}

int ABFile::getattr(cstr path, struct stat *st, bool readsize /* = false */) {

	std::string meta = path;
	meta += "/meta";
	int r = lstat(meta.c_str(), st);
	if (r == 0) { //found meta file, this is a file!
		if (!readsize)
			return 0;
		FILE *mfile = fopen(meta.c_str(), "rb");
		if (mfile) {
			uint64_t len;
			fread(&len, sizeof(len), 1, mfile);

			st->st_size = len;
			st->st_mode = (st->st_mode & ~S_IFMT) | S_IFREG;
			fclose(mfile);
			return 0;
		}
		return -ENOENT;
	}

	//else it's a dir
	r = lstat(path, st);
	if (r != 0 || !S_ISDIR(st->st_mode))
		return -errno;

	return 0;
}

void ABFile::close() {

	Lock l(mMutex);
	blockmap::iterator it = mBlocks.begin();

	for (; it != mBlocks.end(); it++) {

		Block *b = it->second;
		if (b->mDirty)
			b->write();
		delete b;
	}

	mBlocks.clear();

	fclose(mMetaFile);
	/* Done in write now. This should only be done for dirty files...
	 std::string metafile=filename;
	 metafile+="/meta";
	 FILE *mfile=fopen(metafile.c_str(),"wb");
	 if (mfile)
	 {
	 fwrite(&mLength,sizeof(mLength),1,mfile);
	 fclose(mfile);
	 }
	 */
}
