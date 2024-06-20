#include <cstring>

#include <iocsh.h>

#include <ADDriver.h>

#include <visa.h>
#include <TLBC2.h>
#include <TLBC1_Calculations.h>

#include <epicsExport.h> // defines epicsExportSharedSymbols, do not move

class epicsShareClass ADTLBC2: ADDriver, epicsThreadRunable {
    ViSession instr = TLBC2_INV_DEVICE_HANDLE;
    TLBC1_Calculations scan_data;
    ViUInt8 image_data[TLBC1_MAX_ROWS * TLBC1_MAX_COLUMNS * 2];

    epicsEvent start_acquire_event;
    epicsThread acq_thread;

    asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value) override
    {
        const int function = pasynUser->reason;
        if (function == ADAcquire && value == 1) {
            start_acquire_event.trigger();
        }

        return ADDriver::writeInt32(pasynUser, value);
    }

    asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value) override
    {
        const int function = pasynUser->reason;
        ViReal64 readback;

        try {
            if (function == ADAcquireTime) {
                handle_tlbc2_err(TLBC2_set_exposure_time(instr, (ViReal64)value),
                                 "TBLC2_set_exposure_time");
                handle_tlbc2_err(TLBC2_get_exposure_time(instr, &readback),
                                 "TBLC2_get_exposure_time");

                setDoubleParam(ADAcquireTime, (epicsFloat64)readback);

                callParamCallbacks();
                return asynSuccess;
            }

            if (function == ADGain) {
                ViReal64 min, max, readback;

                /* TODO: fill this in DRVL and DRVH instead */
                handle_tlbc2_err(TLBC2_get_gain_range(instr, &min, &max), "get_gain_range");

                if (value < min || value > max)
                    return asynError;

                handle_tlbc2_err(TLBC2_set_gain(instr, (ViReal64)value),
                                 "set_gain");
                handle_tlbc2_err(TLBC2_get_gain(instr, &readback), "get_gain");

                setDoubleParam(ADGain, (epicsFloat64)readback);

                callParamCallbacks();
                return asynSuccess;
            }

            return ADDriver::writeFloat64(pasynUser, value);
        } catch (const std::runtime_error &err) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR, err.what());

            return asynError;
        }
    }

    void run() override
    {
        while (1) {
            start_acquire_event.wait();
            auto err = TLBC2_get_scan_data(instr, &scan_data);
            if (err != VI_SUCCESS || !scan_data.isValid)
                continue;

            ViUInt16 width, height;
            ViUInt8 bpp;
            err = TLBC2_get_image(instr, image_data, &width, &height, &bpp);
            if (err != VI_SUCCESS)
                continue;

            size_t dims[] = {width, height};
            /* TODO: should this be done under a lock? */
            auto pImage = this->pNDArrayPool->alloc(2, dims, bpp == 2 ? NDUInt16 : NDUInt8, 0, NULL);
            memcpy(pImage->pData, image_data, width * height * bpp);

            doCallbacksGenericPointer(pImage, NDArrayData, 0);

            lock();
            setIntegerParam(ADAcquire, 0);
            callParamCallbacks();
            unlock();
        }
    }

    void handle_tlbc2_err(ViStatus err, std::string function)
    {
        if (err == VI_SUCCESS)
            return;

        ViChar ebuf[TLBC2_ERR_DESCR_BUFFER_SIZE];
        TLBC2_error_message(instr, err, ebuf);
        throw std::runtime_error("TBLC2: " + function + ": " +
                                 std::string(ebuf) + "\n");
    };

public:
    ADTLBC2(const char *portName, int maxSizeX, int maxSizeY, int maxMemory, int reset):
        ADDriver(portName, 1, 0, 0, maxMemory,
                 0, 0,
                 ASYN_CANBLOCK, 1,
                 -1, -1),
        acq_thread(*this, (std::string(portName) + "-acq").c_str(), epicsThreadGetStackSize(epicsThreadStackMedium), epicsThreadPriorityHigh)
    {
        ViUInt32 device_count = 0;
        handle_tlbc2_err(TLBC2_get_device_count(VI_NULL, &device_count),
                         "get_device_count");

        if (device_count < 1)
            throw std::runtime_error("no available devices");

        ViBoolean available;
        ViChar resource_name[256];

        /* XXX: we always use the first available device */
        handle_tlbc2_err(TLBC2_get_device_information(
            VI_NULL, /* vi */
            0, /* device index */
            VI_NULL, /* device manufacturer */
            VI_NULL, /* model name */
            VI_NULL, /* serial number */
            &available,
            resource_name), "get_device_information");

        handle_tlbc2_err(TLBC2_init(
            resource_name,
            VI_TRUE, /* identification query */
            reset ? VI_TRUE : VI_FALSE, /* reset device */
            &instr), "init");

        acq_thread.start();
    }
};

static const iocshArg arg0 = {"portName", iocshArgString};
static const iocshArg arg1 = {"maxX", iocshArgInt};
static const iocshArg arg2 = {"maxY", iocshArgInt};
static const iocshArg arg3 = {"maxMemory", iocshArgInt};
static const iocshArg arg4 = {"reset", iocshArgInt};

static const iocshArg *const args[] = {&arg0, &arg1, &arg2, &arg3, &arg4};

static const iocshFuncDef configTLBC2 = {"TLBC2Config", 5, args};
static void configTLBC2CallFunc(const iocshArgBuf *args)
{
    new ADTLBC2(args[0].sval, args[1].ival, args[2].ival, args[3].ival, args[4].ival);
}

static void TLBC2Register()
{
    iocshRegister(&configTLBC2, configTLBC2CallFunc);
}

extern "C" {
    epicsExportRegistrar(TLBC2Register);
}
