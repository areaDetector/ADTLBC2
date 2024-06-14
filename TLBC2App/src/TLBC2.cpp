#include <ADDriver.h>

#include <visa.h>
#include <TLBC2.h>
#include <TLBC1_Calculations.h>

#include <epicsExport.h> // defines epicsExportSharedSymbols, do not move

class epicsShareClass ADTLBC2: public ADDriver {
    ViSession instr = TLBC2_INV_DEVICE_HANDLE;

public:
    ADTLBC2(const char *portName, const char *resource_name, int maxSizeX, int maxSizeY, int maxMemory):
        ADDriver(portName, 1, 0, 0, maxMemory,
                 0, 0,
                 ASYN_CANBLOCK, 1,
                 -1, -1)
    {
        TLBC2_init(resource_name, VI_TRUE, VI_TRUE, &instr);
    }
};
