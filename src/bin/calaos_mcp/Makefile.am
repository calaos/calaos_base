MAINTAINERCLEANFILES = Makefile.in

# Install locations for the Python package.
calaosmcpdir       = $(prefix)/lib/calaos/calaos_mcp
calaosmcptoolsdir  = $(prefix)/lib/calaos/calaos_mcp/tools

bin_SCRIPTS =
calaosmcp_PYTHON =
calaosmcptools_PYTHON =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST = calaos_mcp.in

if HAVE_PYTHON_MCP
bin_SCRIPTS += calaos_mcp
calaosmcp_PYTHON += \
	python/calaos_mcp/__init__.py \
	python/calaos_mcp/__main__.py \
	python/calaos_mcp/server.py \
	python/calaos_mcp/config.py \
	python/calaos_mcp/client.py \
	python/calaos_mcp/auth.py \
	python/calaos_mcp/models.py \
	python/calaos_mcp/safety.py
calaosmcptools_PYTHON += \
	python/calaos_mcp/tools/__init__.py \
	python/calaos_mcp/tools/_home.py \
	python/calaos_mcp/tools/io.py \
	python/calaos_mcp/tools/rooms.py \
	python/calaos_mcp/tools/scenario.py \
	python/calaos_mcp/tools/audio.py
BUILT_SOURCES += calaos_mcp
CLEANFILES += calaos_mcp
endif

install-data-hook:
if HAVE_PYTHON_MCP
	chmod +x $(DESTDIR)$(bindir)/calaos_mcp
endif
