/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "PreRTS.h"
import engine.debug;
#include "GameClient/ClientInstance.h"
import engine.platform.application;

#define GENERALS_GUID "685EAFF2-3216-4265-B047-251C5F4B82F3"

namespace rts
{
bool ClientInstance::s_initialized = false;
UnsignedInt ClientInstance::s_instanceIndex = 0;

#if defined(RTS_MULTI_INSTANCE)
Bool ClientInstance::s_isMultiInstance = true;
#else
Bool ClientInstance::s_isMultiInstance = false;
#endif

bool ClientInstance::initialize(engine::platform::IApplicationService& application)
{
	if (isInitialized())
	{
		return true;
	}

	if (!s_isMultiInstance && s_instanceIndex == 0 &&
		!application.acquire_single_instance(GENERALS_GUID))
		return false;
	s_initialized = true;
	return true;
}

bool ClientInstance::isInitialized()
{
	return s_initialized;
}

bool ClientInstance::isMultiInstance()
{
	return s_isMultiInstance;
}

void ClientInstance::setMultiInstance(bool v)
{
	if (isInitialized())
	{
		engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "ClientInstance::setMultiInstance(%d) - cannot set multi instance after initialization", (int)v);
		return;
	}
	s_isMultiInstance = v;
}

void ClientInstance::skipPrimaryInstance()
{
	if (isInitialized())
	{
		engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "ClientInstance::skipPrimaryInstance() - cannot skip primary instance after initialization");
		return;
	}
	s_instanceIndex = 1;
}

UnsignedInt ClientInstance::getInstanceIndex()
{
	if (!(isInitialized())) engine::debug::log_error("ClientInstance::isInitialized() failed");
	return s_instanceIndex;
}

UnsignedInt ClientInstance::getInstanceId()
{
	return getInstanceIndex() + 1;
}

const char* ClientInstance::getFirstInstanceName()
{
	return GENERALS_GUID;
}

} // namespace rts
