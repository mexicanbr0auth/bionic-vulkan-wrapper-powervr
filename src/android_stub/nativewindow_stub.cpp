#include <vndk/window.h>
#include <errno.h>
#include <android/rect.h>

extern "C" {

AHardwareBuffer *
ANativeWindowBuffer_getHardwareBuffer(ANativeWindowBuffer *anwb)
{
   return nullptr;
}

void
AHardwareBuffer_acquire(AHardwareBuffer *buffer)
{
}

void
AHardwareBuffer_release(AHardwareBuffer *buffer)
{
}

void
AHardwareBuffer_describe(const AHardwareBuffer *buffer,
                         AHardwareBuffer_Desc *outDesc)
{
}

int
AHardwareBuffer_allocate(const AHardwareBuffer_Desc *desc,
                         AHardwareBuffer **outBuffer)
{
   return 0;
}

int
AHardwareBuffer_isSupported(const AHardwareBuffer_Desc* desc)
{
   return 0;
}

const native_handle_t *
AHardwareBuffer_getNativeHandle(const AHardwareBuffer *buffer)
{
   return NULL;
}

int
AHardwareBuffer_sendHandleToUnixSocket(const AHardwareBuffer* buffer,
                                       int socketFd)
{
   return 0;
}

void
ANativeWindow_acquire(ANativeWindow *window)
{
}

void
ANativeWindow_release(ANativeWindow *window)
{
}

int32_t
ANativeWindow_getFormat(ANativeWindow *window)
{
   return 0;
}

int
ANativeWindow_setSwapInterval(ANativeWindow *window, int interval)
{
   return 0;
}

int
ANativeWindow_query(const ANativeWindow *window,
                    ANativeWindowQuery query,
                    int *value)
{
   return 0;
}

int
ANativeWindow_dequeueBuffer(ANativeWindow *window,
                            ANativeWindowBuffer **buffer,
                            int *fenceFd)
{
   return 0;
}

int
ANativeWindow_queueBuffer(ANativeWindow *window,
                          ANativeWindowBuffer *buffer,
                          int fenceFd)
{
   return 0;
}

int ANativeWindow_cancelBuffer(ANativeWindow* window,
                               ANativeWindowBuffer* buffer,
                               int fenceFd) {
   return 0;
}

int
ANativeWindow_setUsage(ANativeWindow *window, uint64_t usage)
{
   return 0;
}

int
ANativeWindow_setSharedBufferMode(ANativeWindow *window,
                                  bool sharedBufferMode)
{
   return 0;
}

int
AHardwareBuffer_lock(AHardwareBuffer *buffer,
                     uint64_t usage,
                     int32_t fence,
                     const ARect *rect,
                     void **outVirtualAddress)
{
   return -ENOSYS;
}

int
AHardwareBuffer_unlock(AHardwareBuffer *buffer,
                       int32_t *fence)
{
   return -ENOSYS;
}
}
