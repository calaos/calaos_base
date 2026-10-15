MAINTAINERCLEANFILES = Makefile.in

# Install locations for the Python package.
calaosmcpdir       = $(prefix)/lib/calaos/calaos_mcp
calaosmcptoolsdir  = $(prefix)/lib/calaos/calaos_mcp/tools

bin_SCRIPTS =
calaosmcp_PYTHON =
calaosmcptools_PYTHON =
BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST = calaos_mcp.in pyproject.toml

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
	python/calaos_mcp/safety.py \
	python/calaos_mcp/iodoc.py
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

# Command format of the devices, rendered into the set_io_state tool
# description. Best effort: a calaos_server that cannot run here (cross build,
# failed gendoc, hang past the timeout) leaves an empty document and the
# sidecar degrades to the short description, instead of failing the build.
# A cross build installs the document of a host build (or the io-doc artifact
# of the CI) with IODOC_PREBUILT=/path/to/io_doc.json; a file that is not one is
# an error there, since it was asked for.
pkgdata_DATA = io_doc.json
CLEANFILES += io_doc.json
CALAOS_SERVER = $(top_builddir)/src/bin/calaos_server/calaos_server
IODOC_PREBUILT =

io_doc.json: $(CALAOS_SERVER)
	@if test -n "$(IODOC_PREBUILT)"; then \
	    if grep -q '"devices"' "$(IODOC_PREBUILT)" 2>/dev/null; \
	    then cp "$(IODOC_PREBUILT)" $@; \
	    else echo "error: IODOC_PREBUILT=$(IODOC_PREBUILT) is not an io_doc.json with devices"; exit 1; fi; \
	else \
	    rm -rf iodoc.tmp && mkdir -p iodoc.tmp/config iodoc.tmp/cache iodoc.tmp/out; \
	    d=`pwd`/iodoc.tmp; t=; \
	    if command -v timeout >/dev/null 2>&1; then t="timeout 120"; fi; \
	    LC_ALL=C LANGUAGE=C $$t $(CALAOS_SERVER) --config $$d/config --cache $$d/cache \
	        --gendoc $$d/out >$$d/log 2>&1; \
	    if grep -q '"devices"' $$d/out/io_doc.json 2>/dev/null; \
	    then mv $$d/out/io_doc.json $@; \
	    else echo "warning: calaos_server --gendoc failed, no device documentation"; echo '{}' > $@; fi; \
	    rm -rf iodoc.tmp; \
	fi

install-data-hook:
if HAVE_PYTHON_MCP
	chmod +x $(DESTDIR)$(bindir)/calaos_mcp
endif
