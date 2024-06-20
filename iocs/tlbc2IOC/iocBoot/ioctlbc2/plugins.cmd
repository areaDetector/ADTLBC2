# Area Detector plugin configuration
#
# The following parameters must be defined before loading this configuration:
#
# $(PREFIX)
# Prefix for all records.
#
# $(PORT)
# The port name for the detector.
#
# $(MAX_IMAGE_WIDTH)
# The maximum image width.
# Used to set the maximum size for row profiles in the NDPluginStats plugin.
#
# $(MAX_IMAGE_HEIGHT)
# The maximum image height.
# Used to set the maximum size for column profiles in the NDPluginStats plugin.
#
# $(MAX_IMAGE_PIXELS)
# The maximum number of pixels to be sent through channel access through
# NDPluginStdArrays.
#
# Optional parameters:
#
# $(IMAGE_ASYN_TYPE)
# Value of the DTYP field of the waveform record which defines the underlying
# asyn datatype.
# This should be consistent with IMAGE_WAVEFORM_TYPE.
# Defaults to Int16.
#
# $(IMAGE_WAVEFORM_TYPE)
# Data type of the waveform values themselves.
# This should be consistent with IMAGE_ASYN_TYPE.
# Defaults to USHORT.
#
# $(QSIZE)
# The queue size for all plugins.
# Defaults to 20
#
# $(QSIZE_HDF5)
# Queue size for HDF5 plugin.
# Defaults to 50
#
# $(NCHANS)
# The maximum number of time series points in the NDPluginStats plugin.
# Defaults to 2048.
#
# $(MAX_THREADS)
# The maximum number of threads for plugins which can run in multiple threads.
# Defaults to 4.

# Create Channel Access conversion plugin
NDStdArraysConfigure("Image1", $(QSIZE=20), 0, $(PORT), 0, 0, 0, 0, $(MAX_THREADS=4))
dbLoadRecords("NDStdArrays.template", "P=$(PREFIX), R=image1:, PORT=Image1, ADDR=0, TIMEOUT=1, NDARRAY_PORT=$(PORT), TYPE=$(IMAGE_ASYN_TYPE=Int16), FTVL=$(IMAGE_WAVEFORM_TYPE=USHORT), NELEMENTS=$(MAX_IMAGE_PIXELS)")

# Create 1 ROI plugin
NDROIConfigure("ROI1", $(QSIZE=20), 0, "$(PORT)", 0, 0, 0, 0, 0, $(MAX_THREADS=4))
dbLoadRecords("NDROI.template", "P=$(PREFIX), R=ROI1:, PORT=ROI1, ADDR=0, TIMEOUT=1, NDARRAY_PORT=$(PORT)")

# Create 1 statistics plugin with time series
NDStatsConfigure("STATS1", $(QSIZE=20), 0, "ROI1", 0, 0, 0, 0, 0, $(MAX_THREADS=4))
dbLoadRecords("NDStats.template", "P=$(PREFIX), R=Stats1:, PORT=STATS1, ADDR=0, TIMEOUT=1, HIST_SIZE=256, XSIZE=$(MAX_IMAGE_WIDTH), YSIZE=$(MAX_IMAGE_HEIGHT), NCHANS=$(NCHANS=2048), NDARRAY_PORT=ROI1")
NDTimeSeriesConfigure("STATS1_TS", $(QSIZE=20), 0, "STATS1", 1, 23, 0, 0, 0, 0)
dbLoadRecords("$(ADCORE)/db/NDTimeSeries.template", "P=$(PREFIX), R=Stats1:TS:, PORT=STATS1_TS, ADDR=0, TIMEOUT=1, NDARRAY_PORT=STATS1, NDARRAY_ADDR=1, NCHANS=$(NCHANS=2048), ENABLED=1")

# Configure HDF5 file format plugin
NDFileHDF5Configure("FileHDF1", $(QSIZE_HDF5=50), 0, "$(PORT)", 0)
dbLoadRecords("NDFileHDF5.template", "P=$(PREFIX), R=HDF1:, PORT=FileHDF1, ADDR=0, TIMEOUT=1, NDARRAY_PORT=$(PORT)")
