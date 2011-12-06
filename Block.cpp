/*
 * Block1.cpp
 *
 *  Created on: Oct 8, 2011
 *      Author: august
 */
#include "Block.h"
#include "lzo/lzo1x.h"
#include "abfs.h"
#include "abfile.h"
#include <stdio.h>
#include <limits.h>

int Block::write() {
	lzo_uint outsize;

#ifdef NOCOMP
	size_t written=fwrite(data, dataLen, 1, f);
#else
	lzo_byte wrkmem[LZO1X_1_MEM_COMPRESS];
	byte *compbuffer = getCompBuffer();
	lzo1x_1_compress(data, dataLen, compbuffer, &outsize, wrkmem);
	FILE *f = mFile->getWriteBlockFile(mBlockNr);
	size_t written=fwrite(compbuffer, outsize, 1, f);
	returnCompBuffer(compbuffer);

#endif
	fclose(f);
	if (1!=written)
	{
		DiskException d;
		char filename[PATH_MAX];
		mFile->getBlockFile(mBlockNr,filename);
		d.filename=filename;
		d.error="Can't write to file";
		throw d;
	}
	mDirty = false;

	return BLOCKSIZE;

}

Block::~Block() {
	delete[] data;
}

Block::Block(ABFile *file, int blocknr) :
		mDirty(true), mBlockNr(blocknr), dataLen(0), mLastUse(0), mFile(file) {

	data = new byte[BLOCKSIZE];
	memset(data, 0, BLOCKSIZE);

}

int Block::read() {
	if (dataLen) //already loaded
		return dataLen;
	char filename[PATH_MAX];
	mFile->getBlockFile(mBlockNr, filename);
	FILE *f = fopen(filename, "rb");
	if (f) {
#ifdef NOCOMP
		int read = fread(data,1, BLOCKSIZE + 1000, f);
#else
		byte *compbuffer = getCompBuffer();
		int read = fread(compbuffer, 1, BLOCKSIZE + 1000, f);
		decompress(compbuffer, read);
		returnCompBuffer(compbuffer);

#endif
		fclose(f);
		mDirty = false;
	} else
		mDirty = true;
	return dataLen;
}

void Block::decompress(byte *src, int src_len) {
	lzo1x_decompress(src, src_len, data, &dataLen, 0);
}

