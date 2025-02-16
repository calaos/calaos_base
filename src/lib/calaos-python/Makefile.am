pythonsrcdir = $(srcdir)

calaospythondir = $(prefix)/lib/python/calaos_extern_proc
calaospython_PYTHON = \
    $(pythonsrcdir)/calaos_extern_proc/__init__.py \
    $(pythonsrcdir)/calaos_extern_proc/extern_proc.py \
    $(pythonsrcdir)/calaos_extern_proc/message.py

EXTRA_DIST = \
    python/calaos_extern_proc/__init__.py \
    python/calaos_extern_proc/extern_proc.py \
    python/calaos_extern_proc/message.py

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(prefix)/lib/python
	$(MKDIR_P) $(DESTDIR)$(prefix)/lib/python/calaos_extern_proc
	echo "$(prefix)/lib/python" > $(DESTDIR)$(PYTHON_SITE_PKG)/calaos.pth

uninstall-local:
	rm -rf $(DESTDIR)$(pythondir)/calaos_extern_proc*
	rm -f $(DESTDIR)$(PYTHON_SITE_PKG)/calaos.pth
