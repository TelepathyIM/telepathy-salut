LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

TELEPATHY_SALUT_BUILT_SOURCES := \
	src/Android.mk \
	extensions/Android.mk \
	lib/gibber/Android.mk

telepathy-salut-configure-real:
	cd $(TELEPATHY_SALUT_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR=$(CONFIGURE_PKG_CONFIG_LIBDIR) \
	PKG_CONFIG_TOP_BUILD_DIR=$(PKG_CONFIG_TOP_BUILD_DIR) \
	$(TELEPATHY_SALUT_TOP)/$(CONFIGURE) --host=arm-linux-androideabi \
	        --disable-submodules \
		--disable-Werror && \
	for file in $(TELEPATHY_SALUT_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

telepathy-salut-configure: telepathy-salut-configure-real

.PHONY: telepathy-salut-configure

CONFIGURE_TARGETS += telepathy-salut-configure

#include all the subdirs...
-include $(TELEPATHY_SALUT_TOP)/src/Android.mk
-include $(TELEPATHY_SALUT_TOP)/extensions/Android.mk
-include $(TELEPATHY_SALUT_TOP)/lib/gibber/Android.mk
