/*
 * Block1.cpp
 *
 *  Created on: Oct 8, 2011
 *      Author: august
 */
#include "Block1.h"
#include "lzo/lzo1x.h"
#include "abfs.h"
#include "abfile.h"
#include <stdio.h>
extern byte compbuffer[];
lzo_byte wrkmem[LZO1X_1_MEM_COMPRESS];
int Block::write() {
	lzo_uint outsize;
	byte *compbuffer=getBuffer();
	lzo1x_1_compress(data, dataLen, compbuffer, &outsize, wrkmem);
	FILE *f = mFile->getWriteBlockFile(mBlockNr);
//	FILE *f = fopen(file, "wb");
	fwrite(compbuffer, outsize, 1, f);
	returnCompBuffer(compbuffer);
	fclose(f);
	mDirty = false;

	return BLOCKSIZE;

}

Block::~Block() {
	delete[] data;
}

Block::Block(ABFile *file, int blocknr) :dataLen(0),
		mBlockNr(blocknr), mFile(file), mLastUse(0), mDirty(true) {

	data = new byte[BLOCKSIZE];
	memset(data,0,BLOCKSIZE);

}

int Block::read() {
	if (dataLen)//already loaded
		return dataLen;
	char filename[PATH_MAX];
	mFile->getBlockFile(mBlockNr, filename);
	FILE *f = fopen(filename, "rb");
	if (f) {
		byte *compbuffer=getBuffer();
		int read = fread(compbuffer,1, BLOCKSIZE + 1000, f);
		fclose(f);
		decompress(compbuffer, read);
		returnCompBuffer(compbuffer);
		mDirty = false;
	} else
		mDirty = true;
	return dataLen;
}

void Block::decompress(byte *src, int src_len) {
	lzo1x_decompress(src, src_len, data, &dataLen, 0);
}

