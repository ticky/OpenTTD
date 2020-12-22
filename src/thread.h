/* $Id$ */

/** @file thread.h Base of all threads. */

#ifndef THREAD_H
#define THREAD_H

struct OTTDThread;

typedef void * (*OTTDThreadFunc)(void*);

OTTDThread *OTTDCreateThread(OTTDThreadFunc, void*);
void       *OTTDJoinThread(OTTDThread*);
void        OTTDExitThread();

#endif /* THREAD_H */
