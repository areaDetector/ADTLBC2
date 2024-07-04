# Save and restore setup

save_restoreSet_status_prefix("$(AUTOSAVE_PREFIX)")
save_restoreSet_Debug(0)

save_restoreSet_IncompleteSetsOk(1)
save_restoreSet_DatedBackupFiles(1)

save_restoreSet_NumSeqFiles(3)
save_restoreSet_SeqPeriodInSeconds(300)

set_savefile_path("C:/ProgramData/ADTLBC2", "autosave")

set_pass0_restoreFile("$(AUTOSAVE_PREFIX).sav")
set_pass1_restoreFile("$(AUTOSAVE_PREFIX).sav")

set_requestfile_path(".")
set_requestfile_path("$(CALC)", "calcApp/Db")
set_requestfile_path("$(ADCORE)", "ADApp/Db")
set_requestfile_path("$(ADTLBC2)", "db")
