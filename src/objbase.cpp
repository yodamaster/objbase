#include "stdafx.h"

static std::mutex lock_;
static std::shared_ptr<ObjBase> instance_;

std::shared_ptr<ObjBase> OBJBASE_API GetObjBase()
{
	std::lock_guard<decltype(lock_)> l(lock_);
	if (nullptr == instance_)
	{
		instance_ = std::make_shared<ObjBase>();
	}
	return instance_;
}

