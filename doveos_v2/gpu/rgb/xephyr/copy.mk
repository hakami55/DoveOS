include Makefile
EDGE=/mnt/c/vscode/HPlapdock/doveos_v2/gpu/rgb/xephyr
$(EDGE)/glamor_copy.o: $(EDGE)/glamor_copy.c
	$(COMPILE) -I$(srcdir) -c $< -o $@
