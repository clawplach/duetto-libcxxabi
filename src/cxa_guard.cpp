//===---------------------------- cxa_guard.cpp ---------------------------===//
//
// Copyright (C) 2013 Alessandro Pignotti <alessandro@leaningtech.com>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// This file incorporates work covered by the following copyright and
// permission notice:
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "abort_message.h"

#ifndef __DUETTO__
#include <pthread.h>
#endif
#include <stdint.h>

/*
    This implementation must be careful to not call code external to this file
    which will turn around and try to call __cxa_guard_acquire reentrantly.
    For this reason, the headers of this file are as restricted as possible.
    Previous implementations of this code for __APPLE__ have used
    pthread_mutex_lock and the abort_message utility without problem.  This
    implementation also uses pthread_cond_wait which has tested to not be a
    problem.
*/

namespace __cxxabiv1
{

namespace
{

#if __arm__ || __DUETTO__

// A 32-bit, 4-byte-aligned static data value. The least significant 2 bits must
// be statically initialized to 0.
typedef uint32_t guard_type;

// Test the lowest bit.
inline bool is_initialized(guard_type* guard_object) {
    return (*guard_object) & 1;
}

inline void set_initialized(guard_type* guard_object) {
    *guard_object |= 1;
}

#else

typedef uint64_t guard_type;

bool is_initialized(guard_type* guard_object) {
    char* initialized = (char*)guard_object;
    return *initialized;
}

void set_initialized(guard_type* guard_object) {
    char* initialized = (char*)guard_object;
    *initialized = 1;
}

#endif

#ifndef __DUETTO__
pthread_mutex_t guard_mut = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  guard_cv  = PTHREAD_COND_INITIALIZER;
#endif

#if (defined(__APPLE__) || defined(__DUETTO__)) && !defined(__arm__)

typedef uint32_t lock_type;

#if __LITTLE_ENDIAN__

#ifdef __DUETTO__
inline
lock_type
get_lock(uint32_t x)
{
    return static_cast<lock_type>(x);
}

inline
void
set_lock(uint32_t& x, lock_type y)
{
    x = static_cast<uint32_t>(y);
}
#else // __DUETTO__
inline
lock_type
get_lock(uint64_t x)
{
    return static_cast<lock_type>(x >> 32);
}

inline
void
set_lock(uint64_t& x, lock_type y)
{
    x = static_cast<uint64_t>(y) << 32;
}
#endif // __DUETTO__

#else  // __LITTLE_ENDIAN__

inline
lock_type
get_lock(uint64_t x)
{
    return static_cast<lock_type>(x);
}

inline
void
set_lock(uint64_t& x, lock_type y)
{
    x = y;
}

#endif  // __LITTLE_ENDIAN__

#else  // !__APPLE__ || __arm__

typedef bool lock_type;

inline
lock_type
get_lock(uint64_t x)
{
    union
    {
        uint64_t guard;
        uint8_t lock[2];
    } f = {x};
    return f.lock[1] != 0;
}

inline
void
set_lock(uint64_t& x, lock_type y)
{
    union
    {
        uint64_t guard;
        uint8_t lock[2];
    } f = {0};
    f.lock[1] = y;
    x = f.guard;
}

inline
lock_type
get_lock(uint32_t x)
{
    union
    {
        uint32_t guard;
        uint8_t lock[2];
    } f = {x};
    return f.lock[1] != 0;
}

inline
void
set_lock(uint32_t& x, lock_type y)
{
    union
    {
        uint32_t guard;
        uint8_t lock[2];
    } f = {0};
    f.lock[1] = y;
    x = f.guard;
}

#endif  // __APPLE__

}  // unnamed namespace

extern "C"
{

int __cxa_guard_acquire(guard_type* guard_object)
{
    char* initialized = (char*)guard_object;
#ifndef __DUETTO__
    if (pthread_mutex_lock(&guard_mut))
        abort_message("__cxa_guard_acquire failed to acquire mutex");
#endif
    int result = *initialized == 0;
    if (result)
    {
#if defined(__APPLE__) && !defined(__arm__)
        const lock_type id = pthread_mach_thread_np(pthread_self());
        lock_type lock = get_lock(*guard_object);
        if (lock)
        {
            // if this thread set lock for this same guard_object, abort
            if (lock == id)
                abort_message("__cxa_guard_acquire detected deadlock");
            do
            {
                if (pthread_cond_wait(&guard_cv, &guard_mut))
                    abort_message("__cxa_guard_acquire condition variable wait failed");
                lock = get_lock(*guard_object);
            } while (lock);
            result = !is_initialized(guard_object);
            if (result)
                set_lock(*guard_object, id);
        }
        else
            set_lock(*guard_object, id);
#else  // !__APPLE__ || __arm__
#ifndef __DUETTO__
        while (get_lock(*guard_object))
            if (pthread_cond_wait(&guard_cv, &guard_mut))
                abort_message("__cxa_guard_acquire condition variable wait failed");
#endif
        result = *initialized == 0;
        if (result)
            set_lock(*guard_object, true);
#endif  // !__APPLE__ || __arm__
    }
#ifndef __DUETTO__
    if (pthread_mutex_unlock(&guard_mut))
        abort_message("__cxa_guard_acquire failed to release mutex");
#endif
    return result;
}

void __cxa_guard_release(guard_type* guard_object)
{
#ifndef __DUETTO__
    if (pthread_mutex_lock(&guard_mut))
        abort_message("__cxa_guard_release failed to acquire mutex");
#endif
    *guard_object = 0;
    set_initialized(guard_object);
#ifndef __DUETTO__
    if (pthread_mutex_unlock(&guard_mut))
        abort_message("__cxa_guard_release failed to release mutex");
    if (pthread_cond_broadcast(&guard_cv))
        abort_message("__cxa_guard_release failed to broadcast condition variable");
#endif
}

void __cxa_guard_abort(guard_type* guard_object)
{
#ifndef __DUETTO__
    if (pthread_mutex_lock(&guard_mut))
        abort_message("__cxa_guard_abort failed to acquire mutex");
#endif
    *guard_object = 0;
#ifndef __DUETTO__
    if (pthread_mutex_unlock(&guard_mut))
        abort_message("__cxa_guard_abort failed to release mutex");
    if (pthread_cond_broadcast(&guard_cv))
        abort_message("__cxa_guard_abort failed to broadcast condition variable");
#endif
}

}  // extern "C"

}  // __cxxabiv1
