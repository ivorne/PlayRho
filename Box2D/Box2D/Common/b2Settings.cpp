/*
* Copyright (c) 2006-2009 Erin Catto http://www.box2d.org
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/

#include <Box2D/Common/b2Settings.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

b2Version b2_version = {2, 3, 2};

// Memory allocators. Modify these to use your own allocator.
void* b2Alloc(b2_size_t size)
{
	b2Assert(size >= 0);
	return std::malloc(size);
}

void* b2Realloc(void* ptr, b2_size_t new_size)
{
	b2Assert(new_size >= 0);
	return std::realloc(ptr, new_size);
}

void b2Free(void* mem)
{
	std::free(mem);
}

// You can modify this to use your logging facility.
void b2Log(const char* string, ...)
{
	va_list args;
	va_start(args, string);
	std::vprintf(string, args);
	va_end(args);
}
