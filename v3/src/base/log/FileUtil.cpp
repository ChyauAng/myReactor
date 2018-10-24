#include "FileUtil.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

AppendFile::AppendFile(StringArg filename)
    :fp_(fopen(filename.c_str(), "ae")),
    writtenBytes_(0){
        assert(fp_);  // close assert when releasing to avoid running time cost
        setbuffer(fp_, buffer_, sizeof buffer_);  // buffer of file, buffer + flush
}

AppendFile::~AppendFile(){
    fclose(fp_);
}

void AppendFile::append(const char* logline, const size_t len){
    size_t n = this->write(logline, len);
    size_t remain = len - n;
    while(remain > 0){
        size_t x = this->write(logline + n, remain);
        if(x == 0){
            int err = ferror(fp_);
            if(err){
                fprintf(stderr, "AppendFile::append() failed.\n");
            }
            break;
        }
        n += x;
        remain = len - n;
    }
    writtenBytes_ += len;
}

void AppendFile::flush(){
    fflush(fp_);
}

size_t AppendFile::write(const char* logline, size_t len){
    return fwrite_unlocked(logline, 1, len, fp_);
}


