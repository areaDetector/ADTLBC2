#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <iocsh.h>

#include <ADDriver.h>

#include <visa.h>
#include <TLBC2.h>
#include <TLBC1_Calculations.h>

#include <epicsExport.h> // defines epicsExportSharedSymbols, do not move

static_assert(std::is_same_v<ViUInt16, epicsUInt16>);
static_assert(std::is_same_v<ViReal32, epicsFloat32>);
static_assert(std::is_same_v<ViReal64, epicsFloat64>);

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

            addAttributesFromScan(pImage, scan_data);

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

    void addAttributesFromScan(NDArray* image, TLBC1_Calculations &data) {
        getAttributes(image->pAttributeList);
        auto list = image->pAttributeList;

        list->add("BaseLevel", "Mean noise of the sensor", NDAttrFloat64,
                  &data.baseLevel);
        list->add("LightShieldedPixelMeanIntensity",
                  "Mean intensity of the light shielded pixels", NDAttrFloat64,
                  &data.lightShieldedPixelMeanIntensity);
        list->add("PeakPositionX", "Peak x pixel position", NDAttrUInt16,
                  &data.peakPositionX);
        list->add("PeakPositionY", "Peak y pixel position", NDAttrUInt16,
                  &data.peakPositionY);
        list->add("CentroidPositionX", "Centroid x pixel position",
                  NDAttrFloat32, &data.centroidPositionX);
        list->add("CentroidPositionY", "Centroid y pixel position",
                  NDAttrFloat32, &data.centroidPositionY);

        list->add("BeamWidthIsoX", "Beam width in X axis (ISO 11146-2)",
                  NDAttrFloat64, &data.beamWidthIsoX);
        list->add("BeamWidthIsoY", "Beam width in Y axis (ISO 11146-2)",
                  NDAttrFloat64, &data.beamWidthIsoY);
        list->add("BeamWidthIsoXSimple",
                  "Beam width in X axis for round profiles with "
                  "ellipticity > 87% (ISO 11146-2)",
                  NDAttrFloat64, &data.beamWidthIsoXSimple);
        list->add("BeamWidthIsoYSimple",
                  "Beam width in Y axis for round profiles with "
                  "ellipticity > 87% (ISO 11146-2)",
                  NDAttrFloat64, &data.beamWidthIsoYSimple);
        list->add("BeamWidthClipX",
                  "Horizontal beam width at clip level in pixel", NDAttrFloat32,
                  &data.beamWidthClipX);
        list->add("BeamWidthClipY",
                  "Vertical beam width at clip level in pixel", NDAttrFloat32,
                  &data.beamWidthClipY);
        list->add("EllipticityIso",
                  "Ellipticity of the beam width (ISO 11146-2)", NDAttrFloat64,
                  &data.ellipticityIso);
        list->add("AzimuthAngle",
                  "Azimuth angle measured clockwise (ISO 11146-2)",
                  NDAttrFloat64, &data.azimuthAngle);

        list->add("EllipseDiameterMin",
                  "Ellipse minor axis diameter in [pixel]", NDAttrFloat32,
                  &data.ellipseDiaMin);
        list->add("EllipseDiameterMax",
                  "Ellipse major axis diameter in [pixel]", NDAttrFloat32,
                  &data.ellipseDiaMax);
        list->add("EllipseDiameterMean",
                  "Ellipse diameter arithmetic mean value in [pixel]",
                  NDAttrFloat32, &data.ellipseDiaMean);
        list->add("EllipseOrientation", "Ellipse orientation angle in degree.",
                  NDAttrFloat32, &data.ellipseOrientation);
        list->add("EllipseEllipticity",
                  "Ellipse's ratio of minor to major axis diameter",
                  NDAttrFloat32, &data.ellipseEllipticity);
        list->add("EllipseEccentricity", "Ellipse's eccentricity",
                  NDAttrFloat32, &data.ellipseEccentricity);
        list->add("EllipseCenterX", "Ellipse center x pixel position",
                  NDAttrFloat32, &data.ellipseCenterX);
        list->add("EllipseCenterY", "Ellipse center y pixel position",
                  NDAttrFloat32, &data.ellipseCenterY);
        list->add("EllipseFitAmplitude",
                  "Ellipse amplitude in Fourier fit (in pixel)", NDAttrFloat32,
                  &data.ellipseFitAmplitude);
        list->add("EllipseRotAngleX", "Ellipse rotation angle in x",
                  NDAttrFloat32, &data.rotAngleEllipseX);
        list->add("EllipseRotAngleY", "Ellipse rotation angle in y",
                  NDAttrFloat32, &data.rotAngleEllipseY);
        list->add("EllipseWidthIsoX", "Ellipse width in x", NDAttrFloat32,
                  &data.ellipseWidthIsoX);
        list->add("EllipseWidthIsoY", "Ellipse width in y", NDAttrFloat32,
                  &data.ellipseWidthIsoY);

        list->add("TotalPower", "Total power in dBm", NDAttrFloat32,
                  &data.totalPower);
        list->add("PeakPowerDensity", "Peak power density in mW/um^2",
                  NDAttrFloat32, &data.peakPowerDensity);

        list->add("GaussianFitCentroidPositionX",
                  "Centroid x pixel position for the gaussian profile",
                  NDAttrFloat32, &data.gaussianFitCentroidPositionX);
        list->add("GaussianFitCentroidPositionY",
                  "Centroid y pixel position for the gaussian profile",
                  NDAttrFloat32, &data.gaussianFitCentroidPositionY);
        list->add("GaussianFitRatingX",
                  "Ratio of actual data to the gaussian fit of the x profile",
                  NDAttrFloat32, &data.gaussianFitRatingX);
        list->add("GaussianFitRatingY",
                  "Ratio of actual data to the gaussian fit of the y profile",
                  NDAttrFloat32, &data.gaussianFitRatingY);
        list->add("GaussianFitDiameterX", "Diameter for the profile X centroid",
                  NDAttrFloat32, &data.gaussianFitDiameterX);
        list->add("GaussianFitDiameterY", "Diameter for the profile Y centroid",
                  NDAttrFloat32, &data.gaussianFitDiameterY);

        list->add("CalcAreaCenterX", "Calculation area left border",
                  NDAttrFloat32, &data.calcAreaCenterX);
        list->add("CalcAreaCenterY", "Calculation area right border",
                  NDAttrFloat32, &data.calcAreaCenterY);
        list->add("CalcAreaWidth", "Calculation area width", NDAttrFloat32,
                  &data.calcAreaWidth);
        list->add("CalcAreaHeight", "Calculation area height", NDAttrFloat32,
                  &data.calcAreaHeight);
        list->add("CalcAreaAngle",
                  "Calculation area angle in degree (counterclock)",
                  NDAttrFloat64, &data.calcAreaAngle);
        list->add("CalcAreaLineOffset",
                  "Pixel inside the calculation area per line", NDAttrFloat64,
                  &data.calcAreaLineOffset);

        list->add("ProfilePeakValueX",
                  "Peak intensity value in the x profile (in calc area)",
                  NDAttrFloat32, &data.profilePeakValueX);
        list->add("ProfilePeakValueY",
                  "Peak intensity value in the y profile (in calc area)",
                  NDAttrFloat32, &data.profilePeakValueY);
        list->add(
            "ProfilePeakPosX",
            "Intensity profile peak intensity x pixel position (in calc area)",
            NDAttrUInt16, &data.profilePeakPosX);
        list->add(
            "ProfilePeakPosY",
            "Intensity profile peak intensity y pixel position (in calc area)",
            NDAttrUInt16, &data.profilePeakPosY);

        list->add(
            "EffectiveArea",
            "Area of an ideal flat top beam with same peak intensity in um^2",
            NDAttrFloat64, &data.effectiveArea);
        list->add("EffectiveBeamDiameter", "Effective beam diameter",
                  NDAttrFloat64, &data.effectiveBeamDiameter);

        list->add("Temperature", "Temperature", NDAttrFloat64,
                  &data.temperature);

        list->add("BesselFitRatingX", "Bessel fit rating in x profile",
                  NDAttrFloat32, &data.besselFitRatingX);
        list->add("BesselFitRatingY", "Bessel fit rating in y profile",
                  NDAttrFloat32, &data.besselFitRatingY);
    }

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
