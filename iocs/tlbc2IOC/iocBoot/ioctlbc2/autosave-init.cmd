# Autosave setup after IOC initialization

create_monitor_set("ioc.req", 30, "P=$(PREFIX)")
set_savefile_name("ioc.req", "$(AUTOSAVE_PREFIX).sav")
