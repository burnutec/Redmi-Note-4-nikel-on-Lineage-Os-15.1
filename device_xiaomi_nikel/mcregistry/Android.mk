# nikel: MIUI's mcDriverDaemon (Trustonic) has /system/app/mcRegistry built in;
# the trustlets are installed to /vendor/app/mcRegistry (proprietary-files-nikel.txt).
ifeq ($(TARGET_DEVICE),nikel)

NIKEL_MCREG_LINK := $(TARGET_OUT)/app/mcRegistry
$(NIKEL_MCREG_LINK):
	@mkdir -p $(dir $@)
	$(hide) if [ ! -e $@ ] || [ -L $@ ]; then rm -f $@; ln -s /vendor/app/mcRegistry $@; echo "nikel: $@ -> /vendor/app/mcRegistry"; fi

ALL_DEFAULT_INSTALLED_MODULES += $(NIKEL_MCREG_LINK)

endif
