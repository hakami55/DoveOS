include Makefile
EDGE=/mnt/c/vscode/HPlapdock/doveos_v2/gpu/rgb/xephyr
RGB=$(EDGE)
EDGE_OBJECTS=$(subst ephyr.o,$(RGB)/ephyr-modes.o,$(subst hostx.o,$(RGB)/hostx-resize.o,$(subst ephyr_glamor_glx.o,$(RGB)/ephyr-resize-root.o,$(Xephyr_OBJECTS))))
EDGE_LIBS=$(subst $(top_builddir)/glamor/libglamor.la,$(EDGE)/libglamor.la,$(Xephyr_LDADD))
$(EDGE)/Xephyr-rgb-720: $(EDGE_OBJECTS) $(EDGE)/.libs/libglamor.a
	$(Xephyr_LINK) $(EDGE_OBJECTS) $(EDGE_LIBS) $(LIBS)

$(RGB)/ephyr-modes.o: $(RGB)/ephyr-modes.c
	$(COMPILE) -c $< -o $@
$(RGB)/hostx-resize.o: $(RGB)/hostx-resize.c
	$(COMPILE) -c $< -o $@
$(RGB)/ephyr-resize-root.o: $(RGB)/ephyr-resize-root.c $(RGB)/resize-root.inc
	$(COMPILE) -c $< -o $@
