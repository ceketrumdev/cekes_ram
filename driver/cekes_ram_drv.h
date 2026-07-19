#ifndef CEKES_RAM_DRV_H
#define CEKES_RAM_DRV_H

#include <ntddk.h>
#include <wdf.h>
#include "shared_ioctl.h"

#define DEVICE_NAME_STRING     L"\\Device\\CekesRamDevice"
#define SYMBOLIC_NAME_STRING   L"\\DosDevices\\CekesRamLink"

typedef struct _DEVICE_CONTEXT {
    WDFDEVICE Device;
    PMDL LockedMdl;
    PVOID LockedUserVa;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

EVT_WDF_DRIVER_DEVICE_ADD EvtDriverDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;
EVT_WDF_DEVICE_FILE_CREATE EvtFileCreate;
EVT_WDF_FILE_CLOSE EvtFileClose;
EVT_WDF_FILE_CLEANUP EvtFileCleanup;

#endif /* CEKES_RAM_DRV_H */
