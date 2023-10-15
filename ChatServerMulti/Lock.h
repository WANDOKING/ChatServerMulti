#pragma once
#pragma comment(lib, "Synchronization.lib")
#include <Windows.h>

class SrwLock
{
public:
    inline SrwLock() { ::InitializeSRWLock(&mLock); }

#pragma warning(push)
#pragma warning(disable: 26110)
    inline void Lock(void) { ::AcquireSRWLockExclusive(&mLock); }
    inline void Unlock(void) { ::ReleaseSRWLockExclusive(&mLock); }
#pragma warning(pop)

    inline void ReadLock(void) { ::AcquireSRWLockShared(&mLock); }
    inline void ReadUnlock(void) { ::ReleaseSRWLockShared(&mLock); }

private:
    SRWLOCK mLock;
};

class CriticalSectionLock
{
public:
    inline CriticalSectionLock() { ::InitializeCriticalSection(&mLock); }
    inline ~CriticalSectionLock() { ::DeleteCriticalSection(&mLock); }

    inline void Lock(void) { ::EnterCriticalSection(&mLock); }
    inline void Unlock(void) { ::LeaveCriticalSection(&mLock); }

private:
    CRITICAL_SECTION mLock;
};

class WaitOnAddressLock
{
public:
    inline void Lock(void)
    {
        LONG compare = 1;

        for (;;)
        {
            if (InterlockedExchange(&mLock, 1))
            {
                ::WaitOnAddress(&mLock, &compare, sizeof(mLock), INFINITE);
            }
            else
            {
                break;
            }
        }
    }

    inline void Unlock(void)
    {
        mLock = 0;
        ::WakeByAddressSingle(&mLock);
    }

private:
    LONG mLock;
};

class SpinLock
{
public:
    inline void Lock(void) { while (InterlockedExchange(&mLock, 1)); }
    inline void Unlock(void) { mLock = 0; }

private:
    LONG mLock;
};