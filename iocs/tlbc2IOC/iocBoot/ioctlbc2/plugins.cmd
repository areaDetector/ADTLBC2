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
# $(MAX_THREADS)
# The maximum number of threads for plugins which can run in multiple threads.
# Defaults to 4.

# Create Channel Access conversion plugin
NDStdArraysConfigure("Image1", $(QSIZE=20), 0, $(PORT), 0, 0, 0, 0, $(MAX_THREADS=4))
dbLoadRecords("NDStdArrays.template", "P=$(PREFIX), R=image1:, PORT=Image1, ADDR=0, TIMEOUT=1, NDARRAY_PORT=$(PORT), TYPE=$(IMAGE_ASYN_TYPE=Int16), FTVL=$(IMAGE_WAVEFORM_TYPE=USHORT), NELEMENTS=$(MAX_IMAGE_PIXELS)")
