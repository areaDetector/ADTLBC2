#include <cstring>

#include <ADDriver.h>

#include <visa.h>
#include <TLBC2.h>
#include <TLBC1_Calculations.h>

#include <epicsExport.h> // defines epicsExportSharedSymbols, do not move

class epicsShareClass ADTLBC2: public ADDriver {
    ViSession instr = TLBC2_INV_DEVICE_HANDLE;

public:
    ADTLBC2(const char *portName, int maxSizeX, int maxSizeY, int maxMemory):
        ADDriver(portName, 1, 0, 0, maxMemory,
                 0, 0,
                 ASYN_CANBLOCK, 1,
                 -1, -1)
    {
        auto handle_tlbc2_err = [](ViStatus err) {
            if (err != VI_SUCCESS)
                throw std::runtime_error("failed tblc2 function");
        };

        ViUInt32 device_count = 0;
        handle_tlbc2_err(TLBC2_get_device_count(VI_NULL, &device_count));

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
            resource_name));

        handle_tlbc2_err(TLBC2_init(resource_name, VI_TRUE, VI_TRUE, &instr));
    }
};
