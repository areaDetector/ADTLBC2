#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>

#include <iocsh.h>

#include <ADDriver.h>

#include <visa.h>
#include <TLBC2.h>
#include <TLBC1_Calculations.h>

#include <epicsExport.h> // defines epicsExportSharedSymbols, do not move

template<typename T>
class Parameter {
    std::function<ViStatus(ViSession, T*)> getter;
    std::function<ViStatus(ViSession, T)> setter;
    std::function<ViStatus(ViSession, T*, T*)> range_getter;

public:
    const std::string name;

    Parameter(const std::string name,
              std::function<ViStatus(ViSession, T*)> getter,
              std::function<ViStatus(ViSession, T)> setter,
              std::function<ViStatus(ViSession, T*, T*)> range_getter = {})
        : name(name), getter(getter), setter(setter), range_getter(range_getter) {}

    ViStatus get(ViSession instr, T& value)
    {
        return getter(instr, &value);
    }

    ViStatus set(ViSession instr, T value)
    {
        T min, max;

        if (range_getter) {
            ViStatus status = range_getter(instr, &min, &max);
            if (status != VI_SUCCESS)
                return status;

            /* TODO: handle this with DRVL and DRVH instead */
            if (value < min || value > max)
                throw std::range_error("value ouside range [" +
                                       std::to_string(min) + ", " +
                                       std::to_string(max) + "]");
        }

        return setter(instr, value);
    }
};

class epicsShareClass ADTLBC2: ADDriver, epicsThreadRunable {
    ViSession instr = TLBC2_INV_DEVICE_HANDLE;
    TLBC1_Calculations scan_data;
    ViUInt8 image_data[TLBC1_MAX_ROWS * TLBC1_MAX_COLUMNS * 2];

    epicsEvent start_acquire_event;
    epicsThread acq_thread;

    const std::unordered_map<int, Parameter<ViReal64>> params;

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
            auto item = params.find(function);

            if (item != params.end()) {
                asynStatus status = asynSuccess;
                auto param = item->second;

                try {
                    handle_tlbc2_err(param.set(instr, (ViReal64)value), "set_" + param.name);
                } catch (const std::runtime_error &err) {
                    // when failing to set, we still need to readback, so just
                    // report this and keep going
                    asynPrint(pasynUser, ASYN_TRACE_ERROR, err.what());

                    status = asynError;
                }

                handle_tlbc2_err(param.get(instr, readback), "get_" + param.name);

                setDoubleParam(function, (epicsFloat64)readback);

                callParamCallbacks();
                return status;
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
        acq_thread(*this, (std::string(portName) + "-acq").c_str(), epicsThreadGetStackSize(epicsThreadStackMedium), epicsThreadPriorityHigh),
        params({
            {ADAcquireTime, {"exposure_time", TLBC2_get_exposure_time, TLBC2_set_exposure_time, TLBC2_get_exposure_time_range}},
            {ADGain, {"gain", TLBC2_get_gain, TLBC2_set_gain, TLBC2_get_gain_range}},
        })
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
