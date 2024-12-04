#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <iocsh.h>

#include <ADDriver.h>

#include <visa.h>
#include <TLBC2.h>
#include <TLBC1_Calculations.h>

#include <epicsExport.h> // defines epicsExportSharedSymbols, do not move

static_assert(std::is_same_v<ViUInt16, epicsUInt16>);
static_assert(std::is_same_v<ViReal32, epicsFloat32>);
static_assert(std::is_same_v<ViReal64, epicsFloat64>);


/* Define these as vendor headers do not provide them */
enum ambient_light_correction_status {
    AMBIENT_LIGHT_CORRECTION_AVAILABLE,
    AMBIENT_LIGHT_CORRECTION_NEVER_RUN,
    AMBIENT_LIGHT_CORRECTION_FAILED,
};

template<typename T, typename V>
std::function<ViStatus(ViSession, T*)> create_getter_wrapper(std::function<ViStatus(ViSession, V*)> getter)
{
    return [getter](ViSession session, T *value) {
        V tmp = 0;
        auto rv = getter(session, &tmp);
        *value = tmp;
        return rv;
    };
}

template<typename T, typename V>
std::function<ViStatus(ViSession, T)> create_setter_wrapper(std::function<ViStatus(ViSession, V)> setter)
{
    return [setter](ViSession session, T value) {
        return setter(session, (V)value);
    };
}

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

    std::unordered_map<int, std::variant<Parameter<ViInt32>, Parameter<ViReal64>>> params;

    int BCAmbientLightCorrection;
    int BCAmbientLightCorrectionStatus;
    int BCAttenuation;
    int BCAutoExposure;
    int BCAutoCalcAreaClipLevel;
    int BCBeamWidthX;
    int BCBeamWidthY;
    int BCCentroidX;
    int BCCentroidY;
    int BCClipLevel;
    int BCComputeAmbientLightCorrection;
    int BCSaturation;
    int BCWavelength;

    template<typename T>
    asynStatus writeParam(asynUser *user, Parameter<T> &param, T value, T &readback) {
        asynStatus status = asynSuccess;

        try {
            handle_tlbc2_err(param.set(instr, value), "set_" + param.name);
        } catch (const std::runtime_error &err) {
            // when failing to set, we still need to readback, so just
            // report this and keep going
            asynPrint(user, ASYN_TRACE_ERROR, err.what());

            status = asynError;
        }

        handle_tlbc2_err(param.get(instr, readback), "get_" + param.name);

        return status;
    }

    template<typename T>
    void readbackParam(const int asyn_param, Parameter<T> &param) {
        T readback;

        handle_tlbc2_err(param.get(instr, readback), "get_" + param.name);
        if constexpr (std::is_same_v<T, ViInt32>) {
            setIntegerParam(asyn_param, readback);
        } else {
            setDoubleParam(asyn_param, readback);
        }
    }

    asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value) override
    {
        const int function = pasynUser->reason;

        auto item = params.find(function);

        if (item != params.end()) {
            auto param = std::get<Parameter<ViInt32>>(item->second);
            ViInt32 readback;

            asynStatus status =
                writeParam<ViInt32>(pasynUser, param, value, readback);

            setIntegerParam(function, readback);
            callParamCallbacks();
            return status;
        } else if (function == ADAcquire && value == 1) {
            start_acquire_event.trigger();
        } else if (function == ADSizeX || function == ADSizeY ||
                   function == ADMinX || function == ADMinY) {
            return writeROI(pasynUser, value);
        } else if (function == BCComputeAmbientLightCorrection && value == 1) {
            return runAmbientLightCorrection(pasynUser);
        }

        return ADDriver::writeInt32(pasynUser, value);
    }

    asynStatus runAmbientLightCorrection(asynUser *user)
    {
        ViUInt8 mode;

        try {
            handle_tlbc2_err(
                TLBC2_get_ambient_light_correction_status(instr, &mode),
                "get_ambient_light_correction_status");

            if (mode == AMBIENT_LIGHT_CORRECTION_NEVER_RUN ||
                mode == AMBIENT_LIGHT_CORRECTION_FAILED) {
                setIntegerParam(BCComputeAmbientLightCorrection, 1);
                callParamCallbacks();

                handle_tlbc2_err(TLBC2_run_ambient_light_correction(instr),
                                 "run_ambient_light_correction");
            }
            setIntegerParam(BCComputeAmbientLightCorrection, 0);
            setIntegerParam(BCAmbientLightCorrectionStatus, 1);
            callParamCallbacks();
        } catch (const std::runtime_error &err) {
            asynPrint(user, ASYN_TRACE_ERROR, err.what());

            return asynError;
        }

        return asynSuccess;
    }

    asynStatus writeROI(asynUser *user, int value)
    {
        int sizex, sizey, minx, miny;
        asynStatus status = asynSuccess;
        const int param = user->reason;

        getIntegerParam(ADSizeX, &sizex);
        getIntegerParam(ADSizeY, &sizey);
        getIntegerParam(ADMinX, &minx);
        getIntegerParam(ADMinY, &miny);

        if (param == ADSizeX)
            sizex = value;
        else if (param == ADSizeY)
            sizey = value;
        else if (param == ADMinX)
            minx = value;
        else if (param == ADMinY)
            miny = value;

        try {
            ViBoolean automatic;
            ViUInt8 form;
            epicsInt32 maxSizeX, maxSizeY;

            getIntegerParam(ADMaxSizeX, &maxSizeX);
            getIntegerParam(ADMaxSizeY, &maxSizeY);

            handle_tlbc2_err(
                TLBC2_get_calculation_area_mode(instr, &automatic, &form),
                "get_calculation_area_mode");

            handle_tlbc2_err(TLBC2_set_calculation_area_mode(instr, VI_ON, 0),
                             "set_calculation_area_mode");

            /* resetting the user calculation area is necessary to avoid
             * triggering a segfault in the library code */
            handle_tlbc2_err(
                TLBC2_set_user_calculation_area(instr, 0, 0, maxSizeX, maxSizeY, 0),
                "set_user_calculation_area");

            handle_tlbc2_err(TLBC2_set_roi(instr, (ViUInt16)minx,
                                           (ViUInt16)miny, (ViUInt16)sizex,
                                           (ViUInt16)sizey),
                             "set_roi");

            handle_tlbc2_err(
                TLBC2_set_calculation_area_mode(instr, automatic, form),
                "set_calculation_area_mode");

        } catch (const std::runtime_error &err) {
            asynPrint(user, ASYN_TRACE_ERROR, err.what());

            status = asynError;
        }

        try {
            ViUInt16 left, top, width, height;

            handle_tlbc2_err(TLBC2_get_roi(instr, &left, &top, &width, &height),
                             "get_roi");

            setIntegerParam(ADMinX, left);
            setIntegerParam(ADMinY, top);
            setIntegerParam(ADSizeX, width);
            setIntegerParam(ADSizeY, height);

            callParamCallbacks();
        } catch (const std::runtime_error &err) {
            asynPrint(user, ASYN_TRACE_ERROR, err.what());

            return asynError;
        }

        return status;
    }

    asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value) override
    {
        const int function = pasynUser->reason;
        ViReal64 readback;

        try {
            auto item = params.find(function);

            if (item != params.end()) {
                auto param = std::get<Parameter<ViReal64>>(item->second);

                asynStatus status =
                    writeParam<ViReal64>(pasynUser, param, value, readback);

                setDoubleParam(function, readback);

                if (function == ADAcquireTime)
                    setIntegerParam(BCAutoExposure, 0);

                callParamCallbacks();
                return status;
            }

            return ADDriver::writeFloat64(pasynUser, value);
        } catch (const std::runtime_error &err) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR, err.what());

            return asynError;
        }
    }

    asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value)
    {
        const int function = pasynUser->reason;

        if (function == ADTemperatureActual) {
            auto item = params.find(function);

            auto param = std::get<Parameter<ViReal64>>(item->second);

            readbackParam<ViReal64>(function, param);
            readbackParam<ViReal64>(ADTemperature, param);
            callParamCallbacks();
        }

        return ADDriver::readFloat64(pasynUser, value);
    }

    void run() override
    {
        while (1) {
            start_acquire_event.wait();
            lock();
            setIntegerParam(ADNumImagesCounter, 0);
            callParamCallbacks();
            unlock();

            auto err = TLBC2_request_new_measurement(instr);
            if (err != VI_SUCCESS)
                continue;

            err = TLBC2_get_scan_data(instr, &scan_data);
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
            updateCounters();
            readAcquireTime();
            updateParamsWithCalculations(scan_data);

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

    void createParameters() {
        createParam("AMBIENT_LIGHT_CORRECTION", asynParamInt32,
                    &BCAmbientLightCorrection);

        auto ambient_light_correction_getter =
            create_getter_wrapper<ViInt32, ViUInt8>(
                TLBC2_get_ambient_light_correction_mode);
        auto ambient_light_correction_setter =
            create_setter_wrapper<ViInt32, ViUInt8>(
                TLBC2_set_ambient_light_correction_mode);
        auto ambient_light_correction_param = Parameter<ViInt32>(
            "ambient_light_correction_mode", ambient_light_correction_getter,
            ambient_light_correction_setter);

        params.insert(
            {BCAmbientLightCorrection, ambient_light_correction_param});

        createParam("AMBIENT_LIGHT_CORRECTION_STATUS", asynParamInt32,
                    &BCAmbientLightCorrectionStatus);

        createParam("ATTENUATION", asynParamFloat64, &BCAttenuation);
        params.insert({BCAttenuation,
                       Parameter<ViReal64>("attenuation", TLBC2_get_attenuation,
                                           TLBC2_set_attenuation)});

        createParam("AUTO_EXPOSURE", asynParamInt32, &BCAutoExposure);

        auto auto_exposure_getter =
            create_getter_wrapper<ViInt32, ViBoolean>(TLBC2_get_auto_exposure);
        auto auto_exposure_setter =
            create_setter_wrapper<ViInt32, ViBoolean>(TLBC2_set_auto_exposure);
        auto auto_exposure_param = Parameter<ViInt32>(
            "auto_exposure", auto_exposure_getter, auto_exposure_setter);

        params.insert({BCAutoExposure, auto_exposure_param});

        createParam("AUTO_CALC_AREA_CLIP_LEVEL", asynParamFloat64, &BCAutoCalcAreaClipLevel);
        params.insert(
            {BCAutoCalcAreaClipLevel,
             Parameter<ViReal64>("auto_calculation_area_clip_level",
                                 TLBC2_get_auto_calculation_area_clip_level,
                                 TLBC2_set_auto_calculation_area_clip_level)});

        createParam("BEAM_WIDTH_X", asynParamFloat64, &BCBeamWidthX);
        createParam("BEAM_WIDTH_Y", asynParamFloat64, &BCBeamWidthY);

        createParam("CENTROID_X", asynParamFloat64, &BCCentroidX);
        createParam("CENTROID_Y", asynParamFloat64, &BCCentroidY);

        createParam("CLIP_LEVEL", asynParamFloat64, &BCClipLevel);
        params.insert({BCClipLevel,
                       Parameter<ViReal64>("clip_level", TLBC2_get_clip_level,
                                           TLBC2_set_clip_level)});

        createParam("COMPUTE_AMBIENT_LIGHT_CORRECTION", asynParamInt32,
                    &BCComputeAmbientLightCorrection);

        createParam("SATURATION", asynParamFloat64, &BCSaturation);

        createParam("WAVELENGTH", asynParamFloat64, &BCWavelength);
        params.insert({BCWavelength,
                       Parameter<ViReal64>("wavelength", TLBC2_get_wavelength,
                                           TLBC2_set_wavelength,
                                           TLBC2_get_wavelength_range)});
    }

    void readParameters() {
        for (auto &[id, param] : params) {
            try {
                if (std::holds_alternative<Parameter<ViInt32>>(param))
                    readbackParam<ViInt32>(id, std::get<Parameter<ViInt32>>(param));
                else
                    readbackParam<ViReal64>(id, std::get<Parameter<ViReal64>>(param));
            } catch (std::runtime_error &err) {
                std::cerr << err.what() << std::endl;
            }
        }

        try {
            ViUInt16 left, top, width, height;

            handle_tlbc2_err(TLBC2_get_roi(instr, &left, &top, &width, &height),
                             "get_roi");

            setIntegerParam(ADMinX, left);
            setIntegerParam(ADMinY, top);
            setIntegerParam(ADSizeX, width);
            setIntegerParam(ADSizeY, height);
        } catch (const std::runtime_error &err) {
            std::cerr << err.what() << std::endl;
        }

        callParamCallbacks();
    }

    void updateCounters()
    {
        epicsInt32 num_images;
        getIntegerParam(ADNumImagesCounter, &num_images);
        setIntegerParam(ADNumImagesCounter, num_images + 1);
    }

    void readAcquireTime() {
        int auto_exposure;
        getIntegerParam(BCAutoExposure, &auto_exposure);

        if (auto_exposure) {
            try {
                ViReal64 exposure_time;
                auto param = std::get<Parameter<ViReal64>>(
                    params.find(ADAcquireTime)->second);

                handle_tlbc2_err(param.get(instr, exposure_time),
                                 "get_" + param.name);

                setDoubleParam(ADAcquireTime, exposure_time);
            } catch (const std::runtime_error &err) {
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, err.what());
            }
        }
    }

    void updateParamsWithCalculations(TLBC1_Calculations &data)
    {
        setDoubleParam(BCBeamWidthX, data.beamWidthClipX);
        setDoubleParam(BCBeamWidthY, data.beamWidthClipY);
        setDoubleParam(BCCentroidX, data.centroidPositionX);
        setDoubleParam(BCCentroidY, data.centroidPositionY);
        setDoubleParam(BCSaturation, data.saturation);
    }

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
            {ADAcquireTime, Parameter<ViReal64>("exposure_time", TLBC2_get_exposure_time, TLBC2_set_exposure_time, TLBC2_get_exposure_time_range)},
            {ADGain, Parameter<ViReal64>("gain", TLBC2_get_gain, TLBC2_set_gain, TLBC2_get_gain_range)},
            {ADTemperatureActual, Parameter<ViReal64>("temperature", TLBC2_get_temperature, {})}
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

        createParameters();

        setIntegerParam(ADMaxSizeX, maxSizeX);
        setIntegerParam(ADMaxSizeY, maxSizeY);
        setIntegerParam(BCAmbientLightCorrectionStatus, 0);

        readParameters();

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
