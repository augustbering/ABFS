/*
 * Block1.h
 *
 *  Created on: Oct 8, 2011
 *      Author: august
 */

#ifndef BLOCK1_H_
#define BLOCK1_H_

typedef unsigned char byte;
#define BLOCKSIZE 8*1024*96
#define BLOCKNR(x) (int((x)/(BLOCKSIZE)))
#define BLOCKSTART(block) (block->mBlockNr*BLOCKSIZE)
#include <lzo/lzo1x.h>
class ABFile;
class Block {
public:
	bool mDirty;
    byte *data;
    int mBlockNr;
    lzo_uint dataLen;

    int mLastUse;
    int read();
    ABFile *mFile;
	int write();
	Block(ABFile *file,int blocknr);
	void ove();
	virtual ~Block();
	 void decompress(byte *src, int src_len);
};

#endif /* BLOCK1_H_ */
