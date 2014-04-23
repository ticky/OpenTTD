/* $Id$ */

#include "../stdafx.h"

#include "countedptr.hpp"

#include "../safeguards.h"

int32 SimpleCountedObject::AddRef()
{
	return ++m_ref_cnt;
}

int32 SimpleCountedObject::Release()
{
	int32 res = --m_ref_cnt;
	assert(res >= 0);
	if (res == 0) {
		FinalRelease();
		delete this;
	}
	return res;
}
