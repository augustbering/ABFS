/*
 * abfs.h
 *
 *  Created on: Oct 10, 2011
 *      Author: august
 */

#ifndef ABFS_H_
#define ABFS_H_
#include <limits.h>
#include "rlog.h"
#include <string>
#include <boost/thread.hpp>
const char *getpath(const char *path);
byte *getBuffer();
void returnCompBuffer(byte *buf);
typedef boost::lock_guard<boost::recursive_mutex> Lock;
class ABFS
{
public:
//	std::string mFsRoot;
};

extern rlog::RLog logger;

#endif /* ABFS_H_ */
