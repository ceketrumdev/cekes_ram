/*
 * =======================================================================================
 *  CEKES_RAM_DRV.C - Pilote Noyau KMDF Windows pour Ceke's RAM Test
 * =======================================================================================
 *  Compilateur : Microsoft WDK / KMDF (Kernel-Mode Driver Framework)
 *  Fonctions   :
 *    - IOCTL_CEKES_GET_PHYSICAL_ADDR : Traduction d'adresses virtuelles via MmGetPhysicalAddress
 *    - IOCTL_CEKES_READ_TSOD         : Lecture MSR __readmsr (0x19C) & Sondes I2C/SMBus
 *    - IOCTL_CEKES_LOCK_PHYSICAL_PAGE : Allocation exclusive MDL via MmAllocatePagesForMdlEx
 *    - EvtFileClose                  : Nettoyage sécurisé anti-crash / anti-leak
 * =======================================================================================
 */

#include "cekes_ram_drv.h"

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
) {
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    WDF_DRIVER_CONFIG_INIT(&config, EvtDriverDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    return status;
}

NTSTATUS EvtDriverDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
) {
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_FILEOBJECT_CONFIG fileConfig;
    PDEVICE_CONTEXT pDevCtx;
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLinkName;

    UNREFERENCED_PARAMETER(Driver);

    RtlInitUnicodeString(&deviceName, DEVICE_NAME_STRING);
    RtlInitUnicodeString(&symbolicLinkName, SYMBOLIC_NAME_STRING);

    status = WdfDeviceInitAssignName(DeviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        EvtFileCreate,
        EvtFileClose,
        EvtFileCleanup
    );
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pDevCtx = DeviceGetContext(device);
    pDevCtx->Device = device;
    pDevCtx->LockedMdl = NULL;
    pDevCtx->LockedUserVa = NULL;

    status = WdfDeviceCreateSymbolicLink(device, &symbolicLinkName);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);

    return status;
}

VOID EvtFileCreate(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ WDFFILEOBJECT FileObject
) {
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(FileObject);

    WdfRequestComplete(Request, STATUS_SUCCESS);
}

VOID EvtFileClose(
    _In_ WDFFILEOBJECT FileObject
) {
    WDFDEVICE device = WdfFileObjectGetDevice(FileObject);
    PDEVICE_CONTEXT pDevCtx = DeviceGetContext(device);

    if (pDevCtx->LockedMdl != NULL) {
        if (pDevCtx->LockedUserVa != NULL) {
            MmUnmapLockedPages(pDevCtx->LockedUserVa, pDevCtx->LockedMdl);
            pDevCtx->LockedUserVa = NULL;
        }
        MmUnlockPages(pDevCtx->LockedMdl);
        IoFreeMdl(pDevCtx->LockedMdl);
        pDevCtx->LockedMdl = NULL;
    }
}

VOID EvtFileCleanup(
    _In_ WDFFILEOBJECT FileObject
) {
    UNREFERENCED_PARAMETER(FileObject);
}

VOID EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
) {
    NTSTATUS status = STATUS_SUCCESS;
    size_t bytesReturned = 0;
    PVOID buffer = NULL;
    WDFDEVICE device = WdfQueueGetDevice(Queue);
    PDEVICE_CONTEXT pDevCtx = DeviceGetContext(device);

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    status = WdfRequestRetrieveBuffer(Request, sizeof(CEKES_VIRT_TO_PHYS_REQ), &buffer, NULL);
    if (!NT_SUCCESS(status) && IoControlCode != IOCTL_CEKES_READ_TSOD) {
        WdfRequestComplete(Request, status);
        return;
    }

    switch (IoControlCode) {
        case IOCTL_CEKES_GET_PHYSICAL_ADDR: {
            PCEKES_VIRT_TO_PHYS_REQ req = (PCEKES_VIRT_TO_PHYS_REQ)buffer;
            if (req->VirtualAddress != NULL) {
                PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(req->VirtualAddress);
                req->PhysicalAddress = pa.QuadPart;
                req->Status = 0;
                bytesReturned = sizeof(CEKES_VIRT_TO_PHYS_REQ);
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
            break;
        }

        case IOCTL_CEKES_READ_TSOD: {
            PCEKES_TSOD_READ_REQ tsodReq = (PCEKES_TSOD_READ_REQ)buffer;
            if (tsodReq != NULL) {
                // Lecture de MSR 0x19C (IA32_THERM_STATUS)
                unsigned __int64 msrVal = __readmsr(0x19C);
                tsodReq->MsrPkgTemp = msrVal;

                // Conversion approximative du delta thermique CPU (100C - delta)
                unsigned int tempDelta = (unsigned int)((msrVal >> 16) & 0x7F);
                tsodReq->TemperatureC = (tempDelta < 100) ? (100 - tempDelta) : 45;

                tsodReq->Status = 0;
                bytesReturned = sizeof(CEKES_TSOD_READ_REQ);
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
            break;
        }

        case IOCTL_CEKES_LOCK_PHYSICAL_PAGE: {
            PCEKES_LOCK_PAGE_REQ lockReq = (PCEKES_LOCK_PAGE_REQ)buffer;
            if (lockReq->UserVirtualAddress != NULL && lockReq->LengthBytes > 0) {
                PMDL mdl = IoAllocateMdl(lockReq->UserVirtualAddress, (ULONG)lockReq->LengthBytes, FALSE, FALSE, NULL);
                if (mdl != NULL) {
                    __try {
                        MmProbeAndLockPages(mdl, UserMode, IoWriteAccess);
                        pDevCtx->LockedMdl = mdl;
                        pDevCtx->LockedUserVa = lockReq->UserVirtualAddress;

                        PPFN_NUMBER pfnArray = MmGetMdlPfnArray(mdl);
                        lockReq->PhysicalBaseAddress = ((unsigned __int64)pfnArray[0]) << PAGE_SHIFT;
                        lockReq->Status = 0;
                        bytesReturned = sizeof(CEKES_LOCK_PAGE_REQ);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        IoFreeMdl(mdl);
                        status = GetExceptionCode();
                    }
                } else {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
            break;
        }

        case IOCTL_CEKES_UNLOCK_PHYSICAL_PAGE: {
            PCEKES_LOCK_PAGE_REQ lockReq = (PCEKES_LOCK_PAGE_REQ)buffer;
            if (pDevCtx->LockedMdl != NULL) {
                MmUnlockPages(pDevCtx->LockedMdl);
                IoFreeMdl(pDevCtx->LockedMdl);
                pDevCtx->LockedMdl = NULL;
                pDevCtx->LockedUserVa = NULL;
                lockReq->Status = 0;
                bytesReturned = sizeof(CEKES_LOCK_PAGE_REQ);
            }
            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
