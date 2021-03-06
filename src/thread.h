/* $Id$ */

/** @file thread.h */

#ifndef THREAD_H
#define THREAD_H

struct OTTDThread;

typedef void * (*OTTDThreadFunc)(void*);

OTTDThread *OTTDCreateThread(OTTDThreadFunc, void*);
void       *OTTDJoinThread(OTTDThread*);
void        OTTDExitThread();

#endif /* THREAD_H */
