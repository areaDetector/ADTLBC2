ADTLBC2 Release Notes
=====================

unreleased
----------

* Multiple/Continuous acquisition mode support (ImageMode PV)
* ArrayCounter PV now holds the total number of images taken since IOC boot
* BC210CU.template can be used for BC210/CU specific values and limits
* ADTLBC2.template can now be used with any of the BC207/BC210 detector models

v0.2.0 (February 26, 2025)
--------------------------

* Add saturation attribute
* Added "_RBV" suffix to $(P)$(R)AmbientLightCorrectionStatus PV
* $(P)$(R)ComputeAmbientLightCorrection now reflects when it's done computing
* Minor improvements: Avoid memory leaks of NDArrays, mitigate the risk of
format injection in error messages, improved locking.

v0.1.0 (July 23, 2024)
---------------------

* Initial release
* Supported features:
  - Single image acquisition
  - Basic device parameter settings, including:
    - Exposure time
    - Auto exposure
    - Attenuation
    - Wavelength
    - Gain
    - Clip level
  - ROI
  - Temperature reading
  - Ambient light correction
  - Beam statistics export as NDArray attributes
  - Save data using the NDFileHDF5 plugin
