#ifndef SHARED_IOCTL_H
#define SHARED_IOCTL_H

#if defined(_WIN32) || defined(_WIN64)
#include <winioctl.h>
#endif

#define CEKES_DEVICE_TYPE 0x8000

#define IOCTL_CEKES_GET_PHYSICAL_ADDR \
    CTL_CODE(CEKES_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_CEKES_READ_TSOD \
    CTL_CODE(CEKES_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_CEKES_LOCK_PHYSICAL_PAGE \
    CTL_CODE(CEKES_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_CEKES_UNLOCK_PHYSICAL_PAGE \
    CTL_CODE(CEKES_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _CEKES_VIRT_TO_PHYS_REQ {
    void* VirtualAddress;
    unsigned __int64 PhysicalAddress;
    unsigned int Status;
} CEKES_VIRT_TO_PHYS_REQ, *PCEKES_VIRT_TO_PHYS_REQ;

typedef struct _CEKES_TSOD_READ_REQ {
    unsigned int CpuIndex;
    unsigned int DimmIndex;
    unsigned int TemperatureC;
    unsigned __int64 MsrPkgTemp;
    unsigned int Status;
} CEKES_TSOD_READ_REQ, *PCEKES_TSOD_READ_REQ;

typedef struct _CEKES_LOCK_PAGE_REQ {
    unsigned __int64 LengthBytes;
    void* UserVirtualAddress;
    unsigned __int64 PhysicalBaseAddress;
    unsigned int Status;
} CEKES_LOCK_PAGE_REQ, *PCEKES_LOCK_PAGE_REQ;

#endif /* SHARED_IOCTL_H */
