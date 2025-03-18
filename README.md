ADTLBC2
=======

An [EPICS][epics] [areaDetector][] driver for the [ThorLabs BC207 and BC210
beam profilers][profilers] using the TLBC2 library provided by the
[manufacturer's SDK][SDK]. Since the provided SDK is Windows-only, this driver
does not work on Linux.

It has been tested with the [BC210CU/M][camera] camera, but should work for the
entire model family.

[profilers]: https://www.thorlabs.com/newgrouppage9.cfm?objectgroup_id=3483
[epics]: https://docs.epics-controls.org/en/latest/
[areaDetector]: https://github.com/areaDetector/areaDetector/blob/master/README.md
[camera]: https://www.thorlabs.com/thorproduct.cfm?partnumber=BC210CU/M
[SDK]: https://www.thorlabs.com/software_pages/ViewSoftwarePage.cfm?code=Beam
