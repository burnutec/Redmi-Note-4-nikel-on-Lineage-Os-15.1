# nikel uses a prebuilt kernel. Lineage 15.1 defines INSTALLED_KERNEL_HEADERS only
# when it builds the kernel from source, but some modules (libtinycompress, ...)
# still depend on it. Provide an empty target; those modules then use the
# kernel headers that ship with bionic.
.PHONY: INSTALLED_KERNEL_HEADERS
INSTALLED_KERNEL_HEADERS: ; $(hide) mkdir -p $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
