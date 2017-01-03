#pragma once

#include <memory>
#include <mutex>
#include <map>
#include <functional>
#include <string>
#include <xstring>
#include <windows.h>
#include <assert.h>

#ifndef DLL_SEPARATOR
#define DLL_SEPARATOR L"@"
#endif

#ifdef OBJBASE_EXPORTS
#define OBJBASE_API __declspec(dllexport)
#pragma warning(disable:4251)
#else
#define OBJBASE_API
#endif

class ObjBase;
std::shared_ptr<ObjBase> OBJBASE_API GetObjBase();

// host object
class ObjBase
{
protected:
	std::recursive_mutex lock_;
	struct ClassInfo
	{
		std::function<void*(void**)> pfnCreate;
		std::function<void(void*)> pfnDestroy;
	};
	std::map<std::wstring, std::shared_ptr<ClassInfo>> classes_;
	std::map<std::wstring, std::weak_ptr<void>> singletons_;

public:
	ObjBase() = default;
	virtual ~ObjBase() = default;

	static std::shared_ptr<ObjBase> getInstance(){ return GetObjBase(); }

	template<typename Interface>
	auto CreateObj(std::wstring objName) -> std::shared_ptr<Interface>
	{
		auto info = GetClsInfo(objName);
		if (info)
		{
			void* obj = nullptr;
			return std::shared_ptr<Interface>(reinterpret_cast<Interface*>(info->pfnCreate(&obj)),
				[info, obj](Interface*)
			{
				info->pfnDestroy(obj);
			});
		}
		return {};
	}

	template<typename Interface>
	auto CreateSingletonObj(std::wstring objName) -> std::shared_ptr<Interface>
	{
		// scoped
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			auto iter = singletons_.find(objName);
			if (iter != singletons_.end())
			{
				auto obj = iter->second.lock();
				if (obj)
					return std::static_pointer_cast<Interface>(obj);
			}
		}

		auto info = GetClsInfo(objName);
		if (info)
		{
			void* obj = nullptr;
			auto ret = std::shared_ptr<Interface>(reinterpret_cast<Interface*>(info->pfnCreate(&obj)),
				[this, info, objName, obj](Interface*)
			{
				// scoped
				{
					std::lock_guard<decltype(lock_)> l(lock_);
					singletons_.erase(objName);
				}

				info->pfnDestroy(obj);
			});

			std::lock_guard<decltype(lock_)> l(lock_);
			singletons_[objName] = ret;
			return ret;
		}
		return {};
	}

	auto RegisterCls(
		std::wstring objName, 
		std::function<void*(void**)> pfnCreate, 
		std::function<void(void*)> pfnDestroy) -> std::shared_ptr<void>
	{
		auto info = std::make_shared<ClassInfo>();
		info->pfnCreate = pfnCreate;
		info->pfnDestroy = pfnDestroy;

		std::lock_guard<decltype(lock_)> l(lock_);
		classes_[objName] = info;

		return { (void*)1, [this, objName](void* p)
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			classes_.erase(objName);
		} };
	}

protected:
	auto GetClsInfo(std::wstring name) -> std::shared_ptr<ClassInfo>
	{
		// scoped
		{
			std::lock_guard<decltype(lock_)> l(lock_);
			auto iter = classes_.find(name);
			if (classes_.end() != iter)
				return iter->second;
		}

		// maybe in a dll, parse the objname, load the dll and retry
		auto p = wcsstr(name.c_str(), DLL_SEPARATOR);
		if (p)
		{
			std::wstring dllname(name.c_str(),p);
			// no need to support unload, let os to handle unload
			auto hdll = LoadLibraryW(dllname.c_str());
			if (hdll)
			{
				std::lock_guard<decltype(lock_)> l(lock_);
				auto iter = classes_.find(name);
				if (classes_.end() != iter)
					return iter->second;
			}
			else
			{
				assert(!"failed to load required dll");
			}
		}
		return {};
	}
};

// registration helper
#define REGISTER_OBJECT(objName, interfaceName, className) \
	static std::shared_ptr<void> objreg_##objName = \
		ObjBase::getInstance()->RegisterCls(objName,  \
		[](void** obj){ \
			auto p = new className; \
			*obj = p; \
			return static_cast<interfaceName*>(p);}, \
		[](void* p){delete static_cast<className*>(p);});

// declaration helper
#define DECLARE_VIRTUAL_GET_OBJECT(interfacename)								\
	static std::shared_ptr<interfacename> getObject(const wchar_t* objName)	\
	{return ObjBase::getInstance()->CreateObj<interfacename>(objName);}

#define DECLARE_GET_OBJECT(interfacename, objname)								\
	static std::shared_ptr<interfacename> getObject()							\
	{return ObjBase::getInstance()->CreateObj<interfacename>(objname);}

#define DECLARE_VIRTUAL_GET_INSTANCE(interfacename)								\
	static std::shared_ptr<interfacename> getInstance(const wchar_t* objname)	\
	{return ObjBase::getInstance()->CreateSingletonObj<interfacename>(objname);}

#define DECLARE_GET_INSTANCE(interfacename, objname)							\
	static std::shared_ptr<interfacename> getInstance()						\
	{return ObjBase::getInstance()->CreateSingletonObj<interfacename>(objname);}
